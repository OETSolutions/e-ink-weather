#include "geoloc.h"
#include "cJSON.h"
#include <string.h>

/* The service's own success flag is the ONLY thing that says a reply is usable.
 *
 * This matters more than it looks: ip-api.com answers a rejected query with HTTP 200 and
 * {"status":"fail","message":"..."} — no lat, no lon. A parser that just looked for the
 * coordinate keys would find neither and could plausibly fall back to 0,0, which is a real
 * place in the Gulf of Guinea. The device would then fetch weather for the ocean and show it
 * with no indication anything was wrong. Checking `status` turns that into a clean failure.
 *
 * The coordinate keys are also checked for TYPE, not just presence: `lat` must be a number.
 * A string "41.7" is not accepted, because accepting it would mean this function silently
 * disagreeing with the caller's numeric type. */
int geoloc_parse(const char *json, double *lat, double *lon, char *city, unsigned city_len)
{
    if (!json || !lat || !lon) return -1;
    if (city && city_len) city[0] = '\0';

    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    int rc = -1;

    const cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsString(status) || !status->valuestring ||
        strcmp(status->valuestring, "success") != 0) {
        goto done;
    }

    const cJSON *la = cJSON_GetObjectItemCaseSensitive(root, "lat");
    const cJSON *lo = cJSON_GetObjectItemCaseSensitive(root, "lon");
    if (!cJSON_IsNumber(la) || !cJSON_IsNumber(lo)) goto done;

    /* Range-check as well as parse. The service will not send these, but the value is written
     * to NVS and then straight into an API URL, so a bogus coordinate is worth rejecting at
     * the one place it can still be rejected. */
    if (la->valuedouble < -90.0 || la->valuedouble > 90.0) goto done;
    if (lo->valuedouble < -180.0 || lo->valuedouble > 180.0) goto done;

    *lat = la->valuedouble;
    *lon = lo->valuedouble;

    if (city && city_len) {
        /* A city is decoration — it is shown back to the user, never used to fetch anything —
         * so its absence is not a failure. */
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "city");
        if (cJSON_IsString(c) && c->valuestring) {
            unsigned n = 0;
            /* Copy with explicit bounds rather than strncpy: the truncation has to be
             * deliberate and terminated, and a place name is arbitrary UTF-8 bytes. */
            while (c->valuestring[n] && n + 1 < city_len) {
                city[n] = c->valuestring[n];
                n++;
            }
            city[n] = '\0';
        }
    }

    rc = 0;

done:
    cJSON_Delete(root);
    return rc;
}
