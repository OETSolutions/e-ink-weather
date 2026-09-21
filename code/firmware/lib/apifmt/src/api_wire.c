#include "api_wire.h"
#include <string.h>

/* A uint32_t holds at most 10 decimal digits (4294967295). Any 11th digit overflows, so the
 * bound is checked digit by digit rather than by parsing into a wider type and clamping —
 * clamping would silently turn a garbage offset into a plausible one. */
#define U32_MAX_DIGITS 10

static int key_matches(const char *p, const char *key, size_t klen)
{
    return strncmp(p, key, klen) == 0;
}

int api_query_u32(const char *query, const char *key, uint32_t *out)
{
    if (!query || !key || !out || key[0] == '\0') return -1;

    const size_t klen = strlen(key);
    const char *p = query;

    for (;;) {
        /* Advance to the start of the next key. A leading '?' is skipped once so callers
         * may pass either the bare query or req->uri's query part verbatim. */
        while (*p == '?' || *p == '&') p++;
        if (*p == '\0') return -1;              /* ran off the end without a match */

        const char *kstart = p;
        while (*p && *p != '=' && *p != '&') p++;
        const size_t this_klen = (size_t)(p - kstart);

        /* A key with no '=' is a flag, not a value: no match, move on. */
        if (*p != '=') {
            while (*p && *p != '&') p++;
            continue;
        }
        p++;                                     /* past '=' */

        const char *vstart = p;
        while (*p && *p != '&') p++;
        const size_t vlen = (size_t)(p - vstart);

        if (this_klen == klen && key_matches(kstart, key, klen)) {
            if (vlen == 0 || vlen > U32_MAX_DIGITS) return -1;
            uint32_t v = 0;
            for (size_t i = 0; i < vlen; i++) {
                const char c = vstart[i];
                if (c < '0' || c > '9') return -1;   /* no sign, no spaces, no units */
                const uint32_t d = (uint32_t)(c - '0');
                /* Check BEFORE multiplying: 4294967296 must be rejected, not wrapped to 0. */
                if (v > (0xFFFFFFFFu - d) / 10u) return -1;
                v = v * 10u + d;
            }
            *out = v;
            return 0;
        }
    }
}

int api_location_is_set(double lat, double lon)
{
    /* NaN fails every comparison, so a malformed coordinate is rejected by the range checks
     * rather than slipping through as "not the sentinel". */
    if (!(lat >= -90.0 && lat <= 90.0)) return 0;
    if (!(lon >= -180.0 && lon <= 180.0)) return 0;
    /* Both parts zero is the app's "not chosen yet" default, not a place. Compare exactly:
     * this is a sentinel, not a threshold, and the app writes literal 0s. */
    if (lat == 0.0 && lon == 0.0) return 0;
    return 1;
}
