/* Approximate the device's location from its public IP (FR-30 / the setup UI).
 *
 * WHY THIS EXISTS: the portal's latitude/longitude fields used to be blank, so the user had to
 * find their own coordinates and type them. The phone cannot supply them — the Geolocation API
 * is unavailable on an insecure origin, and the setup page is plain HTTP on 192.168.4.1, which
 * both iOS and Android block ("Access to geolocation was blocked over insecure connection").
 * The device, however, is on the real internet once it has joined the user's network, so it can
 * look the position up itself.
 *
 * THIS IS A CITY-LEVEL GUESS, NOT GPS. ip-api.com resolves the public IP to roughly where the
 * ISP hands out addresses — good to a few km in a city, and occasionally badly wrong (carrier
 * NAT, a VPN, a corporate egress hundreds of miles away). It is a default the user can correct,
 * which is the only honest way to present it. For a weather panel that is more than enough: OWM
 * reports the same conditions across a metro area.
 *
 * IT IS ONLY A DEFAULT. This must not overwrite coordinates the user has already set — the
 * whole value of it is filling a blank field, and silently replacing a manually placed pin with
 * a city centre would be worse than leaving it blank. The user's own entry always wins; the
 * other writer of these keys is prov_store_extra_config().
 *
 * HTTP, NOT HTTPS: ip-api.com's free tier serves plain HTTP and refuses HTTPS (verified: 403),
 * so there is no TLS here and no certificate to check. That is acceptable for a location guess
 * — it is not a credential and the request carries no data about the user. A failure leaves the
 * fields blank, exactly as before, which is why nothing here is fatal. */

#include "geo_ip.h"
#include "geoloc.h"
#include "net_http.h"
#include "nvs_keys.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"

#include <stdlib.h>

static const char *TAG = "geo_ip";

/* The free endpoint, trimmed to the fields actually used so the reply stays small — it is
 * about 66 bytes, well inside the buffer below. `status` cannot be dropped: it is the only
 * signal separating an answer from a rejection (both arrive as HTTP 200). */
#define GEO_IP_URL "http://ip-api.com/json/?fields=status,message,lat,lon,city"

/* ip-api rejects plain-HTTP requests carrying a browser-style Referer/Origin, but the ESP-IDF
 * client sends none, so this works as-is. */
esp_err_t geo_ip_lookup(double *lat, double *lon, char *city, size_t city_len)
{
    if (!lat || !lon) return ESP_ERR_INVALID_ARG;

    char *body = malloc(512);
    if (!body) return ESP_ERR_NO_MEM;

    const esp_err_t e = net_http_get_json(GEO_IP_URL, NULL, body, 512);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "location lookup failed: %s", esp_err_to_name(e));
        free(body);
        return e;
    }

    /* The parsing — including the "status":"fail" trap — is host-tested; see
     * lib/geoloc and test/test_geoloc. */
    if (geoloc_parse(body, lat, lon, city, (unsigned)city_len) != 0) {
        ESP_LOGW(TAG, "location lookup returned no usable result");
        free(body);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "approximate location: %.4f, %.4f%s%s", *lat, *lon,
             (city && city[0]) ? " (" : "", (city && city[0]) ? city : "");
    free(body);
    return ESP_OK;
}

int geo_ip_fill_if_unset(void)
{
    nvs_handle_t h;
    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return 0;

    /* Only fill a MISSING location. A stored 0,0 is a real (if unlikely) coordinate and is
     * left alone, because it can only have been written deliberately by the user. */
    double existing = 0;
    size_t llen = sizeof(existing);
    if (nvs_get_blob(h, DEVENV_KEY_LOC_LAT, &existing, &llen) == ESP_OK) {
        nvs_close(h);
        return 0;   /* the user has already set a location; nothing to do */
    }

    double lat = 0, lon = 0;
    char city[48] = {0};
    if (geo_ip_lookup(&lat, &lon, city, sizeof(city)) != ESP_OK) {
        nvs_close(h);
        return 0;
    }

    const bool ok = nvs_set_blob(h, DEVENV_KEY_LOC_LAT, &lat, sizeof(lat)) == ESP_OK &&
                    nvs_set_blob(h, DEVENV_KEY_LOC_LON, &lon, sizeof(lon)) == ESP_OK;
    if (ok) {
        nvs_commit(h);
        ESP_LOGI(TAG, "location was empty; filled it from the public IP");
    }
    nvs_close(h);
    return ok ? 1 : 0;
}
