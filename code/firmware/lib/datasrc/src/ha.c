#include "ha.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* HA entity ids are lowercase; the object_id part additionally allows digits and
 * underscores. Requiring at least one '.' keeps a bare word from being mistaken for a
 * full id. This mirrors the rule the web app applies in Task 17. */
int ha_entity_id_valid(const char *id)
{
    if (!id || !*id) return 0;
    const char *dot = strchr(id, '.');
    if (!dot || dot == id) return 0;          /* need a domain before the dot */
    if (dot[1] == '\0') return 0;             /* and an object_id after it */
    for (const char *p = id; *p; p++) {
        char c = *p;
        if (c == '.') continue;
        int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return 0;
    }
    return 1;
}

/* Is `q` a safe entity-id SEARCH substring?
 *
 * THIS IS A TRUST BOUNDARY, not a tidiness check. The search runs server-side as a Jinja
 * template, and the substring is interpolated into it, so an unvalidated `q` is template
 * injection: a single quote would close the string literal and let the rest be Jinja. The
 * entity-id grammar is exactly the safe set (lowercase, digits, '_', '.'), so requiring it both
 * admits every substring a real entity id can contain and leaves no character that means
 * anything to Jinja or to the JSON body the request is carried in. */
int ha_search_query_valid(const char *q)
{
    if (!q || !*q) return 0;
    size_t n = 0;
    for (const char *p = q; *p; p++, n++) {
        if (n >= 48) return 0;               /* longer than any object_id worth matching */
        const char c = *p;
        const int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!ok) return 0;
    }
    return 1;
}

datasrc_status_t ha_classify_state(const char *state, double *out_value)
{
    if (!state) return DATASRC_ERR_UNAVAILABLE;

    /* Skip leading/trailing whitespace: an HTTP body carries a trailing newline, and
     * without this the final entity of every response would read as UNAVAILABLE. */
    while (*state == ' ' || *state == '\t' || *state == '\r' || *state == '\n') state++;
    size_t n = strlen(state);
    while (n > 0 && (state[n - 1] == ' ' || state[n - 1] == '\t' ||
                     state[n - 1] == '\r' || state[n - 1] == '\n')) n--;
    if (n == 0) return DATASRC_ERR_UNAVAILABLE;

    if ((n == 11 && strncmp(state, "unavailable", 11) == 0) ||
        (n == 7  && strncmp(state, "unknown", 7) == 0)) {
        return DATASRC_ERR_UNAVAILABLE;
    }

    /* Copy so the numeric parse sees a NUL-terminated string even when the token was
     * trimmed above. */
    char tmp[64];
    if (n >= sizeof(tmp)) return DATASRC_ERR_UNAVAILABLE;
    memcpy(tmp, state, n); tmp[n] = '\0';

    char *end = NULL;
    double v = strtod(tmp, &end);
    /* strtod accepts leading whitespace, "inf" and "nan" — all of which would become a
     * plausible-looking number on screen. Reject anything that is not a plain number. */
    if (end == tmp || *end != '\0') return DATASRC_ERR_UNAVAILABLE;
    if (!isfinite(v)) return DATASRC_ERR_UNAVAILABLE;
    if (out_value) *out_value = v;
    return DATASRC_OK;
}

int ha_parse_template_line(const char *line, double *out,
                           datasrc_status_t *status, int n_out)
{
    if (!line || !out || !status || n_out <= 0) return 0;

    /* Parse in place over a bounded copy so a response larger than any real layout
     * cannot run past the buffer. */
    char buf[512];
    size_t n = strlen(line);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, line, n); buf[n] = '\0';

    int count = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, "|", &save); tok && count < n_out;
         tok = strtok_r(NULL, "|", &save)) {
        out[count] = 0.0;
        status[count] = ha_classify_state(tok, &out[count]);
        count++;
    }
    /* Missing fields are UNAVAILABLE, not zeros. */
    for (int i = count; i < n_out; i++) {
        status[i] = DATASRC_ERR_UNAVAILABLE;
        out[i] = 0.0;
    }
    return count;
}

int ha_template_add_entity(char *buf, int buflen, int len, const char *entity_id)
{
    if (!buf || buflen <= 0 || len < 0 || len >= buflen) return -1;
    if (!ha_entity_id_valid(entity_id)) return -1;

    char piece[256];
    int k = snprintf(piece, sizeof(piece), "{{ states('%s') }}", entity_id);
    if (k < 0 || k >= (int)sizeof(piece)) return -1;

    int need = len + (len > 0 ? 1 : 0) + k + 1;   /* +1 for the NUL */
    if (need > buflen) return -1;
    if (len > 0) buf[len++] = '|';
    memcpy(&buf[len], piece, (size_t)k);
    len += k;
    buf[len] = '\0';
    return len;
}

int ha_parse_entity_list(const char *body, ha_entity_t *out, int max, int *total)
{
    if (total) *total = 0;
    if (!body || !out || max <= 0) return 0;

    int n = 0;
    int seen = 0;
    const char *p = body;
    while (*p) {
        const char *nl = strchr(p, '\n');
        const size_t linelen = nl ? (size_t)(nl - p) : strlen(p);

        /* 'id|name' — the separators the template emits. A line with no '|' still carries a
         * usable id, so the name falls back to the id rather than dropping the row. */
        const char *bar = memchr(p, '|', linelen);
        const size_t idlen = bar ? (size_t)(bar - p) : linelen;
        const char *name = bar ? bar + 1 : p;
        const size_t namelen = bar ? linelen - idlen - 1 : linelen;

        if (idlen > 0 && idlen < HA_ENTITY_ID_LEN) {
            char id[HA_ENTITY_ID_LEN];
            memcpy(id, p, idlen);
            id[idlen] = '\0';
            /* Filter to real entity ids: the template matches on a substring, and HA's own
             * response formatting can leave a stray token. `seen` counts every row the template
             * produced, so the caller can tell "no matches" from "all matches were clipped". */
            if (ha_entity_id_valid(id)) {
                seen++;
                if (n < max) {
                    snprintf(out[n].id, sizeof(out[n].id), "%s", id);
                    size_t k = namelen < sizeof(out[n].name) - 1 ? namelen : sizeof(out[n].name) - 1;
                    memcpy(out[n].name, name, k);
                    out[n].name[k] = '\0';
                    n++;
                }
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
    if (total) *total = seen;
    return n;
}
