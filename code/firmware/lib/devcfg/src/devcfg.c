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
