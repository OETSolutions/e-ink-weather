#include "api_values.h"
#include <stdio.h>
#include <string.h>

/* Escape a string for a JSON string literal, same rules as api_status.c. Returns the number of
 * characters that WOULD be written (excluding the terminator), or -1 if it does not fit.
 *
 * A widget's value is arbitrary text: a place name ("Hillside Estates"), an OWM condition word,
 * and — if the device's config or a response ever carries one — a quote or a backslash. An
 * unescaped quote would end the JSON string early and make the whole body unparseable, which
 * for a preview means the app shows nothing at all rather than showing one wrong box. */
static int json_escape(const char *s, char *out, size_t outlen)
{
    size_t o = 0;
    if (!s) s = "";
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        const char *rep = NULL;
        char buf[8];
        switch (*p) {
        case '"':  rep = "\\\""; break;
        case '\\': rep = "\\\\"; break;
        case '\n': rep = "\\n";  break;
        case '\r': rep = "\\r";  break;
        case '\t': rep = "\\t";  break;
        case '\b': rep = "\\b";  break;
        case '\f': rep = "\\f";  break;
        default:
            if (*p < 0x20) {
                snprintf(buf, sizeof(buf), "\\u%04X", (unsigned)*p);
                rep = buf;
            }
            break;
        }
        if (rep) {
            for (const char *q = rep; *q; q++) {
                if (o + 1 >= outlen) return -1;
                out[o++] = *q;
            }
        } else {
            if (o + 1 >= outlen) return -1;
            out[o++] = (char)*p;
        }
    }
    out[o] = '\0';
    return (int)o;
}

int api_values_json(const api_values_t *v, char *out, size_t outlen)
{
    if (!out || outlen == 0) return -1;
    out[0] = '\0';
    if (!v) return -1;
    if (v->count < 0 || v->count > API_VALUES_MAX) return -1;
    if (v->count > 0 && !v->items) return -1;

    size_t o = 0;
    int n;
#define APPEND(...)                                                          \
    do {                                                                     \
        if (o >= outlen) return -1;                                          \
        n = snprintf(out + o, outlen - o, __VA_ARGS__);                      \
        if (n < 0 || (size_t)n >= outlen - o) return -1;                     \
        o += (size_t)n;                                                      \
    } while (0)

    APPEND("{\"page\":%d,\"drawn_page\":%d,\"page_count\":%d,\"resolved_at\":%ld,\"values\":[",
           v->page, v->drawn_page, v->page_count, v->resolved_at);

    for (int i = 0; i < v->count; i++) {
        const api_value_t *it = &v->items[i];
        /* Escaped into fixed scratch sized for the worst case (every byte an escape). A value
         * longer than the field it came from cannot occur — the firmware's own value buffers are
         * API_VALUES_TEXT_LEN — but the bound is explicit so a future change is a compile-time
         * buffer size, not a silent overflow. */
        char esc_id[API_VALUES_ID_LEN * 6 + 8];
        char esc_tx[API_VALUES_TEXT_LEN * 6 + 8];
        if (json_escape(it->id, esc_id, sizeof(esc_id)) < 0) return -1;
        if (json_escape(it->text, esc_tx, sizeof(esc_tx)) < 0) return -1;
        APPEND("%s{\"id\":\"%s\",\"text\":\"%s\",\"has_value\":%d}",
               i ? "," : "", esc_id, esc_tx, it->has_value ? 1 : 0);
    }

    APPEND("]}");
#undef APPEND
    return (int)o;
}
