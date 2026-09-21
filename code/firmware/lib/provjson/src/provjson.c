#include "provjson.h"

#include <stdio.h>

int provjson_escape(const char *s, char *out, size_t out_max)
{
    if (!out || out_max == 0) return -1;

    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        if (*p == '"' || *p == '\\') {
            if (o + 2 >= out_max) break;          /* need 2, plus room for the NUL */
            out[o++] = '\\';
            out[o++] = (char)*p;
        } else if (*p < 0x20 || *p >= 0x80) {
            if (o + 7 >= out_max) break;          /* need 6, plus room for the NUL */
            o += (size_t)snprintf(out + o, out_max - o, "\\u%04x", *p);
        } else {
            if (o + 1 >= out_max) break;
            out[o++] = (char)*p;
        }
    }
    out[o] = '\0';
    return 0;
}

int provjson_list_begin(char *buf, size_t cap, size_t *o)
{
    if (!buf || !o) return -1;
    /* Need `{"list":[` (9) plus the NUL. */
    if (cap < 10) return -1;
    *o = (size_t)snprintf(buf, cap, "{\"list\":[");
    return 0;
}

int provjson_list_add(char *buf, size_t cap, size_t *o, const char *ssid, int rssi, int first)
{
    if (!buf || !o || !ssid) return -1;

    char esc[PROVJSON_MAX_ESCAPED + 8];
    if (provjson_escape(ssid, esc, sizeof(esc)) != 0) return -1;

    /* Measure this exact entry rather than assuming the worst case, so the check is tight for
     * ordinary names and the buffer holds as many networks as it truly can. `need` is the JSON
     * text without its terminator. */
    const int need = snprintf(NULL, 0, "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                              first ? "" : ",", esc, rssi);
    if (need < 0) return -1;

    /* THE CHECK, and the whole point of this file. Three bytes must remain after this entry:
     * `]}` for provjson_list_end() and the NUL that terminates the document. Reserving only two
     * is an off-by-one — it passes here and then loses the final byte in end(). And the check is
     * what makes `o` safe to use as an offset: snprintf returns the length it WOULD have written,
     * so without it a too-long entry would push `o` past the buffer and the NEXT call's
     * `cap - *o` would underflow to a near-SIZE_MAX size_t, which is precisely the heap overflow
     * this library was extracted from prov_ap.c to fix and to keep fixed. */
    if (*o + (size_t)need + 3 > cap) return 1;

    const size_t o0 = *o;
    const size_t at = (size_t)snprintf(buf + o0, cap - o0, "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                                       first ? "" : ",", esc, rssi);
    /* Unreachable for any cap the check above accepted. It is here so that a caller passing a
     * cap it does not actually own still gets a NUL-terminated buffer and a refusal instead of a
     * bad offset; an offset past the buffer is the one failure that must never be stored. */
    if (o0 + at >= cap) {
        buf[o0] = '\0';
        return 1;
    }
    *o = o0 + at;
    return 0;
}

int provjson_list_end(char *buf, size_t cap, size_t *o)
{
    if (!buf || !o) return -1;
    /* `]}` plus the NUL. begin() reserved 10 and every add() reserved these same 3, so this
     * cannot fail for a caller that used the API correctly; refusing rather than truncating
     * means a bookkeeping bug surfaces as an error instead of a malformed document. */
    if (*o + 3 > cap) {
        if (*o < cap) buf[*o] = '\0';
        return -1;
    }
    (void)snprintf(buf + *o, cap - *o, "]}");
    *o += 2;
    return 0;
}
