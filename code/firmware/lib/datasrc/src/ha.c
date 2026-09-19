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
