#include "otarelease.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Copy a JSON string field into a fixed buffer. Returns 0 on success, -1 if absent, not a
 * string, or too long to fit (a truncated version string would compare wrong, so it is
 * refused rather than clipped). */
static int take_str(const cJSON *o, const char *key, char *dst, size_t cap)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsString(v) || !v->valuestring) return -1;
    const size_t n = strlen(v->valuestring);
    if (n == 0 || n >= cap) return -1;
    memcpy(dst, v->valuestring, n + 1);
    return 0;
}

/* A firmware value must be a BARE FILENAME. It is concatenated onto a trusted base URL, so a
 * value like "../other" or "a/b" or an absolute "https://evil" would redirect the download
 * somewhere the base URL never agreed to. The updater only ever expects "firmware.bin", so
 * anything else is refused rather than sanitised. */
static int is_bare_filename(const char *s)
{
    if (!s || !*s) return 0;
    if (strstr(s, "..")) return 0;
    for (const char *p = s; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':') return 0;
        if (!(isalnum((unsigned char)*p) || *p == '.' || *p == '-' || *p == '_')) return 0;
    }
    return 1;
}

int otarelease_parse(const char *json, otarelease_manifest_t *out)
{
    if (!json || !*json || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->size = -1;

    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return -1; }

    int rc = 0;
    if (take_str(root, "version", out->version, sizeof(out->version)) != 0) rc = -1;
    if (rc == 0 && take_str(root, "firmware", out->firmware, sizeof(out->firmware)) != 0) rc = -1;
    if (rc == 0 && !is_bare_filename(out->firmware)) rc = -1;

    if (rc == 0) {
        /* sha256 and size are optional: a release without them still updates, it just cannot be
         * hash-verified. An absent or malformed value leaves the field empty/-1 rather than
         * failing the whole parse, so a future manifest that adds a field cannot brick updates. */
        const cJSON *h = cJSON_GetObjectItemCaseSensitive(root, "sha256");
        if (cJSON_IsString(h) && h->valuestring) {
            const size_t n = strlen(h->valuestring);
            if (n == 64) memcpy(out->sha256, h->valuestring, 65);
        }
        const cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "size");
        if (cJSON_IsNumber(s)) out->size = (long)s->valuedouble;
    }

    cJSON_Delete(root);
    return rc;
}

/* Parse up to 4 dot-separated components. Returns the count parsed, or -1 if any component is
 * non-numeric or the string is empty. A leading 'v' is skipped so "v1.2.3" works. */
static int parse_ver(const char *s, long out[4])
{
    if (!s) return -1;
    if (*s == 'v' || *s == 'V') s++;
    if (!*s) return -1;

    int n = 0;
    const char *p = s;
    while (*p && n < 4) {
        if (!isdigit((unsigned char)*p)) return -1;
        long v = 0;
        while (isdigit((unsigned char)*p)) {
            v = v * 10 + (*p - '0');
            if (v > 100000000L) return -1;      /* absurd component: treat as malformed */
            p++;
        }
        out[n++] = v;
        if (*p == '.') { p++; if (!*p) return -1; }  /* trailing dot is malformed */
        else break;
    }
    if (*p != '\0') return -1;   /* leftover junk (e.g. "-beta") is malformed */
    return n;
}

int otarelease_version_cmp(const char *a, const char *b)
{
    long va[4] = {0, 0, 0, 0}, vb[4] = {0, 0, 0, 0};
    const int na = parse_ver(a, va);
    const int nb = parse_ver(b, vb);
    if (na < 0 || nb < 0) {
        /* Fall back to strcmp on the ORIGINAL strings. A malformed "version" then cannot
         * masquerade as newer than a valid one purely by how the broken parse defaults. */
        const int c = strcmp(a ? a : "", b ? b : "");
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    for (int i = 0; i < 4; i++) {
        if (va[i] < vb[i]) return -1;
        if (va[i] > vb[i]) return 1;
    }
    return 0;
}

bool otarelease_is_newer(const char *current, const char *latest)
{
    return otarelease_version_cmp(current, latest) < 0;
}

int otarelease_firmware_url(const char *base, const otarelease_manifest_t *m,
                            char *out, size_t cap)
{
    if (!base || !*base || !m || !*m->firmware || !out || cap == 0) return -1;
    /* Exactly one slash between the base and the filename, regardless of how the base was
     * written — a double slash is a different URL to some servers. */
    const size_t bl = strlen(base);
    const int needs_slash = bl > 0 && base[bl - 1] != '/';
    const int n = snprintf(out, cap, "%s%s%s", base, needs_slash ? "/" : "", m->firmware);
    if (n < 0 || (size_t)n >= cap) { out[0] = '\0'; return -1; }
    return 0;
}
