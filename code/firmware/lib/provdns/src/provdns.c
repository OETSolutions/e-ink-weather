#include "provdns.h"

#include <string.h>

/* Every field is read and written through explicit big-endian helpers rather than by casting
 * to a packed struct. A packed struct invites exactly the bug this code was extracted to
 * eliminate: it makes the compiler lay the bytes out for the HOST, so a 16-bit field read
 * straight out of the buffer is in native order and any bit mask written in wire order tests
 * the wrong bits. The helpers make the byte order explicit at every access, so there is no
 * native-order value anywhere in this file to get wrong. */

#define DNS_OFF_ID        0
#define DNS_OFF_FLAGS     2
#define DNS_OFF_QDCOUNT   4
#define DNS_OFF_ANCOUNT   6
#define DNS_OFF_NSCOUNT   8
#define DNS_OFF_ARCOUNT   10
#define DNS_HDR_LEN       12

#define DNS_TYPE_A   0x0001
#define DNS_CLASS_IN 0x0001

/* Flag and opcode bits in NETWORK (WIRE) BIT ORDER — the value the field has after
 * get_be16(), i.e. as the diagram below reads left to right.
 *
 *     bit 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 *         QR [  opcode ] AA TC RD RA Z AD CD [  rcode  ]
 */
#define DNS_FLAG_QR     0x8000
#define DNS_FLAG_RD     0x0100
#define DNS_OPCODE_MASK 0x7800

#define DNS_TTL_SEC 300

/* The name in an answer is sent as "the name at this offset in the message", flagged by the
 * top two bits. Sending the name again instead would work but wastes space and is not what
 * clients expect. */
#define DNS_PTR_COMPRESS 0xC000

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Skip over one DNS name in wire form ("len,bytes,len,bytes,0"). Returns the offset just past
 * the terminating zero, or 0 if the name runs off the end of the buffer or contains a label
 * long enough to be malformed.
 *
 * A pointer-compressed name (top two bits of the first byte set) ends the walk immediately:
 * this server never needs to resolve one, and following a pointer is what lets a crafted
 * packet loop forever. Treating it as "name ends here" is enough to find the question that
 * follows, and the answer always points at the question's own offset. */
static size_t skip_name(const uint8_t *buf, size_t len, size_t off)
{
    while (off < len) {
        const uint8_t label = buf[off];
        if ((label & 0xC0) == 0xC0) return off + 2;   /* pointer: two bytes, then done */
        if (label == 0) return off + 1;               /* root: the terminator itself */
        off += (size_t)label + 1;
    }
    return 0;
}

int provdns_build_reply(const uint8_t *req, size_t req_len,
                        uint8_t *reply, size_t reply_max, uint32_t ip_be)
{
    if (!req || !reply) return -1;
    if (req_len < DNS_HDR_LEN || req_len > reply_max) return -1;

    const uint16_t flags   = get_be16(req + DNS_OFF_FLAGS);
    const uint16_t qd_count = get_be16(req + DNS_OFF_QDCOUNT);

    /* Only standard queries get an answer. Anything else — a response, an opcode we do not
     * implement — is ignored rather than answered wrongly, because a malformed answer makes
     * the client retry forever instead of falling through to its real resolver. */
    if ((flags & DNS_OPCODE_MASK) != 0) return 0;
    if (qd_count == 0) return 0;

    /* Walk the question section to find where it ends, and count the A questions while doing
     * it. This is what makes the reply well formed: the answer is written at q_end, not at
     * the end of the request, so anything the client appended (an EDNS OPT record, a TSIG) is
     * left out entirely. Copying the request wholesale and appending the answer would put the
     * answer AFTER the client's OPT record — and since the OPT record must be last in the
     * additional section, resolvers reject the whole packet as malformed.
     *
     * Counting A questions here rather than reusing qd_count matters: a question of any other
     * type is copied through but gets no answer, so announcing qd_count answers would tell the
     * client to parse records that were never written. */
    size_t q_end = DNS_HDR_LEN;
    uint16_t an_count = 0;
    for (uint16_t i = 0; i < qd_count; i++) {
        const size_t name_end = skip_name(req, req_len, q_end);
        if (name_end == 0) return -1;
        /* A question is the name plus qtype and qclass. */
        if (name_end + 4 > req_len) return -1;
        if (get_be16(req + name_end) == DNS_TYPE_A) an_count++;
        q_end = name_end + 4;
    }

    const size_t reply_len = q_end + (size_t)an_count * 16u;   /* 16 = sizeof(answer) */
    if (reply_len > reply_max) return -1;

    /* Only the header and the question section are copied. Everything after q_end in the
     * reply buffer is the answer section, written below. */
    memcpy(reply, req, q_end);
    memset(reply + q_end, 0, reply_len - q_end);

    /* QR marks this as a response; RD is echoed because the client asked for recursion. Every
     * other bit is cleared, including AD and CD, which this server is in no position to
     * assert. */
    put_be16(reply + DNS_OFF_FLAGS, (uint16_t)((flags & DNS_FLAG_RD) | DNS_FLAG_QR));
    put_be16(reply + DNS_OFF_ANCOUNT, an_count);
    /* The request's own counts for the sections that were dropped still sit in the copied
     * header and would claim records that are not in the reply. */
    put_be16(reply + DNS_OFF_NSCOUNT, 0);
    put_be16(reply + DNS_OFF_ARCOUNT, 0);

    size_t q_off = DNS_HDR_LEN;
    size_t a_off = q_end;

    for (uint16_t i = 0; i < qd_count; i++) {
        const size_t name_end = skip_name(reply, q_end, q_off);
        if (name_end == 0) return -1;

        const uint16_t qtype  = get_be16(reply + name_end);
        const uint16_t qclass = get_be16(reply + name_end + 2);

        if (qtype == DNS_TYPE_A) {
            put_be16(reply + a_off + 0, (uint16_t)(DNS_PTR_COMPRESS | q_off));
            put_be16(reply + a_off + 2, DNS_TYPE_A);
            put_be16(reply + a_off + 4, qclass ? qclass : DNS_CLASS_IN);
            put_be32(reply + a_off + 6, DNS_TTL_SEC);
            put_be16(reply + a_off + 10, 4);              /* RDLENGTH */
            /* The address is COPIED, not byte-swapped. `ip_be` is the raw in-memory value of
             * esp_ip4_addr_t.addr, which is already held in network byte order — so passing it
             * through a big-endian store would swap it a second time and answer with a
             * byte-reversed address (192.168.4.1 would go out as 1.4.168.192). Copying its
             * four bytes verbatim is what puts the correct octets on the wire. */
            memcpy(reply + a_off + 12, &ip_be, 4);
            a_off += 16;
        }
        q_off = name_end + 4;
    }

    return (int)a_off;
}
