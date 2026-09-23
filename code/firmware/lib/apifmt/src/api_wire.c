#include "api_wire.h"
#include <stdio.h>
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

/* The characters an entity-id search term may contain: exactly HA's entity-id set. Everything
 * Jinja would interpret to change the template (quotes, braces, backslash, '%', whitespace,
 * '&', '=') is outside this set and is therefore rejected, which is the whole point — see the
 * header note. The set MATCHES ha_entity_id_valid(), so a term this accepts is one that can
 * only ever match a real entity id. */
static int token_char_ok(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}

int api_query_token(const char *query, const char *key, char *out, size_t outlen)
{
    if (!query || !key || !out || outlen == 0 || key[0] == '\0') return -1;
    out[0] = '\0';

    const size_t klen = strlen(key);
    const char *p = query;

    for (;;) {
        while (*p == '?' || *p == '&') p++;
        if (*p == '\0') return -1;

        const char *kstart = p;
        while (*p && *p != '=' && *p != '&') p++;
        const size_t this_klen = (size_t)(p - kstart);

        if (*p != '=') {                       /* a flag, not a value */
            while (*p && *p != '&') p++;
            continue;
        }
        p++;                                   /* past '=' */

        const char *vstart = p;
        while (*p && *p != '&') p++;
        const size_t vlen = (size_t)(p - vstart);

        if (this_klen == klen && key_matches(kstart, key, klen)) {
            if (vlen == 0 || vlen >= outlen) return -1;
            for (size_t i = 0; i < vlen; i++) {
                if (!token_char_ok(vstart[i])) return -1;
            }
            memcpy(out, vstart, vlen);
            out[vlen] = '\0';
            return 0;
        }
    }
}

/* ------------------------------------------------------------------ location splice --- */

/* Find the value of `"key"` when it is the member of the object that starts at `obj`, which points
 * at the '{'. Returns a pointer to the first byte of the value, or NULL.
 *
 * Only TOP-LEVEL members of that one object are considered, so a same-named key nested deeper — or
 * inside a string — is never picked up. Depth counting skips over nested objects and arrays and
 * honours quoted strings (with backslash escapes) so a brace inside a string cannot skew it. */
static const char *member_value(const char *obj, const char *key, const char *end)
{
    const size_t klen = strlen(key);
    const char *p = obj;
    if (p >= end || *p != '{') return NULL;

    int depth = 0;
    int in_str = 0;
    for (; p < end; p++) {
        const char c = *p;
        if (in_str) {
            if (c == '\\') { p++; continue; }     /* skip the escaped byte */
            if (c == '"') in_str = 0;
            continue;
        }
        if (c == '"') {
            /* A string at top level (depth 1) is a key iff a ':' follows it. */
            const char *qs = p;
            p++;
            while (p < end && *p != '"') { if (*p == '\\') p++; p++; }
            if (p >= end) return NULL;
            const char *after = p + 1;
            while (after < end && (*after == ' ' || *after == '\t' ||
                                   *after == '\n' || *after == '\r')) after++;
            if (depth == 1 && after < end && *after == ':' &&
                (size_t)(p - qs - 1) == klen && strncmp(qs + 1, key, klen) == 0) {
                const char *v = after + 1;
                while (v < end && (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r')) v++;
                return v < end ? v : NULL;
            }
            p = after - 1;                        /* loop's p++ moves to the value */
            continue;
        }
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') {
            depth--;
            if (depth <= 0) return NULL;          /* left the object without a match */
        }
    }
    return NULL;
}

/* Format `d` the way cJSON does, so a spliced document is byte-identical to one that went through
 * a tree: an integral value prints as an integer, otherwise 15 significant digits, falling back to
 * 17 when 15 does not round-trip. Getting this wrong is not cosmetic — the web app re-reads these
 * numbers and a lost digit of precision would silently move the pin. */
static int format_double(double d, char *buf, size_t buflen)
{
    const int n = snprintf(buf, buflen, "%1.15g", d);
    if (n < 0 || (size_t)n >= buflen) return -1;
    double back = 0;
    if (sscanf(buf, "%lg", &back) != 1 || back != d) {
        const int n2 = snprintf(buf, buflen, "%1.17g", d);
        if (n2 < 0 || (size_t)n2 >= buflen) return -1;
    }
    return 0;
}


/* See api_wire.h for why this returns SLICES rather than an edited copy: the render window leaves
 * no room for a second buffer of the document's size, so the answer has to be described as pieces
 * of the caller's existing buffer plus two numbers on its stack. */
int api_location_slices(const char *json, double lat, double lon,
                        char *num_buf, size_t num_buf_len,
                        api_slice_t *out, int max_out)
{
    if (!json || !num_buf || !out || max_out < 1) return -1;

    const size_t len = strlen(json);
    const char *end = json + len;

    /* Two numbers, formatted into the caller's stack buffer, exactly as cJSON would print them. */
    const size_t half = num_buf_len / 2;
    if (half < 24) return -1;                       /* too small for "%1.17g" plus its NUL */
    if (format_double(lat, num_buf, half) != 0) return -1;
    const size_t nlat_len = strlen(num_buf);
    if (format_double(lon, num_buf + half, num_buf_len - half) != 0) return -1;
    const char *nlat = num_buf;
    const char *nlon = num_buf + half;

    const char *loc = member_value(json, "location", end);
    if (!loc || *loc != '{') {
        /* NO location object: insert one after the document's opening brace, which is always valid
         * JSON. The inserted text cannot be a slice of the caller's document, so it is built into
         * the free tail of num_buf — past the second number — to give it the same lifetime. It is
         * formatted into a LOCAL first: snprintf may not read from and write to the same object,
         * and nlat/nlon both live in num_buf. */
        char tmp[96];
        const int tl = snprintf(tmp, sizeof(tmp),
                                "\"location\":{\"latitude\":%s,\"longitude\":%s},", nlat, nlon);
        if (tl < 0 || (size_t)tl >= sizeof(tmp)) return -1;

        const size_t used = half + strlen(nlon) + 1;
        const size_t room = num_buf_len > used ? num_buf_len - used : 0;
        if ((size_t)tl + 1 > room) return -1;
        char *ins = num_buf + used;
        memcpy(ins, tmp, (size_t)tl + 1);

        const char *at = json;
        while (at < end && *at != '{') at++;
        if (at >= end) return -1;
        at++;                                        /* past '{' */

        if (max_out < 3) return -1;
        out[0].p = json;  out[0].len = (size_t)(at - json);
        out[1].p = ins;   out[1].len = (size_t)tl;
        out[2].p = at;    out[2].len = (size_t)(end - at);
        return 3;
    }

    const char *la = member_value(loc, "latitude", end);
    const char *lo = member_value(loc, "longitude", end);
    if (!la || !lo) return -1;

    /* A number runs to the next ',', '}' or whitespace. */
    const char *la_end = la;
    while (la_end < end && *la_end != ',' && *la_end != '}' &&
           *la_end != ' ' && *la_end != '\n' && *la_end != '\t' && *la_end != '\r') la_end++;
    const char *lo_end = lo;
    while (lo_end < end && *lo_end != ',' && *lo_end != '}' &&
           *lo_end != ' ' && *lo_end != '\n' && *lo_end != '\t' && *lo_end != '\r') lo_end++;

    /* Order the two by position: the app writes latitude first, but nothing here assumes it. */
    const char *f_s = la, *f_e = la_end, *s_s = lo, *s_e = lo_end;
    const char *f_n = nlat; size_t f_nl = nlat_len;
    const char *s_n = nlon; size_t s_nl = strlen(nlon);
    if (lo < la) {
        f_s = lo; f_e = lo_end; f_n = nlon; f_nl = strlen(nlon);
        s_s = la; s_e = la_end; s_n = nlat; s_nl = nlat_len;
    }

    if (max_out < 5) return -1;
    int n = 0;
    out[n].p = json;  out[n].len = (size_t)(f_s - json);  n++;
    out[n].p = f_n;   out[n].len = f_nl;                  n++;
    out[n].p = f_e;   out[n].len = (size_t)(s_s - f_e);   n++;
    out[n].p = s_n;   out[n].len = s_nl;                  n++;
    out[n].p = s_e;   out[n].len = (size_t)(end - s_e);   n++;
    return n;
}
