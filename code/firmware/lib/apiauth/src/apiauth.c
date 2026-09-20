#include "apiauth.h"
#include <string.h>

/* Constant-time-ish equality, over the FULL length of both strings.
 *
 * WHY NOT strcmp/memcmp: memcmp returns at the first differing byte, so the time it takes
 * reveals how many leading bytes matched. An attacker who can measure that recovers the
 * token one byte at a time. This walks every byte of both strings regardless, accumulating
 * differences, and only then reports. The lengths differ case is handled by folding a
 * length difference into the result rather than returning early.
 *
 * This is only worth doing because a token is a secret with no rate limit in front of it.
 * For a non-secret comparison it would be pointless complexity. */
static int secure_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;

    size_t la = strlen(a);
    size_t lb = strlen(b);
    size_t n = la > lb ? la : lb;      /* walk the longer one */

    unsigned diff = (unsigned)(la ^ lb);   /* any length difference makes this non-zero */

    for (size_t i = 0; i < n; i++) {
        /* Reading past the shorter string would be a bug AND a leak, so index defensively:
         * beyond the end, contribute 0 and let the `diff` above carry the verdict. */
        unsigned ca = i < la ? (unsigned char)a[i] : 0u;
        unsigned cb = i < lb ? (unsigned char)b[i] : 0u;
        diff |= ca ^ cb;
    }
    return diff == 0;
}

int apiauth_header_matches(const char *authorization, const char *expected)
{
    /* A device with no token set must never accept an empty bearer — see the header. */
    if (!expected || expected[0] == '\0') return 0;
    if (!authorization) return 0;

    /* Exactly "Bearer <token>", scheme case-insensitive (RFC 7235 says the scheme is
     * case-insensitive; clients emit both). One space is required.
     *
     * The reference is stored LOWER-CASE and the received byte is folded DOWN to match. The
     * first version of this compared a folded byte against "Bearer" as written — capital B —
     * so a correct header never matched. Folding one side means the other side must already
     * be in the folded case. */
    static const char SCHEME[] = "bearer ";
    const size_t n = sizeof(SCHEME) - 1;
    for (size_t i = 0; i < n; i++) {
        const char c = authorization[i];
        if (c == '\0') return 0;                      /* shorter than the scheme */
        char got = c;
        /* Fold the scheme only. Folding the TOKEN too would make it case-insensitive, and a
         * token is an opaque byte string — "ABC123" is not the token "abc123". */
        if (got >= 'A' && got <= 'Z') got = (char)(got - 'A' + 'a');
        if (got != SCHEME[i]) return 0;
    }
    return secure_eq(authorization + n, expected);
}

int apiauth_required(int enabled, const char *token)
{
    if (!enabled) return 0;
    /* Enabled but no usable token: do NOT require one. A device in this state would demand a
     * credential that cannot exist, and the only way out would be a factory reset — a
     * self-inflicted lockout is a worse outcome than the exposure this guards. */
    if (!token || token[0] == '\0') return 0;
    return 1;
}

int apiauth_token_is_usable(const char *token)
{
    if (!token || token[0] == '\0') return 0;

    size_t n = 0;
    for (const char *p = token; *p; p++, n++) {
        if (n >= APIAUTH_TOKEN_MAX) return 0;              /* too long to store */
        const unsigned char c = (unsigned char)*p;
        /* A space would break the header parse; a control byte would be stripped or mangled
         * in transit. Either way the token could not be sent, so it must not be stored. */
        if (c <= ' ' || c == 0x7f) return 0;
    }
    return 1;
}

void apiauth_make_token(char *out, size_t len, unsigned (*rand_next)(void))
{
    if (!out) return;
    static const char HEX[] = "0123456789abcdef";
    size_t i = 0;
    for (; i + 1 < len; i += 2) {
        /* One rand() per BYTE, split into two nibbles — drawing a fresh number per nibble
         * would waste entropy and make an even-length token look like two concatenated
         * ones. */
        const unsigned v = rand_next() & 0xffu;
        out[i]     = HEX[(v >> 4) & 0x0f];
        out[i + 1] = HEX[v & 0x0f];
    }
    if (i < len) out[i++] = HEX[rand_next() & 0x0f];
    out[i] = '\0';
}
