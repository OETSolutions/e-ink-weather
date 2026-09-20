/* Host tests for the captive portal's DNS reply construction.
 *
 * WHY THESE EXIST: the firmware had no test for this, and two independent wire-format bugs
 * shipped as a result. Both were invisible to a hand-rolled probe and to the firmware's own
 * logging; each was found only by pointing a real resolver at a live device. They are the
 * reason this logic was moved out of the component and into a library — wire format is
 * exactly what should be checked on the host, byte by byte, without a radio.
 *
 * The regression cases are marked, and each one fails against the code as it was. */

#include "unity.h"
#include "provdns.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---- request builders ------------------------------------------------------------------ */

/* A DNS request for one A question. `flags` is in WIRE order, so the caller writes the bits as
 * the diagram in provdns.c reads them. */
static size_t make_query(uint8_t *buf, size_t max, const char *name,
                         uint16_t flags, int with_edns)
{
    size_t n = 0;
    buf[n++] = 0x12; buf[n++] = 0x34;                 /* id */
    buf[n++] = (uint8_t)(flags >> 8); buf[n++] = (uint8_t)flags;
    buf[n++] = 0; buf[n++] = 1;                       /* qdcount */
    buf[n++] = 0; buf[n++] = 0;                       /* ancount */
    buf[n++] = 0; buf[n++] = 0;                       /* nscount */
    buf[n++] = 0; buf[n++] = (uint8_t)(with_edns ? 1 : 0);  /* arcount */

    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        const size_t len = dot ? (size_t)(dot - p) : strlen(p);
        buf[n++] = (uint8_t)len;
        memcpy(buf + n, p, len);
        n += len;
        if (!dot) break;
        p = dot + 1;
    }
    buf[n++] = 0;                                     /* root label */
    buf[n++] = 0; buf[n++] = 1;                       /* qtype A */
    buf[n++] = 0; buf[n++] = 1;                       /* qclass IN */
    (void)max;

    if (with_edns) {
        /* OPT: root name, type 41, class = UDP payload size, ttl = 0, rdlen = 0. */
        buf[n++] = 0;
        buf[n++] = 0; buf[n++] = 41;
        buf[n++] = 0x04; buf[n++] = 0xD0;             /* 1232 */
        buf[n++] = 0; buf[n++] = 0; buf[n++] = 0; buf[n++] = 0;
        buf[n++] = 0; buf[n++] = 0;
    }
    return n;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }

/* The device's own address as esp_ip4_addr_t.addr holds it: a uint32_t whose in-memory bytes
 * are already in network order. On this little-endian host that means the literal reads
 * "backwards" — 0x0104A8C0 stores as the bytes C0 A8 04 01, i.e. 192.168.4.1 on the wire.
 * Writing it as 0x01A8C0C0 would be the mistake the library itself had to avoid: that value
 * stores as 01 04 A8 C0 and answers with 1.4.168.192. */
#define AP_IP_BE 0x0104A8C0u

/* ---- the flag byte-order regression ---------------------------------------------------- */

/* THE BUG THIS CATCHES. The masks were written in wire order while the request was inspected
 * without byte-swapping, so the opcode test read byte-reversed bits. A query with only the
 * recursion-desired bit set (0x0100) read natively as 0x0001 — opcode zero, so it was
 * answered — which is why the hand-rolled probe passed. dig, Chrome and systemd-resolved all
 * set the AD bit as well, making the wire value 0x0120, which read natively as 0x2001 and was
 * dropped as an unsupported opcode with NO reply at all. The portal worked for the test and
 * failed for every real client. */
static void test_rd_only_query_is_answered(void)
{
    uint8_t req[128], rep[256];
    const size_t rl = make_query(req, sizeof(req), "example.com", 0x0100, 0);
    const int n = provdns_build_reply(req, rl, rep, sizeof(rep), AP_IP_BE);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
}

static void test_rd_plus_ad_query_is_answered(void)
{
    uint8_t req[128], rep[256];
    const size_t rl = make_query(req, sizeof(req), "example.com", 0x0120, 0);
    const int n = provdns_build_reply(req, rl, rep, sizeof(rep), AP_IP_BE);
    /* 0 means "ignore silently" — the exact failure mode a real client sees as a timeout. */
    TEST_ASSERT_GREATER_THAN_INT(0, n);
}

/* A query with an unsupported opcode must still be ignored rather than answered wrongly. */
static void test_nonzero_opcode_is_ignored(void)
{
    uint8_t req[128], rep[256];
    const size_t rl = make_query(req, sizeof(req), "example.com", 0x2800, 0);  /* opcode 5 */
    TEST_ASSERT_EQUAL_INT(0, provdns_build_reply(req, rl, rep, sizeof(rep), AP_IP_BE));
}

/* ---- the EDNS layout regression -------------------------------------------------------- */

/* THE SECOND BUG. The request was copied wholesale and the answer appended, so a client's
 * EDNS OPT record ended up between the question and the answer. The OPT record must be the
 * LAST record in the additional section, so resolvers rejected the packet as malformed
 * ("message parser reports malformed message packet") and then timed out. Because dig, Chrome
 * and systemd-resolved all attach an OPT record by default, this broke every real client while
 * a query with no additional section worked. */
static void test_edns_opt_record_is_not_copied_into_the_reply(void)
{
    uint8_t req[128], rep[256];
    const size_t rl = make_query(req, sizeof(req), "example.com", 0x0100, 1);
    const int n = provdns_build_reply(req, rl, rep, sizeof(rep), AP_IP_BE);

    TEST_ASSERT_GREATER_THAN_INT(0, n);
    /* The reply must be header + question + exactly one answer, with no trailing OPT. */
    TEST_ASSERT_EQUAL_INT(12 + 17 + 16, n);
    /* And the counts must describe what is actually there. */
    TEST_ASSERT_EQUAL_INT(0, be16(rep + 8));    /* nscount */
    TEST_ASSERT_EQUAL_INT(0, be16(rep + 10));   /* arcount — the client's OPT is gone */
}

/* ---- reply contents -------------------------------------------------------------------- */

static void test_reply_header_and_answer_are_correct(void)
{
    uint8_t req[128], rep[256];
    const size_t rl = make_query(req, sizeof(req), "example.com", 0x0100, 0);
    const int n = provdns_build_reply(req, rl, rep, sizeof(rep), AP_IP_BE);

    TEST_ASSERT_GREATER_THAN_INT(0, n);
    TEST_ASSERT_EQUAL_HEX16(0x1234, be16(rep + 0));            /* id echoed */
    TEST_ASSERT_EQUAL_HEX16(0x8100, be16(rep + 2));            /* QR set, RD echoed, AD clear */
    TEST_ASSERT_EQUAL_INT(1, be16(rep + 4));                   /* qdcount */
    TEST_ASSERT_EQUAL_INT(1, be16(rep + 6));                   /* ancount */
    TEST_ASSERT_EQUAL_INT(0, be16(rep + 8));
    TEST_ASSERT_EQUAL_INT(0, be16(rep + 10));

    /* The answer: name pointer to offset 12 (the question's name), type A, class IN,
     * ttl 300, rdlength 4, and the address bytes in network order. */
    const uint8_t *a = rep + n - 16;
    TEST_ASSERT_EQUAL_HEX16(0xC00C, be16(a + 0));
    TEST_ASSERT_EQUAL_HEX16(0x0001, be16(a + 2));
    TEST_ASSERT_EQUAL_HEX16(0x0001, be16(a + 4));
    TEST_ASSERT_EQUAL_INT(300, (int)((uint32_t)a[6] << 24 | (uint32_t)a[7] << 16 |
                                     (uint32_t)a[8] << 8 | a[9]));
    TEST_ASSERT_EQUAL_HEX16(4, be16(a + 10));
    TEST_ASSERT_EQUAL_UINT8(192, a[12]);
    TEST_ASSERT_EQUAL_UINT8(168, a[13]);
    TEST_ASSERT_EQUAL_UINT8(4,   a[14]);
    TEST_ASSERT_EQUAL_UINT8(1,   a[15]);
}

/* The AD bit must not be echoed: this server is in no position to assert that its data was
 * authenticated, and claiming it would be a lie a validating client might act on. */
static void test_ad_bit_is_cleared_in_the_reply(void)
{
    uint8_t req[128], rep[256];
    const size_t rl = make_query(req, sizeof(req), "example.com", 0x0130, 0);  /* RD+AD+CD */
    const int n = provdns_build_reply(req, rl, rep, sizeof(rep), AP_IP_BE);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    TEST_ASSERT_EQUAL_HEX16(0x8100, be16(rep + 2));
}

/* ---- robustness ------------------------------------------------------------------------ */

static void test_truncated_request_is_rejected(void)
{
    uint8_t req[128], rep[256];
    const size_t rl = make_query(req, sizeof(req), "example.com", 0x0100, 0);
    /* Every prefix shorter than a full header must be refused, not read out of bounds. */
    TEST_ASSERT_EQUAL_INT(-1, provdns_build_reply(req, 0, rep, sizeof(rep), AP_IP_BE));
    TEST_ASSERT_EQUAL_INT(-1, provdns_build_reply(req, 11, rep, sizeof(rep), AP_IP_BE));
    /* A header claiming a question that is not there. */
    uint8_t short_req[12];
    memcpy(short_req, req, 12);
    TEST_ASSERT_EQUAL_INT(-1, provdns_build_reply(short_req, 12, rep, sizeof(rep), AP_IP_BE));
    (void)rl;
}

static void test_oversized_request_is_rejected(void)
{
    /* 64 bytes cannot hold header + question + answer (45), so the reply is refused rather
     * than truncated — a short reply would leave the client parsing a partial record. */
    uint8_t req[128], rep[44];
    const size_t rl = make_query(req, sizeof(req), "example.com", 0x0100, 0);
    TEST_ASSERT_EQUAL_INT(-1, provdns_build_reply(req, rl, rep, sizeof(rep), AP_IP_BE));
}

/* A name with a compression pointer must not be followed — following one is how a crafted
 * packet makes the parser loop. The reply is refused, not hung on. */
static void test_compressed_question_name_does_not_loop(void)
{
    uint8_t req[64];
    memset(req, 0, sizeof(req));
    req[0] = 0x12; req[1] = 0x34;
    req[5] = 1;                                  /* qdcount = 1 */
    req[12] = 0xC0; req[13] = 0x0C;              /* name is a pointer to itself */
    req[14] = 0; req[15] = 1;                    /* qtype A */
    req[16] = 0; req[17] = 1;                    /* qclass IN */
    uint8_t rep[256];
    const int n = provdns_build_reply(req, 18, rep, sizeof(rep), AP_IP_BE);
    TEST_ASSERT_GREATER_THAN_INT(0, n);          /* answered without looping */
}

/* A zero-question query has nothing to answer and must not be echoed back as one. */
static void test_zero_questions_is_ignored(void)
{
    uint8_t req[12], rep[256];
    memset(req, 0, sizeof(req));
    req[1] = 0x00;
    TEST_ASSERT_EQUAL_INT(0, provdns_build_reply(req, sizeof(req), rep, sizeof(rep), AP_IP_BE));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rd_only_query_is_answered);
    RUN_TEST(test_rd_plus_ad_query_is_answered);
    RUN_TEST(test_nonzero_opcode_is_ignored);
    RUN_TEST(test_edns_opt_record_is_not_copied_into_the_reply);
    RUN_TEST(test_reply_header_and_answer_are_correct);
    RUN_TEST(test_ad_bit_is_cleared_in_the_reply);
    RUN_TEST(test_truncated_request_is_rejected);
    RUN_TEST(test_oversized_request_is_rejected);
    RUN_TEST(test_compressed_question_name_does_not_loop);
    RUN_TEST(test_zero_questions_is_ignored);
    return UNITY_END();
}
