/* The setup access point and its configuration page (FR-30.1).
 *
 * This is the browser half of provisioning. The BLE half belongs to wifi_prov_mgr (see
 * prov.h for why the manager cannot do both). The page here collects everything the device
 * needs — WiFi credentials, the OWM API key, the Home Assistant URL and token, and the
 * location — and hands the WiFi part to the manager through prov_submit_credentials() so
 * that both transports share one connect-and-verify path.
 *
 * SECURITY POSTURE: the AP is OPEN and the page has no authentication. That is a deliberate,
 * bounded trade-off, not an oversight. The device has no keyboard, so anything it demands
 * before configuration must come from the very screen the user is trying to reach. The
 * exposure is limited by construction:
 *   - the portal only exists while the device has NO credentials, so a provisioned device
 *     in someone's house never raises it;
 *   - it is up for the minutes it takes to fill in a form, not permanently;
 *   - the WiFi password is never echoed back by the page, and is never logged;
 *   - the only thing an attacker can do in that window is configure the device — which
 *     requires being physically present during first setup, and is recoverable with
 *     prov_forget() or an erase.
 * The alternative (a password printed nowhere, on a device with no display) would lock the
 * user out of their own hardware. */

#include "prov.h"
#include "nvs_keys.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"

static const char *TAG = "prov_ap";

#define PROV_AP_CHANNEL    1
#define PROV_AP_MAX_STA    4
#define PROV_AP_NETIF_KEY  "WIFI_AP_DEF"

void prov_dns_start(void);
void prov_dns_stop(void);

/* Hand credentials to the manager. Defined in prov_softap.c, which owns the manager
 * lifecycle; declared here so this file does not have to know about it. */
esp_err_t prov_submit_credentials(const char *ssid, const char *pass);

static httpd_handle_t s_server;
static esp_netif_t   *s_ap_netif;
static int            s_ap_up;

int prov_ap_is_running(void) { return s_ap_up; }

/* ------------------------------------------------------------------ the page ---- */

/* Deliberately one self-contained document with no external references: the AP has no
 * internet route, so a stylesheet or script from a CDN would simply never load and the page
 * would render unstyled. It is also the reason the form posts to a relative URL. */
static const char PAGE_HTML[] =
"<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Set up e-ink weather display</title><style>"
":root{color-scheme:light dark}"
"body{font:16px/1.5 system-ui,-apple-system,sans-serif;margin:0;padding:1.5rem;max-width:34rem}"
"h1{font-size:1.35rem;margin:0 0 .25rem}"
"p.sub{margin:0 0 1.5rem;opacity:.7;font-size:.9rem}"
"fieldset{border:1px solid #8884;border-radius:.5rem;margin:0 0 1.25rem;padding:1rem}"
"legend{font-weight:600;padding:0 .4rem}"
"label{display:block;margin:.75rem 0 .2rem;font-size:.9rem;font-weight:500}"
"input{width:100%;box-sizing:border-box;padding:.55rem;font-size:1rem;"
"border:1px solid #8886;border-radius:.35rem;background:#8881}"
".row{display:flex;gap:.75rem}.row>div{flex:1}"
"button{width:100%;padding:.8rem;font-size:1rem;font-weight:600;border:0;"
"border-radius:.35rem;background:#2563eb;color:#fff}"
".hint{font-size:.8rem;opacity:.7;margin:.3rem 0 0}"
"#msg{margin:1rem 0 0;padding:.75rem;border-radius:.35rem;display:none}"
"#msg.ok{display:block;background:#16a34a33}"
"#msg.err{display:block;background:#dc262633}"
"</style></head><body>"
"<h1>e-ink weather display</h1>"
"<p class=\"sub\">Enter your network and weather details. They are stored only on the "
"device.</p>"
"<form id=\"f\">"
"<fieldset><legend>WiFi</legend>"
"<label for=\"ssid\">Network name</label>"
"<input id=\"ssid\" name=\"ssid\" required autocapitalize=\"none\" "
"autocorrect=\"off\" spellcheck=\"false\">"
"<label for=\"pass\">Password</label>"
"<input id=\"pass\" name=\"pass\" type=\"password\">"
"<p class=\"hint\">Leave blank for an open network.</p>"
"</fieldset>"
"<fieldset><legend>Weather</legend>"
"<label for=\"owm\">OpenWeatherMap API key</label>"
"<input id=\"owm\" name=\"owm\" autocapitalize=\"none\" autocorrect=\"off\" "
"spellcheck=\"false\">"
"<label>Location</label>"
"<div class=\"row\"><div><input id=\"lat\" name=\"lat\" placeholder=\"Latitude\" "
"inputmode=\"decimal\"><p class=\"hint\">e.g. 40.7608</p></div>"
"<div><input id=\"lon\" name=\"lon\" placeholder=\"Longitude\" "
"inputmode=\"decimal\"><p class=\"hint\">e.g. -111.8910</p></div></div>"
"</fieldset>"
"<fieldset><legend>Home Assistant <span style=\"font-weight:400\">(optional)</span>"
"</legend>"
"<label for=\"haurl\">Base URL</label>"
"<input id=\"haurl\" name=\"haurl\" placeholder=\"http://homeassistant.local:8123\" "
"autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\">"
"<label for=\"hatoken\">Long-lived access token</label>"
"<input id=\"hatoken\" name=\"hatoken\" type=\"password\" autocapitalize=\"none\">"
"</fieldset>"
"<button type=\"submit\">Save and connect</button>"
"<div id=\"msg\"></div></form><script>"
"var f=document.getElementById('f'),m=document.getElementById('msg');"
"function show(t,ok){m.textContent=t;m.className=ok?'ok':'err';}"
"f.addEventListener('submit',function(e){e.preventDefault();"
"var d=new URLSearchParams(new FormData(f));"
"show('Saving\\u2026',true);"
/* fetch is same-origin (relative URL), so no CORS preflight and no absolute host: the page
 * works whether the portal is reached via the AP IP, a redirect, or a captured probe. */
"fetch('save',{method:'POST',body:d}).then(function(r){return r.json()})"
".then(function(j){if(j.ok){show('Saved. The display is connecting to your network\\u2026 "
"it will reboot and the setup network will disappear.',true);}"
"else{show(j.error||'That did not work.',false);}})"
".catch(function(){show('Could not reach the device. Is it still powered?',false);});"
"});"
"</script></body></html>";

static esp_err_t h_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    /* The page must never be cached: it is served during setup, and a phone that cached a
     * stale copy would keep posting to an old form after a firmware change. */
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

/* Percent-decode an application/x-www-form-urlencoded value into `out`. Returns 0 on
 * success. `+` means space; `%XX` is a hex byte. A malformed escape is passed through
 * literally rather than rejected — a password containing a stray '%' must still work. */
static int url_decode(const char *in, size_t in_len, char *out, size_t out_max)
{
    size_t o = 0;
    for (size_t i = 0; i < in_len && o + 1 < out_max; i++) {
        char c = in[i];
        if (c == '+') {
            out[o++] = ' ';
        } else if (c == '%' && i + 2 <= in_len - 1) {
            const int hi = (in[i + 1] >= '0' && in[i + 1] <= '9') ? in[i + 1] - '0'
                         : (in[i + 1] >= 'a' && in[i + 1] <= 'f') ? in[i + 1] - 'a' + 10
                         : (in[i + 1] >= 'A' && in[i + 1] <= 'F') ? in[i + 1] - 'A' + 10 : -1;
            const int lo = (in[i + 2] >= '0' && in[i + 2] <= '9') ? in[i + 2] - '0'
                         : (in[i + 2] >= 'a' && in[i + 2] <= 'f') ? in[i + 2] - 'a' + 10
                         : (in[i + 2] >= 'A' && in[i + 2] <= 'F') ? in[i + 2] - 'A' + 10 : -1;
            if (hi < 0 || lo < 0) {
                out[o++] = c;
            } else {
                out[o++] = (char)((hi << 4) | lo);
                i += 2;
            }
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
    return 0;
}

/* Find `key` in a form body and decode its value into `out`. Returns 1 if present. */
static int form_field(const char *body, const char *key, char *out, size_t out_max)
{
    out[0] = '\0';
    const size_t klen = strlen(key);
    const char *p = body;

    while (p && *p) {
        const char *next = strchr(p, '&');
        const size_t seg = next ? (size_t)(next - p) : strlen(p);
        const char *eq = memchr(p, '=', seg);
        if (eq && (size_t)(eq - p) == klen && strncmp(p, key, klen) == 0) {
            url_decode(eq + 1, seg - (size_t)(eq + 1 - p), out, out_max);
            return 1;
        }
        p = next ? next + 1 : NULL;
    }
    return 0;
}

static void send_json(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_save(httpd_req_t *req)
{
    /* The form is a few hundred bytes; 1 KB is generous and keeps a malformed or hostile
     * Content-Length from allocating anything large on a device with limited heap. */
    if (req->content_len <= 0 || req->content_len > 1024) {
        send_json(req, "413 Payload Too Large", "{\"ok\":false,\"error\":\"Bad request size\"}");
        return ESP_OK;
    }

    char *body = calloc(1, (size_t)req->content_len + 1);
    if (!body) {
        send_json(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"Out of memory\"}");
        return ESP_OK;
    }

    int got = 0;
    while (got < req->content_len) {
        const int n = httpd_req_recv(req, body + got, (size_t)req->content_len - (size_t)got);
        if (n <= 0) {
            if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(body);
            send_json(req, "400 Bad Request", "{\"ok\":false,\"error\":\"Incomplete body\"}");
            return ESP_OK;
        }
        got += n;
    }

    char ssid[64] = {0}, pass[128] = {0};
    char owm[128] = {0}, haurl[192] = {0}, hatoken[256] = {0};
    char lat_s[32] = {0}, lon_s[32] = {0};

    form_field(body, "ssid", ssid, sizeof(ssid));
    form_field(body, "pass", pass, sizeof(pass));
    form_field(body, "owm", owm, sizeof(owm));
    form_field(body, "haurl", haurl, sizeof(haurl));
    form_field(body, "hatoken", hatoken, sizeof(hatoken));
    form_field(body, "lat", lat_s, sizeof(lat_s));
    form_field(body, "lon", lon_s, sizeof(lon_s));
    free(body);

    if (ssid[0] == '\0') {
        send_json(req, "400 Bad Request",
                  "{\"ok\":false,\"error\":\"Network name is required\"}");
        return ESP_OK;
    }

    /* Location is optional and all-or-nothing: one coordinate without the other is not a
     * place, and storing half of it would put the device at a silently wrong position. */
    int have_loc = 0;
    double lat = 0, lon = 0;
    if (lat_s[0] && lon_s[0]) {
        char *end = NULL;
        lat = strtod(lat_s, &end);
        if (!end || *end != '\0' || lat < -90.0 || lat > 90.0) {
            send_json(req, "400 Bad Request",
                      "{\"ok\":false,\"error\":\"Latitude must be between -90 and 90\"}");
            return ESP_OK;
        }
        lon = strtod(lon_s, &end);
        if (!end || *end != '\0' || lon < -180.0 || lon > 180.0) {
            send_json(req, "400 Bad Request",
                      "{\"ok\":false,\"error\":\"Longitude must be between -180 and 180\"}");
            return ESP_OK;
        }
        have_loc = 1;
    }

    /* Store the extra fields first. If the WiFi handover below fails the user retries the
     * form, and re-storing these is harmless — whereas losing them because the handover
     * failed would be a silent partial configuration. */
    prov_store_extra_config(owm, haurl, hatoken, lat, lon, have_loc);

    /* Hand the WiFi credentials to the manager. It applies them, connects, and on success
     * tears the session down — which is what makes prov_run_if_unconfigured() return and the
     * device reboot. The reply is sent BEFORE that can happen, so the browser gets its
     * confirmation rather than a dropped connection. */
    const esp_err_t e = prov_submit_credentials(ssid, pass);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "manager rejected the credentials: %s", esp_err_to_name(e));
        send_json(req, "500 Internal Server Error",
                  "{\"ok\":false,\"error\":\"The device could not apply those settings\"}");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "credentials submitted from the captive portal");
    send_json(req, "200 OK", "{\"ok\":true}");
    return ESP_OK;
}

/* Anything not found is redirected to the page. This is the other half of the captive-portal
 * illusion: the OS's connectivity probe requests a specific URL, and answering with a
 * redirect to our page is what it detects. A plain 404 would leave the network looking
 * merely broken, and the user would have to type the address themselves.
 *
 * The 303 and the short body are both load-bearing: iOS only treats the network as captive
 * if the response has content, so a bare redirect is not enough. */
static esp_err_t h_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirecting to the setup page", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ------------------------------------------------------------------ the AP ---- */

static esp_err_t start_httpd(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    /* The portal serves a handful of small requests from one or two phones; the default
     * socket count is already more than enough, but the stack is raised because the handler
     * builds a ~1 KB body buffer and calls into NVS. */
    cfg.stack_size = 6144;
    cfg.max_open_sockets = 5;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t e = httpd_start(&s_server, &cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(e));
        s_server = NULL;
        return e;
    }

    static const httpd_uri_t uris[] = {
        { .uri = "/",     .method = HTTP_GET,  .handler = h_page },
        { .uri = "/save", .method = HTTP_POST, .handler = h_save },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        e = httpd_register_uri_handler(s_server, &uris[i]);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed: %s", uris[i].uri, esp_err_to_name(e));
            return e;
        }
    }
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, h_404);
    return ESP_OK;
}

esp_err_t prov_ap_start(const char *ap_ssid)
{
    if (s_ap_up) return ESP_OK;
    if (!ap_ssid || !*ap_ssid) return ESP_ERR_INVALID_ARG;

    /* The AP netif must exist before the interface is configured. Created once, guarded,
     * because esp_netif_create_default_wifi_ap() has no lookup of its own — it calls
     * esp_netif_new() unconditionally (esp_wifi/src/wifi_default.c), so a second call builds
     * a SECOND interface instead of returning the existing one. The retry path after a bad
     * password would then be attaching the AP config to a different interface than the one
     * already up, and the portal would silently stop working. */
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (!s_ap_netif) {
            ESP_LOGE(TAG, "cannot create the AP interface");
            return ESP_FAIL;
        }
    }

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = (uint8_t)strlen(ap_ssid);
    ap.ap.channel = PROV_AP_CHANNEL;
    ap.ap.max_connection = PROV_AP_MAX_STA;
    /* OPEN, deliberately — see the security note at the top of this file. The WiFi password
     * the user is about to type is protected by the HTTPS-free page being on a network that
     * only exists for the next few minutes, not by the AP's own key. */
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.pmf_cfg.required = false;

    /* APSTA, not AP: the manager is simultaneously running the station to verify the
     * credentials it is given, so switching to AP-only here would break the verification
     * that is the whole point of the shared handover path. */
    esp_err_t e = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (e != ESP_OK) { ESP_LOGE(TAG, "set_mode failed: %s", esp_err_to_name(e)); return e; }
    e = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (e != ESP_OK) { ESP_LOGE(TAG, "set_config failed: %s", esp_err_to_name(e)); return e; }

    /* The interface is already started by the manager (it calls esp_wifi_start() during
     * start_provisioning), so this returns ESP_ERR_WIFI_STATE when it is. That is success
     * for our purposes, not a failure to report. */
    e = esp_wifi_start();
    if (e != ESP_OK && e != ESP_ERR_WIFI_STATE) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(e));
        return e;
    }

    e = start_httpd();
    if (e != ESP_OK) return e;

    /* The DNS catch-all is what turns "an AP with a page" into a captive portal the phone
     * opens by itself. Started last: if it fails, the page is still reachable by IP. */
    prov_dns_start();

    s_ap_up = 1;

    char ip[16] = "192.168.4.1";
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_ap_netif, &info) == ESP_OK) {
        inet_ntoa_r(info.ip.addr, ip, sizeof(ip));
    }
    ESP_LOGI(TAG, "=== setup AP \"%s\" is up: connect and open http://%s ===", ap_ssid, ip);
    return ESP_OK;
}

void prov_ap_stop(void)
{
    if (!s_ap_up && !s_server) return;

    prov_dns_stop();

    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    /* Stop broadcasting. Without this the AP stays up with no page behind it, which is the
     * worst of both worlds: a network that invites a connection and then does nothing.
     *
     * Switched back to STA rather than stopped outright, because the station is what the
     * manager is using to verify the credentials that were just supplied — dropping to
     * AP-only here would break the very connection this teardown is meant to allow. */
    esp_wifi_set_mode(WIFI_MODE_STA);

    /* The netif is intentionally NOT destroyed: esp_netif_create_default_wifi_ap() can only
     * be called once per app lifetime, and a later prov_ap_start() (the retry path after a
     * bad password) would fail to create it a second time and take the whole portal down with
     * it. It holds no socket and no memory worth reclaiming — the HTTP server above owned
     * those. */
    s_ap_up = 0;
    ESP_LOGI(TAG, "setup AP stopped");
}
