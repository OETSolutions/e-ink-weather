#include "ha_mqtt.h"
#include "ha.h"
#include <stdio.h>
#include <string.h>

/* The statestream topic is built by splitting the entity id at its dot:
 *     sensor.outdoor_temp  ->  <base>/sensor/outdoor_temp/state
 * HA publishes exactly this for the `state` of every entity, and only this — the
 * attribute topics (`.../attributes/<name>`) sit alongside it under the same prefix,
 * which is why ha_mqtt_entity_from_topic insists on an exact "/state" tail. */

static int append(char *buf, int buflen, int len, const char *s, size_t n)
{
    if (len < 0 || (size_t)len + n + 1 > (size_t)buflen) return -1;
    memcpy(buf + len, s, n);
    buf[len + n] = '\0';
    return len + (int)n;
}

int ha_mqtt_state_topic(char *buf, int buflen, const char *base_topic,
                        const char *entity_id)
{
    if (!buf || buflen <= 0 || !base_topic || !*base_topic) return -1;
    if (!ha_entity_id_valid(entity_id)) return -1;

    const char *dot = strchr(entity_id, '.');

    int len = 0;
    buf[0] = '\0';
    if ((len = append(buf, buflen, len, base_topic, strlen(base_topic))) < 0) return -1;
    if ((len = append(buf, buflen, len, "/", 1)) < 0) return -1;
    if ((len = append(buf, buflen, len, entity_id, (size_t)(dot - entity_id))) < 0) return -1;
    if ((len = append(buf, buflen, len, "/", 1)) < 0) return -1;
    if ((len = append(buf, buflen, len, dot + 1, strlen(dot + 1))) < 0) return -1;
    if ((len = append(buf, buflen, len, "/state", 6)) < 0) return -1;
    return len;
}

int ha_mqtt_entity_from_topic(const char *topic, const char *base_topic,
                              char *out, int outlen)
{
    if (!topic || !base_topic || !*base_topic || !out || outlen <= 0) return -1;
    out[0] = '\0';

    size_t blen = strlen(base_topic);
    if (strncmp(topic, base_topic, blen) != 0) return -1;
    const char *rest = topic + blen;
    if (*rest != '/') return -1;
    rest++;

    /* The tail must be exactly "/state". Anything else — a shorter topic, an
     * attribute topic, a deeper path — is not a state message. */
    static const char kTail[] = "/state";
    size_t rlen = strlen(rest);
    if (rlen <= sizeof(kTail) - 1) return -1;
    if (strcmp(rest + rlen - (sizeof(kTail) - 1), kTail) != 0) return -1;
    size_t body = rlen - (sizeof(kTail) - 1);

    /* body is "<domain>/<object_id>" with no further separators. */
    const char *slash = memchr(rest, '/', body);
    if (!slash || slash == rest) return -1;                 /* need a domain */
    if (slash == rest + body - 1) return -1;                /* need an object_id */
    if (memchr(slash + 1, '/', body - (size_t)(slash - rest) - 1)) return -1;

    size_t dlen = (size_t)(slash - rest);
    size_t olen = body - dlen - 1;
    if ((size_t)outlen < dlen + olen + 2) return -1;
    memcpy(out, rest, dlen);
    out[dlen] = '.';
    memcpy(out + dlen + 1, slash + 1, olen);
    out[dlen + olen + 1] = '\0';

    /* The recovered id must satisfy the same grammar the builder enforces. Together with
     * the structural checks above (exact "/state" tail, exactly one separator) that makes
     * the two directions exact inverses: any topic that reaches here rebuilds to itself,
     * so a wildcard or an unexpected character can never survive as a fake entity. */
    if (!ha_entity_id_valid(out)) {
        out[0] = '\0';
        return -1;
    }
    return (int)(dlen + olen + 1);
}

int ha_mqtt_unquote(const char *payload, char *out, int outlen)
{
    if (!payload || !out || outlen <= 0) return -1;

    size_t n = strlen(payload);
    if (n >= 2 && payload[0] == '"' && payload[n - 1] == '"') {
        payload++;
        n -= 2;
    }

    int w = 0;
    for (size_t i = 0; i < n; i++) {
        char c = payload[i];
        /* Only the escapes JSON actually defines are consumed. Dropping every backslash
         * would corrupt a literal one ("C:\temp" would lose its separator) and would
         * swallow the character after a bare '\' at the end of the payload. */
        if (c == '\\' && i + 1 < n) {
            char e = payload[i + 1];
            switch (e) {
            case '"':  c = '"';  i++; break;
            case '\\': c = '\\'; i++; break;
            case '/':  c = '/';  i++; break;
            case 'n':  c = '\n'; i++; break;
            case 't':  c = '\t'; i++; break;
            case 'r':  c = '\r'; i++; break;
            default:   break;    /* not an escape: keep the backslash verbatim */
            }
        }
        if (w >= outlen - 1) return -1;   /* overflow is an error, never a silent clip */
        out[w++] = c;
    }
    out[w] = '\0';
    return w;
}
