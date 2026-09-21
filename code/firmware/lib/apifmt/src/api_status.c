#include "api_status.h"
#include <stdio.h>
#include <string.h>

/* Escape a string for a JSON string literal. Returns the number of characters that WOULD
 * be written (excluding the terminator), or -1 if it does not fit.
 *
 * Handles the two things that actually occur in this data: control characters (a truncated
 * firmware error string could contain anything) and quotes/backslashes. An unescaped quote
 * would end the JSON string early and make the whole status body unparseable — which is the
 * worst possible failure for the endpoint you use to diagnose failures. */
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
                /* Any other control char: \u00XX. Invalid JSON otherwise. */
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
    if (o >= outlen) return -1;
    out[o] = '\0';
    return (int)o;
}

int api_status_json(const api_status_t *s, char *out, size_t outlen)
{
    if (!s || !out || outlen == 0) return -1;

    /* Build with snprintf into a cursor so every append is bounded. If any append would
     * overflow, the whole function fails rather than emitting a truncated document. */
    size_t o = 0;
    int n;

#define APPEND(...)                                                          \
    do {                                                                     \
        if (o >= outlen) return -1;                                          \
        n = snprintf(out + o, outlen - o, __VA_ARGS__);                      \
        if (n < 0 || (size_t)n >= outlen - o) return -1;                     \
        o += (size_t)n;                                                      \
    } while (0)

    APPEND("{\"version\":\"");
    /* Escape the version too: it comes from the build and could contain anything. */
    {
        char esc[API_STATUS_VERSION_LEN * 2 + 8];
        if (json_escape(s->version ? s->version : "unknown", esc, sizeof(esc)) < 0) return -1;
        APPEND("%s", esc);
    }
    APPEND("\",\"uptime_s\":%lu", (unsigned long)s->uptime_s);
    APPEND(",\"free_heap\":%lu", (unsigned long)s->free_heap);
    APPEND(",\"free_heap_min\":%lu", (unsigned long)s->free_heap_min);
    APPEND(",\"largest_free_block\":%lu", (unsigned long)s->largest_free_block);
    APPEND(",\"artwork_pages\":%d", s->artwork_pages);
    if (s->has_last_page) APPEND(",\"last_page\":%d", s->last_page);
    else                  APPEND(",\"last_page\":null");

    /* RSSI is omitted rather than reported as 0 when there is no reading: 0 dBm is a real
     * (and implausibly strong) signal, so conflating the two would mislead a remote
     * diagnosis. `has_rssi` distinguishes them. */
    if (s->has_rssi) APPEND(",\"rssi\":%d", s->rssi);
    else             APPEND(",\"rssi\":null");

    if (s->has_vbat) {
        APPEND(",\"vbat\":%.2f", s->vbat);
        /* Numbering matches power_source_t exactly (0 unknown, 1 battery, 2 USB). Decoding
         * with any other order silently swaps mains and battery — the one thing this field
         * exists to distinguish. */
        const char *src = s->vbat_source == 2 ? "usb"
                        : s->vbat_source == 1 ? "battery" : "unknown";
        APPEND(",\"power_source\":\"%s\"", src);
        /* The trend and the sample count go WITH the reading, because a trend without its
         * basis is unfalsifiable: "0.00 V/min" reads as "stable" whether it came from ten
         * samples or from none at all. An operator needs to know which (FR-33). */
        APPEND(",\"vbat_trend\":%.4f,\"vbat_samples\":%d", s->vbat_trend, s->vbat_samples);
    } else {
        APPEND(",\"vbat\":null,\"power_source\":null");
        APPEND(",\"vbat_trend\":null,\"vbat_samples\":%d", s->vbat_samples);
    }

    APPEND(",\"partials_since_full\":%d", s->partials_since_full);
    APPEND(",\"fulls_total\":%d", s->fulls_total);
    APPEND(",\"bitmap_slot\":%d", s->bitmap_slot);

    /* The count is null until a timestamp has been seen, rather than 0: "we have made no
     * calls" and "we cannot count yet" are different states, and reporting the second as the
     * first would hide exactly the runaway-refresh bug this counter exists to catch. */
    if (s->owm_calls_known) APPEND(",\"owm_day_calls\":%d", s->owm_day_calls);
    else                    APPEND(",\"owm_day_calls\":null");

    /* -1 means "never refreshed"; emit null rather than a nonsensical huge age. */
    if (s->last_refresh_age_s < 0) APPEND(",\"last_refresh_age_s\":null");
    else                           APPEND(",\"last_refresh_age_s\":%d", s->last_refresh_age_s);

    APPEND(",\"errors\":[");
    int written = 0;
    for (int i = 0; i < s->error_count && i < API_STATUS_MAX_ERRORS; i++) {
        if (!s->errors[i]) continue;
        char esc[API_STATUS_ERROR_LEN * 2 + 8];
        if (json_escape(s->errors[i], esc, sizeof(esc)) < 0) return -1;
        APPEND("%s\"%s\"", written ? "," : "", esc);
        written++;
    }
    APPEND("]}");
#undef APPEND

    return (int)o;
}

int api_status_push_error(char errors[API_STATUS_MAX_ERRORS][API_STATUS_ERROR_LEN],
                          int *count, const char *msg)
{
    if (!errors || !count) return -1;

    /* Shift older entries down, dropping the oldest when full. Bounded by construction, so
     * an error loop cannot grow this without limit. */
    int n = *count;
    if (n > API_STATUS_MAX_ERRORS) n = API_STATUS_MAX_ERRORS;
    int keep = (n < API_STATUS_MAX_ERRORS) ? n : API_STATUS_MAX_ERRORS - 1;
    for (int i = keep; i > 0; i--) {
        memcpy(errors[i], errors[i - 1], API_STATUS_ERROR_LEN);
    }
    if (msg) {
        strncpy(errors[0], msg, API_STATUS_ERROR_LEN - 1);
        errors[0][API_STATUS_ERROR_LEN - 1] = '\0';
    } else {
        errors[0][0] = '\0';
    }
    *count = keep + 1;
    return 0;
}
