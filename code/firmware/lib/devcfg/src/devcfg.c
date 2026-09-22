#include "devcfg.h"
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) {
        memcpy(p, s, n);
    }
    return p;
}

int devcfg_migrate(int from, int to, const char *in_json, char **out_json)
{
    *out_json = NULL;
    if (!in_json) {
        return -1;
    }
    if (from > to || from < 1) {
        return -2;      /* from the future, or nonsense: refuse */
    }
    if (to != DEVCFG_SCHEMA_VERSION) {
        return -3;      /* we only know how to reach our own version */
    }
    /* v1 is the first version, so there is nothing to rewrite yet. */
    *out_json = dup_str(in_json);
    return *out_json ? 0 : -4;
}

/* See devcfg.h for the contract and the error codes. */
int devcfg_normalize_ha_url(const char *in, char *out, unsigned long cap)
{
    if (!in || !out || cap == 0 || !*in) return -1;

    const int https = (strncmp(in, "https://", 8) == 0);
    if (!https && strncmp(in, "http://", 7) != 0) return -2;

    /* `keep` is the index just past the scheme. The strip below must never go below it: a value
     * of "http://" would otherwise have its own "//" eaten and become "http:", which fails at
     * fetch time with the same generic error the scheme check exists to avoid. */
    const unsigned long keep = https ? 8UL : 7UL;
    if (cap <= keep) return -1;

    unsigned long L = 0;
    while (in[L] && L < cap - 1) { out[L] = in[L]; L++; }
    out[L] = '\0';
    if (in[L] != '\0') return -1;              /* truncated: the caller's buffer is too small */

    while (L > keep && out[L - 1] == '/') out[--L] = '\0';
    if (L <= keep) return -3;                  /* scheme with no host */
    return 0;
}
