#pragma once

/* The captive portal's DNS reply construction, as pure byte-level logic.
 *
 * WHY THIS IS A LIBRARY AND NOT PART OF THE COMPONENT: this is wire-format code, and two
 * separate bugs in it were invisible to the firmware's own tests and to a hand-rolled probe —
 * both were found only by pointing a real resolver at a live device. Wire format is exactly
 * the kind of thing that should be tested on the host, byte by byte, without a radio.
 *
 * The two bugs, both now covered in test/test_provdns:
 *
 *   1. The flags field was inspected without byte-swapping while the bit masks were written
 *      in wire order. On this little-endian target a query with only the recursion-desired
 *      bit set (wire 0x0100) read as 0x0001 — opcode zero, so it was answered — while the
 *      same query with the AD bit set (wire 0x0120, what dig, Chrome and systemd-resolved
 *      send by default) read as 0x2001 and was dropped as an unsupported opcode with no
 *      reply. The portal therefore worked for the test and failed for real clients.
 *   2. The request was copied wholesale and the answer appended, so a client's EDNS OPT
 *      record ended up BEFORE the answer. The OPT record must be last in the additional
 *      section, so resolvers rejected the packet as malformed and timed out. dig, Chrome and
 *      systemd-resolved all attach an OPT record by default.
 *
 * Both are gone by construction here: the header is read field by field through explicit
 * big-endian helpers (no packed structs, no native-order bit tests), and only the header and
 * question section are ever copied forward. */

#include <stddef.h>
#include <stdint.h>

/* Build a reply that points every A question at `ip_be`.
 *
 * `ip_be` is the address in NETWORK byte order — the same form as
 * esp_netif_ip_info_t.ip.addr — and is copied into the answer verbatim.
 *
 * Returns the reply length in bytes, 0 if the request is not a standard query and should be
 * ignored silently, or -1 if it is malformed or does not fit in `reply_max`.
 *
 * Any section the client appended after its questions (EDNS OPT, TSIG) is dropped, and the
 * authority and additional counts are zeroed so the reply does not claim records that are not
 * present. */
int provdns_build_reply(const uint8_t *req, size_t req_len,
                        uint8_t *reply, size_t reply_max, uint32_t ip_be);
