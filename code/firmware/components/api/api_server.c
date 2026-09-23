#include "api.h"
#include "api_internal.h"
#include "api_ota.h"
#include "api_status.h"
#include "api_values.h"
#include "owm_counter.h"
#include "net_wifi.h"
#include "net_http.h"
#include "ha.h"
#include "cfg_store.h"
#include "devcfg.h"
#include "layout_model.h"
#include "power.h"
#include "nvs_keys.h"
#include "prov.h"
#include "webui.h"
#include "apiauth.h"
#include "nvs.h"
#include "bitmap_upload.h"
#include "bitmap_slot.h"
#include "api_wire.h"
#include "api_store.h"
#include "api_internal.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* Needed for the RTC_DATA_ATTR voltage history (FR-33) — see its definition below. */
#include "vbat_history.h"

static const char *TAG = "api";

#define API_MAX_BODY    (API_CONFIG_MAX_LEN + 1024)

/* ------------------------------------------------------------------------ runtime state */

static httpd_handle_t s_server;

static int      s_partials_since_full;
static int      s_fulls_total;
static int64_t  s_last_refresh_us;      /* 0 = never */
/* The page the last refresh rendered (FR-15). With rotation, "which page is on the
 * glass" is not derivable from anything else the API reports. */
static int      s_last_page;
static int      s_have_last_page;
static int      s_pending_full_refresh;
/* A one-shot page override (FR-15). -1 = none, so page 0 is a real value and is not confused
 * with "no override". */
static int      s_pending_page = -1;
static uint32_t s_free_heap_min;

/* Task to notify when a refresh is requested (see api_set_refresh_task). */
static void    *s_refresh_task;

static char s_errors[API_STATUS_MAX_ERRORS][API_STATUS_ERROR_LEN];
static int  s_error_count;

/* Boot-time battery reading, cached because ADC2 cannot be read with WiFi up (HW-3). */
static double s_vbat;

/* The battery voltage history (FR-33), retained across deep sleep.
 *
 * RTC_DATA_ATTR because it is the ONLY memory that survives esp_deep_sleep_start(), and the
 * battery case — where a failing pack can only be seen as a trend across wakes — is exactly the
 * case FR-33 is for. It is deliberately not in NVS: a flash write per wake would cost the very
 * energy budget this history exists to help diagnose, and a value that is diagnostic only does
 * not warrant wearing the flash for.
 *
 * The struct's own validity rule is what makes retained-but-arbitrary RTC RAM safe to read. */
RTC_DATA_ATTR static vbat_history_t s_vbat_hist;

/* OWM daily call count (spec §3.4). The struct's own have_day flag is what makes "unknown"
 * distinguishable from "zero calls so far". */
static owm_counter_t s_owm;
static int    s_vbat_source;
static int    s_has_vbat;

/* Guards only the small counters and the pending-refresh flag, which are written by the
 * HTTP server task and read by the refresh path. Deliberately NOT held across rendering or
 * flash writes — see api_request_full_refresh(). */
static SemaphoreHandle_t s_lock;

static void lock(void)   { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

void api_reset_cycle_counters(void)
{
    lock();
    s_partials_since_full = 0;
    s_fulls_total = 0;
    s_last_refresh_us = 0;
    s_error_count = 0;
    s_free_heap_min = (uint32_t)esp_get_free_heap_size();
    unlock();
}

void api_record_page(int page)
{
    lock();
    s_last_page = page;
    s_have_last_page = 1;
    unlock();
}

/* The last resolved values, for GET /api/values (FR-27). Static rather than allocated: it is a
 * fixed 24 entries and the endpoint must be answerable without a malloc that could fail and
 * turn a preview into an error. */
static struct {
    int  count;
    int  page;
    int  page_count;
    char ids[API_VALUES_MAX][API_VALUES_ID_LEN];
    char texts[API_VALUES_MAX][API_VALUES_TEXT_LEN];
    int  has_value[API_VALUES_MAX];
    long resolved_at;
    int  valid;
} s_values;

void api_record_values(const char (*ids)[24], const char (*texts)[40],
                       const int *has_value, int count, int page, int page_count)
{
    if (!ids || !texts || !has_value || count < 0) return;

    lock();
    const int n = count > API_VALUES_MAX ? API_VALUES_MAX : count;
    for (int i = 0; i < n; i++) {
        strncpy(s_values.ids[i], ids[i], API_VALUES_ID_LEN - 1);
        s_values.ids[i][API_VALUES_ID_LEN - 1] = '\0';
        strncpy(s_values.texts[i], texts[i], API_VALUES_TEXT_LEN - 1);
        s_values.texts[i][API_VALUES_TEXT_LEN - 1] = '\0';
        s_values.has_value[i] = has_value[i] ? 1 : 0;
    }
    s_values.count = n;
    s_values.page = page;
    s_values.page_count = page_count;
    s_values.resolved_at = (long)(esp_timer_get_time() / 1000000LL);
    s_values.valid = 1;
    unlock();
}

void api_record_refresh(int was_full)
{
    lock();
    if (was_full) {
        s_fulls_total++;
        s_partials_since_full = 0;
    } else {
        s_partials_since_full++;
    }
    s_last_refresh_us = esp_timer_get_time();
    unlock();
}

void api_set_refresh_task(void *task)
{
    lock();
    s_refresh_task = task;
    unlock();
}

void api_request_full_refresh(void)
{
    lock();
    s_pending_full_refresh = 1;
    void *t = s_refresh_task;
    unlock();
    /* Notified OUTSIDE the lock: the woken task immediately calls back into this file
     * (api_take_full_refresh), which takes the same lock, so notifying while holding it
     * would deadlock until the notification timeout expired. */
    if (t) xTaskNotifyGive((TaskHandle_t)t);
}

int api_take_full_refresh(void)
{
    lock();
    const int was = s_pending_full_refresh;
    s_pending_full_refresh = 0;
    unlock();
    return was;
}

void api_request_page(int page)
{
    if (page < 0) return;
    lock();
    s_pending_page = page;
    /* A page change is a different background, so it must be a FULL refresh and must not be
     * downgraded by refresh_decide()'s partial budget. */
    s_pending_full_refresh = 1;
    void *t = s_refresh_task;
    unlock();
    if (t) xTaskNotifyGive((TaskHandle_t)t);
}

int api_take_page(void)
{
    lock();
    const int was = s_pending_page;
    unlock();
    return was;
}

void api_clear_page(int page)
{
    lock();
    /* CLEAR ONLY IF THE REQUEST IS STILL THE ONE THAT WAS HONOURED. A render takes seconds, and a
     * second "show this page" arriving during it overwrites s_pending_page — clearing
     * unconditionally would then DISCARD that newer request and leave the user's second click with
     * no effect. Comparing against the honoured page leaves a newer request pending for the next
     * tick, which is what the user asked for. */
    if (s_pending_page == page) s_pending_page = -1;
    unlock();
}

int api_live_bitmap_slot(void)
{
    return bitmap_store_live_slot();
}

void api_note_error(const char *msg)
{
    if (!msg) return;
    lock();
    api_status_push_error(s_errors, &s_error_count, msg);
    unlock();
}

void api_note_vbat(double volts, int source)
{
    lock();
    s_vbat = volts;
    s_vbat_source = source;
    s_has_vbat = 1;
    /* Record the sample into the retained history. Millivolts, because the whole range of
     * interest for a Li-ion pack fits in a small integer and a float in RTC memory would be
     * both bigger and subject to whatever the previous wake left there. */
    if (isfinite(volts) && volts > 0.0) {
        vbat_history_push(&s_vbat_hist, (int32_t)(volts * 1000.0 + 0.5));
    }
    unlock();
}

int api_vbat_history_count(void)
{
    return vbat_history_count(&s_vbat_hist);
}

/* The trend in volts per minute across the recorded history, or 0.0 when there is too little of
 * it. `span_minutes` is supplied by the caller from the configured wake interval times the
 * sample count: the device has no clock that survives deep sleep, so the ring stores volts only
 * and the elapsed span is reconstructed rather than timestamped. */
double api_vbat_trend(double span_minutes)
{
    return vbat_history_trend(&s_vbat_hist, span_minutes);
}

/* Whether RTC memory says this is a wake rather than a cold boot is not reported: the retained
 * history's sample count already conveys it (0 samples means no wake has recorded a reading),
 * and a second field saying the same thing is one more thing to keep consistent. */

/* Called once per boot, before anything else touches the history. On a cold boot (or after a
 * firmware update, which resets RTC memory) the retained bytes fail validation and the history
 * is re-initialised; on a wake it is kept, which is the whole point. */
void api_vbat_history_boot(void)
{
    const int was_valid = vbat_history_valid(&s_vbat_hist);
    const int had = vbat_history_count(&s_vbat_hist);
    if (!was_valid) vbat_history_init(&s_vbat_hist);
    /* Logged unconditionally: whether the retained history SURVIVED this boot is the one fact
     * this function exists to establish, and it is not observable from anywhere else. A cold
     * boot and a deep-sleep wake look identical in every other log line. */
    ESP_LOGI(TAG, "vbat history: %s (%d samples retained)",
             was_valid ? "kept from RTC" : "cold; starting empty", had);
}

int api_partials_since_full(void)
{
    lock();
    const int n = s_partials_since_full;
    unlock();
    return n;
}

/* ---- OWM daily call counter (spec §3.4) -------------------------------------------------
 *
 * The spec requires the firmware to "count and cap daily calls and surface the count in
 * /api/status, so a bug cannot silently burn the quota". The free tier is 1,000 calls/day
 * against a 10-15 minute refresh (~96-144 calls/day), so the cap should never be reached in
 * normal operation — which is exactly why it is worth having: it is the tripwire for a bug
 * that has started refreshing in a loop, and nothing else would make that visible off-device.
 *
 * THE ARITHMETIC ITSELF LIVES IN lib/owmcount, host-tested (test/test_owmcount), because the
 * interesting cases are all calendar arithmetic that the bench cannot reproduce without
 * waiting a day. This file only holds the state and exposes it to the HTTP handler.
 *
 * There is no wall clock on this device: esp_timer_get_time() is documented as "time since
 * boot" and resets on every wake, so the day index comes from the timestamp already carried
 * in each OWM response. See api_owm_note_call(). */
#define API_OWM_DAILY_CAP 1000

int api_owm_should_call(void)
{
    lock();
    const int allowed = owm_counter_should_call(&s_owm, API_OWM_DAILY_CAP);
    unlock();
    return allowed;
}

void api_owm_note_call(long now_unix, int did_call)
{
    if (!did_call) return;
    lock();
    owm_counter_note_call(&s_owm, now_unix, API_OWM_DAILY_CAP);
    unlock();
}

int api_partial_limit(void)
{
    char *json = NULL;
    if (cfg_store_get(cfg_store_nvs(), &json) != 0) return 5;   /* documented default */

    layout_config_t cfg;
    const int ok = layout_config_parse(json, &cfg) == 0;
    free(json);
    return ok ? cfg.partial_refresh_limit : 5;
}

/* The configured update interval, in seconds, for the always-on serve loop.
 *
 * On battery this number is consumed by the deep-sleep timer in the boot path and nothing else
 * needs it. On mains the device never sleeps, and without this the ONLY thing that ever moved
 * the panel was POST /api/refresh — so a plugged-in weather display sat on whatever it fetched
 * at boot and never showed a new reading. That is the main deployment (the user runs it plugged
 * in), so the serve loop needs the same interval the sleep path uses. */
int api_update_seconds(void)
{
    char *json = NULL;
    if (cfg_store_get(cfg_store_nvs(), &json) != 0) return 900;

    layout_config_t cfg;
    const int ok = layout_config_parse(json, &cfg) == 0;
    free(json);
    /* Refuse a value the parser rejected or one below the documented 30 s floor: a bad read
     * must not turn the serve loop into a busy refresh loop. */
    if (!ok || cfg.update_seconds < 30) return 900;
    return cfg.update_seconds;
}

/* -------------------------------------------------------------------- small helpers -- */

esp_err_t api_send_json(httpd_req_t *req, const char *body, const char *status)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    /* The device's state changes on every request; a cached /api/status is worse than
     * useless when diagnosing a fault. */
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const size_t n = strlen(body);
    /* Send the WHOLE body in one call. httpd_resp_send() treats a length of -1 as
     * "strlen", but passing an explicit length avoids the body being re-scanned and, more
     * importantly, keeps this a single write so the response is not interleaved with
     * anything else on the socket. */
    return httpd_resp_send(req, body, (ssize_t)n);
}

esp_err_t api_send_err(httpd_req_t *req, const char *status, const char *msg)
{
    char body[192];
    /* Escape via cJSON, not sprintf. The messages reaching here include parser output, and
     * a single '"' or '\\' in one would produce an unparseable body — which the web app
     * shows as a device fault rather than as the error it actually is. */
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(req);
    cJSON_AddStringToObject(root, "error", msg ? msg : "error");
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(req);

    snprintf(body, sizeof(body), "%s", json);
    free(json);
    return api_send_json(req, body, status);
}

/* Read the request body into a caller-supplied buffer, NUL-terminated. Returns the body
 * length, or -1 on failure (and sends the error response itself). */
int api_read_body(httpd_req_t *req, char *buf, size_t cap)
{
    if ((size_t)req->content_len >= cap) {
        api_send_err(req, "413 Payload Too Large", "body too large");
        return -1;
    }
    size_t got = 0;
    while (got < (size_t)req->content_len) {
        const int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;   /* partial read; keep going */
        if (r <= 0) {
            api_send_err(req, "400 Bad Request", "short read");
            return -1;
        }
        got += (size_t)r;
    }
    buf[got] = '\0';
    return (int)got;
}

/* ------------------------------------------------------- optional API authentication -- */

/* The token, cached from NVS at server start so the check does not open NVS on every
 * request. `s_auth_token[0] == '\0'` means "no token set", which apiauth_required() treats as
 * "nothing to check" — see that function for why that is the safe state rather than a
 * fail-closed one. */
static char s_auth_token[APIAUTH_TOKEN_MAX + 1];
static int  s_auth_enabled;

void api_auth_reload(void)
{
    s_auth_token[0] = '\0';
    s_auth_enabled = 0;

    nvs_handle_t h;
    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;

    uint8_t on = 0;
    if (nvs_get_u8(h, DEVENV_KEY_API_AUTH_ENABLED, &on) == ESP_OK) s_auth_enabled = on ? 1 : 0;

    size_t n = sizeof(s_auth_token);
    if (nvs_get_str(h, DEVENV_KEY_API_TOKEN, s_auth_token, &n) != ESP_OK) s_auth_token[0] = '\0';
    nvs_close(h);

    /* A token that would not survive the header parse must not count as protection — the
     * owner would believe the device was locked while apiauth_required() fell back to "no
     * token set". Refusing it here keeps the stored state and the enforced state identical. */
    if (!apiauth_token_is_usable(s_auth_token)) s_auth_token[0] = '\0';

    ESP_LOGI(TAG, "api auth: %s", apiauth_required(s_auth_enabled, s_auth_token)
             ? "enabled" : "disabled");
}

int api_auth_enabled(void)
{
    return apiauth_required(s_auth_enabled, s_auth_token);
}

/* The stored token, for the config app to display. Never sent to an unauthenticated caller:
 * the handler that reports it is itself behind the check when auth is on. */
const char *api_auth_token(void)
{
    return s_auth_token;
}

/* Gate a mutating handler. Returns 0 when the request may proceed; otherwise it has already
 * sent the 401 and the caller must return immediately.
 *
 * WHY THE RESPONSE SETS WWW-Authenticate: a bare 401 is what makes a browser show its own
 * credential dialog and lose the app's own error message. Sending the header is correct
 * per RFC 7235, and the web app reads the 401 body to prompt for the token itself.
 *
 * WHY GET IS NOT GATED: the config app is served BY this device and must be able to read
 * /api/config to draw itself before the user has entered a token. Reads only ever expose
 * what the device already shows on the glass, so the exposure is bounded; the mutating verbs
 * are where the risk lives, and POST /api/ota is the one that matters most. */
int api_auth_gate(httpd_req_t *req)
{
    if (!apiauth_required(s_auth_enabled, s_auth_token)) return 0;

    /* A 2 KB buffer, sized for the longest header a client could send. A header longer than
     * this cannot be the token (bounded by APIAUTH_TOKEN_MAX), so a truncation would only
     * reject a request that could not have authenticated anyway. */
    char hdr[256];
    const size_t got = httpd_req_get_hdr_value_len(req, "Authorization");
    if (got > 0 && got < sizeof(hdr)) {
        if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) == ESP_OK) {
            if (apiauth_header_matches(hdr, s_auth_token)) return 0;
        }
        /* Deliberately not logged in detail: an auth log is a place a token can leak into,
         * and this device's log is readable over the same LAN. */
        ESP_LOGW(TAG, "auth: rejected %s %s", req->method == HTTP_PUT ? "PUT" : "POST", req->uri);
    } else {
        ESP_LOGW(TAG, "auth: missing Authorization header for %s %s",
                 req->method == HTTP_PUT ? "PUT" : "POST", req->uri);
    }

    httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer realm=\"eink-weather\"");
    api_send_err(req, "401 Unauthorized", "auth required");
    return 1;
}

/* ------------------------------------------------------------------ GET /api/status -- */

static esp_err_t h_status(httpd_req_t *req)
{
    const char *ver = "0.1.0";
    char version[API_STATUS_VERSION_LEN];
    snprintf(version, sizeof(version), "%s", ver);

    lock();
    api_status_t s;
    memset(&s, 0, sizeof(s));
    s.version = version;
    s.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000LL);
    s.free_heap = (uint32_t)esp_get_free_heap_size();
    /* The largest contiguous block, not the total. Both the TLS handshake (~20 KB) and every
     * framebuffer (78,200 B) need ONE contiguous allocation, so a device with plenty of total
     * free heap can still be unable to fetch or draw — and the total alone reports it healthy. */
    s.largest_free_block = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    s.artwork_pages = artwork_store_page_count();
    s.last_page = s_last_page;
    s.has_last_page = s_have_last_page;
    const uint32_t now_heap = s.free_heap;
    if (now_heap < s_free_heap_min) s_free_heap_min = now_heap;
    s.free_heap_min = s_free_heap_min;

    /* WiFi is normally torn down between refreshes (NFR-3), so "no RSSI" is the common
     * case, not an error — has_rssi is what distinguishes it from a real 0 dBm. */
    if (net_wifi_connected()) {
        s.rssi = net_wifi_rssi();
        s.has_rssi = 1;
    } else {
        s.rssi = 0;
        s.has_rssi = 0;
    }

    /* From the boot-time cache, never a live ADC2 read — see api_note_vbat(). */
    s.vbat = s_vbat;
    s.vbat_source = s_vbat_source;
    s.has_vbat = s_has_vbat;

    /* The voltage trend, and the sample count it is based on. The span is reconstructed from the
     * configured wake interval times the sample count, because nothing on this device keeps time
     * across deep sleep — see api_vbat_trend(). */
    s.vbat_samples = api_vbat_history_count();
    {
        layout_config_t c;
        memset(&c, 0, sizeof(c));
        int wake_s = 900;
        {
            char *json = NULL;
            if (cfg_store_get(cfg_store_nvs(), &json) == 0 && json) {
                if (layout_config_parse(json, &c) == 0 && c.update_seconds > 0) {
                    wake_s = c.update_seconds;
                }
                free(json);
            }
        }
        s.vbat_trend = api_vbat_trend((double)s.vbat_samples * (double)wake_s / 60.0);
    }

    s.partials_since_full = s_partials_since_full;
    s.fulls_total = s_fulls_total;
    s.bitmap_slot = api_live_bitmap_slot();
    s.owm_day_calls = owm_counter_calls(&s_owm);
    s.owm_calls_known = owm_counter_known(&s_owm);
    s.last_refresh_age_s = s_last_refresh_us == 0
        ? -1
        : (int)((esp_timer_get_time() - s_last_refresh_us) / 1000000LL);
    for (int i = 0; i < s_error_count; i++) s.errors[i] = s_errors[i];
    s.error_count = s_error_count;
    unlock();

    char *out = malloc(1024);
    if (!out) return api_send_err(req, "500 Internal Server Error", "oom");
    const int n = api_status_json(&s, out, 1024);
    if (n < 0) {
        free(out);
        return api_send_err(req, "500 Internal Server Error", "status buffer too small");
    }
    const esp_err_t e = api_send_json(req, out, "200 OK");
    free(out);
    return e;
}

/* ---------------------------------------------------------------- GET /api/values ---- */

/* The values the last refresh resolved, per widget id (FR-27).
 *
 * WHY THE DEVICE SERVES THIS AND THE APP DOES NOT COMPUTE IT: FR-27 wants the preview rendered
 * with real fetched data. The device is the only party that HAS it — it holds the OWM key and
 * the HA token, it already fetched the documents, and value_format_widget() is the exact code
 * that decides the string on the glass. A TypeScript reimplementation would drift on the subtle
 * parts (fallback, decimals default, alert replacement, HA slot indexing), and in the embedded
 * case the browser has neither the credentials nor, on the setup network, any internet.
 *
 * BEFORE THE FIRST REFRESH there is nothing to report, and that is a 200 with an empty list
 * rather than a 404: the editor's question is "what will the panel show", and "the device has
 * not drawn yet" is a real answer to it. A 404 would read as a missing endpoint. */
static esp_err_t h_values(httpd_req_t *req)
{
    static char out[2048];

    api_value_t items[API_VALUES_MAX];
    api_values_t v;
    memset(&v, 0, sizeof(v));
    memset(items, 0, sizeof(items));

    lock();
    const int n = s_values.count;
    for (int i = 0; i < n; i++) {
        items[i].id = s_values.ids[i];
        items[i].text = s_values.texts[i];
        items[i].has_value = s_values.has_value[i];
    }
    v.items = items;
    v.count = n;
    v.page = s_values.page;
    v.page_count = s_values.page_count;
    v.resolved_at = s_values.resolved_at;
    unlock();

    const int len = api_values_json(&v, out, sizeof(out));
    if (len < 0) {
        return api_send_err(req, "500 Internal Server Error", "values buffer too small");
    }
    return api_send_json(req, out, "200 OK");
}

/* ------------------------------------------------------------------ GET/PUT /config -- */

/* Write the location from a config document into the NVS blobs the fetch path reads.
 *
 * WHY THIS IS NEEDED AT ALL: the document's `location` field and the NVS lat/lon blobs were
 * two disconnected stores. The document round-trips through save/load and looks authoritative
 * in the web app, while app_refresh.c reads ONLY the blobs. So without this, a pin dragged on
 * the map picker would be saved, echoed back to the user, and then silently IGNORED by the
 * device — a UI confirming a change that has no effect. FR-24 makes the map the place the
 * position is chosen; the document is the input and the blobs are what the firmware acts on.
 *
 * An absent or malformed location leaves the stored one ALONE rather than clearing it, and
 * never fails the request: the layout itself has already been validated and stored, and
 * rejecting the whole PUT over an optional field would be surprising. The user's ability to
 * remove a location is the factory reset, not a blank field in a document that also carries
 * the API key. */
static void note_config_location(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    const cJSON *loc = cJSON_GetObjectItemCaseSensitive(root, "location");
    const cJSON *la = cJSON_IsObject(loc) ? cJSON_GetObjectItemCaseSensitive(loc, "latitude") : NULL;
    const cJSON *lo = cJSON_IsObject(loc) ? cJSON_GetObjectItemCaseSensitive(loc, "longitude") : NULL;

    /* (0,0) MEANS "NO LOCATION CHOSEN" IN THE APP, and it must not be stored as a real one.
     *
     * The app's default document ships exactly this (`emptyConfig()` and the default layout both
     * start at latitude 0, longitude 0) — that is the Gulf of Guinea, and no one puts a pin
     * there on purpose. But the config PUT runs on every Save, so a user who saves before
     * placing a pin would persist 0,0 as a DELIBERATE location — and geo_ip_fill_if_unset()
     * treats any stored coordinate as the user's own and never overwrites it. The device would
     * then be pinned to 0,0 for good, the autofill that exists to learn the real city would be
     * disabled, and the panel would read "Globe" (OWM's name for those coordinates) with no
     * indication anything was wrong. Verified on the bench: exactly that, after a Save.
     *
     * Treating it as absent leaves the stored location alone, so the next refresh fills it from
     * the public IP — the same "only fill a blank" rule geo_ip.c already documents.
     *
     * The rule itself is api_location_is_set() in lib/apifmt, where it is host-tested: it has to
     * agree exactly with the app's sentinel, and this handler is not testable off-device. */
    if (cJSON_IsNumber(la) && cJSON_IsNumber(lo) &&
        api_location_is_set(la->valuedouble, lo->valuedouble)) {
        /* Reuse the one extra-config writer rather than opening NVS again here — it already
         * owns the "write only what was supplied" rule for every field it stores. */
        if (prov_store_extra_config(NULL, NULL, NULL,
                                    la->valuedouble, lo->valuedouble, 1) == 0) {
            ESP_LOGI(TAG, "location from the config document stored (%.4f, %.4f)",
                     la->valuedouble, lo->valuedouble);
        } else {
            ESP_LOGW(TAG, "could not store the location from the config document");
        }
    }
    cJSON_Delete(root);
}

/* Put the stored location back into the returned document.
 *
 * WHY THE RESPONSE AND NOT THE STORE: the NVS blobs are the source of truth — the device
 * writes them itself from the public IP (geo_ip.c) when the user has not chosen a position.
 * Echoing only what was stored as a document would show a blank or stale location in the app
 * on a device that knows exactly where it is, and the map would then open on 0,0. Injecting
 * the real value here means every reader — the map picker, the fields, save-to-file — sees
 * the same coordinates the firmware will fetch with. */
static void inject_stored_location(cJSON *root)
{
    nvs_handle_t h;
    double lat = 0, lon = 0;
    size_t llen = sizeof(lat);
    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    const esp_err_t e = nvs_get_blob(h, DEVENV_KEY_LOC_LAT, &lat, &llen);
    llen = sizeof(lon);
    const esp_err_t e2 = nvs_get_blob(h, DEVENV_KEY_LOC_LON, &lon, &llen);
    nvs_close(h);
    if (e != ESP_OK || e2 != ESP_OK) return;   /* nothing chosen and nothing guessed */

    cJSON *loc = cJSON_GetObjectItemCaseSensitive(root, "location");
    if (!cJSON_IsObject(loc)) {
        loc = cJSON_AddObjectToObject(root, "location");
        if (!loc) return;
    }
    /* cJSON_AddNumberToObject replaces an existing member, so this both fills a missing
     * location and corrects a stale one. */
    cJSON_DeleteItemFromObjectCaseSensitive(loc, "latitude");
    cJSON_DeleteItemFromObjectCaseSensitive(loc, "longitude");
    cJSON_AddNumberToObject(loc, "latitude", lat);
    cJSON_AddNumberToObject(loc, "longitude", lon);
}

/* Send a config document with the device's real location merged in. Falls back to sending the
 * document unchanged if it will not parse or the merge cannot be serialised — a GET that
 * returns the user's layout is far more useful than an error, and the location is the one
 * field the device is authoritative about. */
static esp_err_t api_send_config_json(httpd_req_t *req, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return api_send_json(req, json, "200 OK");

    inject_stored_location(root);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) return api_send_json(req, json, "200 OK");

    const esp_err_t e = api_send_json(req, out, "200 OK");
    free(out);
    return e;
}

static esp_err_t h_config_get(httpd_req_t *req)
{
    char *json = NULL;
    if (cfg_store_get(cfg_store_nvs(), &json) != 0) {
        /* Distinguish "never configured" from "corrupt": the former is a normal first-boot
         * state and is a 200 with the default document, so the web app can always load
         * something editable. */
        json = strdup(cfg_store_default_json());
    }
    if (!json) return api_send_err(req, "500 Internal Server Error", "oom");

    const esp_err_t e = api_send_config_json(req, json);
    free(json);
    return e;
}

/* Copy the VALUE of a top-level `"key": "value"` string into `out`, without building a cJSON
 * tree. Returns 0 on success.
 *
 * WHY A SCAN AND NOT cJSON: a PUT already parses the document once in cfg_store_put, and every
 * extra parse of an 8 KB config allocates a cJSON tree (~16 KB here) that does not always fit —
 * measured on hardware, a 22-widget config was rejected while a 21-widget one stored, purely on
 * tree size. Reading one short string for the power-mode comparison must not cost a whole parse.
 * The scan requires the key to be a real key (preceded by `{` or `,` followed by `:`), so a
 * string VALUE that happens to contain the text cannot be mistaken for it. */
static int scan_top_string(const char *json, const char *key, char *out, size_t cap)
{
    if (!json || !key || !out || cap == 0) return -1;
    char pat[48];
    const int pn = snprintf(pat, sizeof(pat), "\"%s\"", key);
    if (pn <= 0 || (size_t)pn >= sizeof(pat)) return -1;

    const char *k = strstr(json, pat);
    while (k) {
        const char *p = k;
        while (p > json && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r')) p--;
        if (p != json && (p[-1] == '{' || p[-1] == ',')) break;   /* looks like a key */
        k = strstr(k + 1, pat);
    }
    if (!k) return -1;

    k += pn;
    while (*k == ' ' || *k == '\t' || *k == '\n' || *k == '\r') k++;
    if (*k != ':') return -1;
    k++;
    while (*k == ' ' || *k == '\t' || *k == '\n' || *k == '\r') k++;
    if (*k != '"') return -1;
    k++;

    size_t i = 0;
    while (k[i] && k[i] != '"' && i < cap - 1) { out[i] = k[i]; i++; }
    if (k[i] != '"') return -1;              /* unterminated or too long */
    out[i] = '\0';
    return 0;
}

/* Would this document change the power mode the running boot path is already using?
 *
 * Compares against the STORED config, not against a default, so a PUT that merely re-sends
 * the same document does not restart the device. The comparison is on the parsed mode rather
 * than the raw string, so "battery" and "auto" in different key orders or with whitespace
 * still compare equal in meaning.
 *
 * Returns 0 on a document that cannot be read: one that cannot be read cannot be a deliberate
 * mode change, and restarting on a malformed body would be a denial-of-service against the owner.
 * It does NOT validate the document — cfg_store_put does, and the restart is only reached after
 * the store succeeds. `fresh_out` receives the document's mode so the caller can word the
 * restart warning without parsing again. */
static int power_mode_differs(const char *new_json, power_mode_t *fresh_out)
{
    /* An ABSENT powerMode means 'auto', exactly as layout_config_parse() defaults it. Returning
     * early on a missing key would be wrong: a stored 'battery' document replaced by one that
     * omits the field IS a change to auto, and treating it as "no change" would leave the device
     * asleep on a setting the user had just removed — the same silently-ignored save this
     * comparison exists to prevent. */
    char want[16];
    power_mode_t fresh = POWER_MODE_AUTO;
    if (scan_top_string(new_json, "powerMode", want, sizeof(want)) == 0) {
        fresh = power_mode_from_string(want);
    }
    if (fresh_out) *fresh_out = fresh;

    char *stored = NULL;
    char have[16];
    power_mode_t old = POWER_MODE_AUTO;        /* matches layout_config_parse's own default */
    if (cfg_store_get(cfg_store_nvs(), &stored) == 0 && stored) {
        if (scan_top_string(stored, "powerMode", have, sizeof(have)) == 0) {
            old = power_mode_from_string(have);
        }
        free(stored);
    }
    return fresh != old;
}

/* malloc a request body buffer, retrying briefly if the first attempt fails.
 *
 * WHY A RETRY AND NOT A SINGLE ATTEMPT: this part has no PSRAM and ONE big DRAM region, which
 * the network stack and the refresh machinery split into fragments. The web app pushes ARTWORK
 * first and the config immediately after, and the artwork upload ends by requesting a FULL
 * REFRESH — so for a few hundred milliseconds the heap is transiently fragmented and a single
 * malloc can fail on a device reporting 40 KB free. Observed on hardware: the browser's own save
 * flow got HTTP 500 {"error":"oom"} and then stored the identical document without complaint a
 * moment later. The transient is short, so retrying covers it, and this is the same remedy the
 * render path already uses for the same fragmentation.
 *
 * SAFE TO WAIT: nothing has been read from the socket yet, so the body is still there when the
 * retry succeeds. Returns NULL only after the window has elapsed, which is a genuine OOM. */
static char *alloc_body_retry(size_t len)
{
    char *p = malloc(len);
    for (int i = 0; i < 40 && !p; i++) {
        vTaskDelay(pdMS_TO_TICKS(25));
        p = malloc(len);
    }
    return p;
}

static esp_err_t h_config_put(httpd_req_t *req)
{
    /* Optional bearer auth (FR-31). Gated because this WRITES the stored config. */
    if (api_auth_gate(req)) return ESP_OK;

    /* Size the buffer from the request's own Content-Length, NOT from the 16 KB maximum.
     *
     * WHY THIS MATTERS ON THIS PART: the shipped config document is ~4.6 KB, but this used to
     * malloc API_CONFIG_MAX_LEN (16,384) for every PUT. The largest free DRAM block under HTTP
     * load sits at 13-17 KB — the network stack's allocations split the big region — so a 16 KB
     * request FAILS on a device reporting 80 KB free. That made the web app's primary save path
     * return HTTP 500 {"error":"oom"}, intermittently and confusingly, with the device otherwise
     * healthy. Allocating the actual body size removes the over-request entirely.
     *
     * The cap is still enforced, and enforced BEFORE allocating: a request at or above the cap is
     * the same 413 api_read_body() would have produced. */
    if (req->content_len <= 0 || (size_t)req->content_len >= API_CONFIG_MAX_LEN) {
        return api_send_err(req, "413 Payload Too Large", "body too large");
    }
    char *body = alloc_body_retry((size_t)req->content_len + 1);
    if (!body) return api_send_err(req, "500 Internal Server Error", "oom");

    const int n = api_read_body(req, body, (size_t)req->content_len + 1);
    if (n < 0) {
        free(body);
        return ESP_OK;      /* read_body already answered */
    }

    /* Does this document change the power behaviour the RUNNING boot path is already using?
     *
     * THIS MUST BE COMPARED BEFORE THE STORE, and that ordering is the whole subtlety:
     * power_mode_differs() reads the STORED config, so calling it after cfg_store_put() would
     * compare the new document against itself and always report "no change" — the restart
     * would silently never happen, which is exactly the saved-but-ignored failure this
     * feature exists to prevent.
     *
     * NO EXTRA PARSE HERE. This used to run layout_config_parse(body) FIRST and then call
     * power_mode_differs(), which parsed it again — two more transient cJSON trees on top of the
     * one cfg_store_put builds. On this part that was enough to push a full-size config over the
     * heap: measured on hardware, 21 widgets stored reliably and 22 never did. power_mode_differs
     * now scans the text, so the store is the only parse of the body. */
    power_mode_t fresh_mode = POWER_MODE_AUTO;
    const int power_changed = power_mode_differs(body, &fresh_mode);

    /* Log the SUB-CODE. "rejected config" alone cannot distinguish a malformed document
     * (-2/-3/-4) from a failed write (-5), and on this part those have completely different
     * causes — one is the user's fault, the other is the heap's.
     *
     * RETRIED BRIEFLY, for the same transient-fragmentation reason as alloc_body_retry(): the
     * store allocates a cJSON tree and then writes NVS, and either can fail for a few hundred
     * milliseconds while the heap is split by a refresh the artwork push just triggered. This was
     * observed as HTTP 400 on a valid document that stored without complaint moments later. A
     * GENUINELY bad document simply fails every attempt, so retrying costs nothing and cannot
     * admit something invalid. */
    int store_rc = cfg_store_put(cfg_store_nvs(), body);
    for (int i = 0; i < 20 && store_rc != 0; i++) {
        vTaskDelay(pdMS_TO_TICKS(25));
        store_rc = cfg_store_put(cfg_store_nvs(), body);
    }
    if (store_rc != 0) {
        ESP_LOGW(TAG, "rejected config (%d bytes, code %d)", n, store_rc);
        api_note_error("api: config rejected");
        free(body);
        return api_send_err(req, "400 Bad Request",
                        "invalid config: needs numeric schemaVersion and a valid layout");
    }

    /* The layout is stored; now act on the location it carries. Read the body BEFORE freeing
     * it — this is the only copy. */
    note_config_location(body);

    ESP_LOGI(TAG, "stored config (%d bytes)", n);
    free(body);

    /* Force a full refresh: the layout just changed, and a partial cannot clear the previous
     * page's glyphs, so a partial here would leave the old text ghosted behind the new. */
    api_request_full_refresh();

    /* A powerMode change must take effect, and only a reboot can apply it.
     *
     * WHY A REBOOT AND NOT A LIVE APPLY: the mode is consulted at exactly one point — before
     * WiFi init, where the VBAT sense runs — and from there it decides whether this boot ends
     * in the always-on serve loop or in deep sleep. A USB-powered device is in that serve
     * loop and never reboots, so without this the owner would set 'battery', watch the config
     * save, and see absolutely nothing happen — the same "saved but ignored" failure the
     * powerMode field itself was added to fix. Re-entering the boot path is also what makes
     * the change safe: the sleep decision is made with the radio OFF, which is required for
     * the ADC read.
     *
     * The reply is sent BEFORE the restart so the browser gets its confirmation rather than a
     * dropped connection — the same ordering the provisioning handover uses. */
    if (power_changed) {
        ESP_LOGW(TAG, "powerMode changed; restarting to apply it");

        /* Warn when the chosen mode will take the device OFF THE NETWORK. 'battery' means
         * deep sleep, and a sleeping device does not answer HTTP — so the owner can no longer
         * undo it from the config app. This response is the last moment the device can say
         * so, and saying it afterwards is no use to anyone. The escape is the factory-reset
         * button (5 s on KEY1). */
        char out[256];
        if (fresh_mode == POWER_MODE_BATTERY) {
            snprintf(out, sizeof(out),
                     "{\"status\":\"stored\",\"restarting\":true,\"warning\":"
                     "\"Battery mode puts the display to sleep between updates, so this page "
                     "will stop responding. To undo it, hold the reset button for 5 seconds.\"}");
        } else {
            snprintf(out, sizeof(out),
                     "{\"status\":\"stored\",\"restarting\":true}");
        }
        const esp_err_t e = api_send_json(req, out, "200 OK");
        /* Only once the body is on the wire. Restarting before the response is flushed would
         * drop the connection, and the app would report a failure for a change that actually
         * succeeded — the most confusing possible outcome. If the send failed the client is
         * already gone, so restarting is still the right thing to do. */
        vTaskDelay(pdMS_TO_TICKS(600));   /* let lwIP flush the socket */
        esp_restart();
        return e;                          /* not reached */
    }

    return api_send_json(req, "{\"status\":\"stored\"}", "200 OK");
}

/* ---------------------------------------------------------- GET/PUT /api/auth -- */

/* Report the auth state, and the token so the owner can copy it into the client.
 *
 * THE TOKEN IS RETURNED HERE AND NOWHERE ELSE. When auth is on, this endpoint is itself
 * gated (PUT below), but GET stays readable — deliberately, because the config app has to be
 * able to show "auth is ON, here is the token" to someone who already has the device in
 * front of them on the LAN, which is the same trust level the token is protecting. A device
 * whose owner has lost the token must remain recoverable without a factory reset.
 *
 * Also reports the device's own URL base so the app can tell the user which address to point
 * their client at, rather than making them find it. */
static esp_err_t h_auth_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return api_send_err(req, "500 Internal Server Error", "oom");

    cJSON_AddBoolToObject(root, "enabled", api_auth_enabled() ? 1 : 0);
    /* `wantEnabled` is the stored FLAG, which can differ from `enabled`: the owner may have
     * ticked the box before a token existed. Reporting both lets the app show "on, but no
     * token yet — generate one" instead of silently pretending it is off. */
    cJSON_AddBoolToObject(root, "wantEnabled", s_auth_enabled ? 1 : 0);
    cJSON_AddStringToObject(root, "token", api_auth_token());

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) return api_send_err(req, "500 Internal Server Error", "oom");
    const esp_err_t e = api_send_json(req, out, "200 OK");
    free(out);
    return e;
}

/* Change the auth settings: {"enabled": bool, "token": "..."} — either field optional.
 *
 * A PUT with `enabled: true` and no token GENERATES one. That is what makes the checkbox
 * usable: the user ticks it, and instead of being asked to invent a secret they get one to
 * copy. apiauth_required() would refuse to enforce an enabled-with-no-token state anyway, so
 * generating here is what turns the intent into something actually protecting the device.
 *
 * This endpoint is gated like the others: a device with auth on must not accept "turn auth
 * off" from an unauthenticated caller, which would make the whole feature a formality. */
static esp_err_t h_auth_put(httpd_req_t *req)
{
    if (api_auth_gate(req)) return ESP_OK;

    char *body = malloc(512);
    if (!body) return api_send_err(req, "500 Internal Server Error", "oom");
    if (api_read_body(req, body, 512) < 0) {
        free(body);
        return ESP_OK;                     /* read_body already answered */
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return api_send_err(req, "400 Bad Request", "invalid json");

    const cJSON *en = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    const cJSON *tk = cJSON_GetObjectItemCaseSensitive(root, "token");
    int want_enabled = s_auth_enabled;
    if (cJSON_IsBool(en)) want_enabled = cJSON_IsTrue(en) ? 1 : 0;

    /* A supplied token must be usable BEFORE anything is written. Storing an unusable one
     * would leave the device reporting "enabled" while enforcing nothing. */
    char new_token[APIAUTH_TOKEN_MAX + 1];
    snprintf(new_token, sizeof(new_token), "%s", s_auth_token);
    if (cJSON_IsString(tk) && tk->valuestring) {
        if (tk->valuestring[0] != '\0' && !apiauth_token_is_usable(tk->valuestring)) {
            cJSON_Delete(root);
            return api_send_err(req, "400 Bad Request",
                                "token must be 1-128 chars, no spaces or control characters");
        }
        snprintf(new_token, sizeof(new_token), "%s", tk->valuestring);
    }

    /* Enable with no token: generate one so the checkbox cannot leave the device in the
     * "enabled but unenforced" state described above. */
    if (want_enabled && new_token[0] == '\0') {
        apiauth_make_token(new_token, 32, (unsigned (*)(void))esp_random);
        ESP_LOGI(TAG, "generated an API token for the owner");
    }

    nvs_handle_t h;
    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        cJSON_Delete(root);
        return api_send_err(req, "500 Internal Server Error", "nvs unavailable");
    }
    esp_err_t e = nvs_set_u8(h, DEVENV_KEY_API_AUTH_ENABLED, (uint8_t)(want_enabled ? 1 : 0));
    if (e == ESP_OK) e = nvs_set_str(h, DEVENV_KEY_API_TOKEN, new_token);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    cJSON_Delete(root);

    if (e != ESP_OK) {
        ESP_LOGE(TAG, "storing auth settings failed: %s", esp_err_to_name(e));
        return api_send_err(req, "500 Internal Server Error", "could not store settings");
    }

    /* Re-read through the same path the gate uses, so what is enforced always matches what
     * was just stored — including the is-this-token-usable filter. */
    api_auth_reload();

    if (api_auth_enabled()) {
        ESP_LOGW(TAG, "API authentication is now ON");
    } else {
        ESP_LOGW(TAG, "API authentication is now OFF");
    }

    /* Echo the resulting state, including the token if one was generated, so the app can show
     * the user what to type without a second round trip. */
    return h_auth_get(req);
}

/* ----------------------------------------------------- GET /api/ha/entities ---- */

/* The HA entity search behind the config app's entity picker.
 *
 * WHY A DEVICE-SIDE PROXY AT ALL, instead of the browser asking HA directly: Home Assistant
 * sends no CORS headers, so a browser fetch from the config app's origin is blocked before it
 * is even sent — verified against the bench instance, where a valid token still produced
 * "blocked by CORS policy" and an empty picker. The device, meanwhile, holds the HA URL and
 * token and reaches HA every refresh. So the search runs HERE, with the credentials the device
 * already has, and the app gets a list it could never fetch itself.
 *
 * IT USES THE TEMPLATE API, not /api/states: the template renders server-side and returns only
 * matching ids, so the response is small enough for this part. /api/states on the bench instance
 * was 1590 entities and would not fit in any buffer worth holding here.
 *
 * `?q=` is REQUIRED and strictly validated (api_query_token). The query is interpolated into the
 * Jinja template, so an unvalidated term would be template injection — the same trust boundary
 * ha_template_add_entity() guards for a single id. */
#define HA_SEARCH_MAX   24          /* rows returned to the app */

static esp_err_t h_ha_entities(httpd_req_t *req)
{
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return api_send_err(req, "400 Bad Request", "missing ?q=");
    }
    char q[64];
    if (api_query_token(query, "q", q, sizeof(q)) != 0 || !ha_search_query_valid(q)) {
        return api_send_err(req, "400 Bad Request",
                            "q must be a lowercase entity-id fragment (a-z 0-9 _ . -)");
    }

    /* Read the credentials the device already holds. A missing URL/token is reported as such
     * rather than as "no matches" — the picker can then say what to fix instead of implying the
     * instance is empty. */
    nvs_handle_t h;
    char url[DEVENV_BUF_HA_URL] = {0};
    char token[DEVENV_BUF_HA_TOKEN] = {0};
    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return api_send_err(req, "409 Conflict", "no credentials stored");
    }
    size_t n = sizeof(url);
    const int have_url = (nvs_get_str(h, DEVENV_KEY_HA_URL, url, &n) == ESP_OK && url[0] != '\0');
    n = sizeof(token);
    const int have_tok = (nvs_get_str(h, DEVENV_KEY_HA_TOKEN, token, &n) == ESP_OK && token[0] != '\0');
    nvs_close(h);
    if (!have_url || !have_tok) {
        return api_send_err(req, "409 Conflict",
                            "no Home Assistant url/token stored on the display");
    }
    /* Normalise for the same reason the refresh path does: a stored trailing slash would make
     * "/api/template" a double slash and read as a bad request. */
    char norm[DEVENV_BUF_HA_URL];
    if (devcfg_normalize_ha_url(url, norm, sizeof(norm)) == 0) snprintf(url, sizeof(url), "%s", norm);

    /* Build the search template. The query is single-quoted INSIDE a double-quoted JSON string,
     * so it also cannot break the body.
     *
     * THE FIRST LINE IS THE TOTAL MATCH COUNT and the rest are up to HA_SEARCH_MAX rows, so the
     * app can tell "these are all of them" from "there were more". Emitting the count and the rows
     * with the SAME cap would make a clipped list always look complete — the exact kind of control
     * that reports a value it cannot actually know. Matches are collected ONCE into `m` so the
     * count and the rows agree, then sorted for a stable list.
     *
     * THE NAME IS SLICED TO HA_ENTITY_NAME_LEN HERE, not in the parser: it is display text and a
     * friendly_name can be arbitrarily long, so bounding it in the template keeps the response
     * size predictable whatever the instance holds. The id is not sliced — a real one reaches 69
     * chars, and truncating it would make the picker offer an entity that does not exist. */
    char body[512];
    const int bn = snprintf(body, sizeof(body),
        "{\"template\":\"{%% set m = states | selectattr('entity_id','contains','%s') | list %%}"
        "{{ m | length }}\\n"
        "{%% set c = namespace(n=0) %%}"
        "{%% for s in m | sort(attribute='entity_id') %%}"
        "{%% if c.n < %d %%}{{ s.entity_id }}|{{ s.name[:%d] }}\\n"
        "{%% set c.n = c.n + 1 %%}{%% endif %%}{%% endfor %%}\"}",
        q, HA_SEARCH_MAX, HA_ENTITY_NAME_LEN - 1);
    if (bn < 0 || (size_t)bn >= sizeof(body)) {
        return api_send_err(req, "500 Internal Server Error", "search template overflow");
    }

    /* The response is at most HA_SEARCH_MAX rows of (HA_ENTITY_ID_LEN + HA_ENTITY_NAME_LEN + 4)
     * bytes — 24 * 196 = ~4.7 KB, so 8 KB holds it with room for the count line and any escaping.
     * Heap, not `.bss`: a permanent static block is DRAM the heap never gets, and on this part the
     * 78,200-byte render window is decided by the largest FREE block — a standing allocation that
     * size can drop it below what the static layer needs. Freed on every path. */
    char *resp = malloc(8192);
    if (!resp) return api_send_err(req, "500 Internal Server Error", "oom");

    char turl[DEVENV_BUF_HA_URL + 32];
    snprintf(turl, sizeof(turl), "%s/api/template", url);
    const esp_err_t hx = net_http_post_json(turl, token, body, resp, 8192);
    if (hx != ESP_OK) {
        free(resp);
        return api_send_err(req, "502 Bad Gateway",
                            "could not reach Home Assistant (check the url and token)");
    }

    /* The parsed rows are ALSO on the heap, not on the handler's stack: 24 rows of 144 bytes is
     * 3.4 KB, and the httpd task's stack is 8 KB with the OTA handler's TLS headroom in it. A
     * 3.4 KB array beside the request body would leave the margin too thin for a response that
     * runs while another socket is being served. */
    ha_entity_t *rows = calloc(HA_SEARCH_MAX, sizeof(ha_entity_t));
    if (!rows) { free(resp); return api_send_err(req, "500 Internal Server Error", "oom"); }
    int total = 0;
    const int got = ha_parse_entity_list(resp, rows, HA_SEARCH_MAX, &total);
    free(resp);

    cJSON *root = cJSON_CreateObject();
    if (!root) { free(rows); return api_send_err(req, "500 Internal Server Error", "oom"); }
    cJSON *arr = cJSON_AddArrayToObject(root, "entities");
    for (int i = 0; arr && i < got; i++) {
        cJSON *o = cJSON_CreateObject();
        if (!o) break;
        cJSON_AddStringToObject(o, "entity_id", rows[i].id);
        /* The friendly name is only added when it differs from the id, so the app can show it
         * without a fallback of its own. */
        if (rows[i].name[0] && strcmp(rows[i].name, rows[i].id) != 0) {
            cJSON_AddStringToObject(o, "friendly_name", rows[i].name);
        }
        cJSON_AddItemToArray(arr, o);
    }
    free(rows);
    /* Say whether the list was clipped, so a short list is not read as a complete one. */
    cJSON_AddNumberToObject(root, "total", total);
    cJSON_AddBoolToObject(root, "truncated", total > got);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) return api_send_err(req, "500 Internal Server Error", "oom");
    const esp_err_t e = api_send_json(req, out, "200 OK");
    free(out);
    return e;
}

/* ------------------------------------------------------------- POST /api/bitmap ---- */

/* One upload session at a time, owned by the server task. The chunk state machine itself is
 * the host-tested module; this is the adapter that feeds it from sockets. */
static upload_session_t s_upload;

static esp_err_t h_bitmap(httpd_req_t *req)
{
    /* Optional bearer auth (FR-31). Gated because this writes a flash partition. */
    if (api_auth_gate(req)) return ESP_OK;

    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return api_send_err(req, "400 Bad Request", "missing query");
    }

    uint32_t offset = 0;
    uint32_t total = 0;
    if (api_query_u32(query, "offset", &offset) != 0) {
        return api_send_err(req, "400 Bad Request", "bad or missing offset");
    }
    if (api_query_u32(query, "total", &total) != 0) {
        return api_send_err(req, "400 Bad Request", "bad or missing total");
    }

    /* offset 0 begins a session; upload_begin() refuses a wrong total immediately, so a
     * mis-sized upload fails on its first request rather than after 78 KB of traffic. */
    if (offset == 0) {
        const upload_result_t r = upload_begin(&s_upload, total);
        if (r == UPLOAD_ERR_BAD_TOTAL) {
            return api_send_err(req, "400 Bad Request", "total must be 78200");
        }
        if (r != UPLOAD_OK) {
            /* An aborted session would otherwise leave the device waiting for the rest of
             * an upload that is never coming. Restarting is the right recovery: the client
             * is explicitly beginning a new upload from byte 0. */
            upload_abort(&s_upload);
            if (upload_begin(&s_upload, total) != UPLOAD_OK) {
                return api_send_err(req, "409 Conflict", "cannot start upload");
            }
        }
        /* Open the flash session BEFORE the first chunk is written. This erases the spare
         * partition and zeroes its header, which is what makes an interrupted upload safe:
         * the slot stays invalid — and the previous image stays live — until the final
         * header write in bitmap_store_finish_upload() (IF-2a). */
        if (bitmap_store_begin_upload() != 0) {
            upload_abort(&s_upload);
            api_note_error("api: cannot open bitmap slot");
            return api_send_err(req, "500 Internal Server Error", "cannot open bitmap slot");
        }
    }

    if (req->content_len == 0) {
        upload_abort(&s_upload);
        bitmap_store_abort_upload();
        return api_send_err(req, "400 Bad Request", "empty chunk");
    }
    if ((size_t)req->content_len > BITMAP_UPLOAD_MAX_CHUNK) {
        upload_abort(&s_upload);
        bitmap_store_abort_upload();
        return api_send_err(req, "413 Payload Too Large", "chunk over 4096 bytes");
    }

    /* Read the chunk into a stack buffer and write it straight to the spare partition.
     * The body is at most 4 KB, so a stack buffer is safe and avoids a heap allocation per
     * chunk — a 20-chunk upload would otherwise fragment the heap — and, more importantly,
     * the 78,200-byte image is never held in RAM at all (NFR-2). */
    uint8_t chunk[BITMAP_UPLOAD_MAX_CHUNK];
    size_t got = 0;
    while (got < (size_t)req->content_len) {
        const int r = httpd_req_recv(req, (char *)chunk + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            upload_abort(&s_upload);
            bitmap_store_abort_upload();
            return api_send_err(req, "400 Bad Request", "short chunk read");
        }
        got += (size_t)r;
    }

    upload_result_t r = upload_chunk(&s_upload, offset, chunk, (uint32_t)got);
    if (r != UPLOAD_OK) {
        /* Every one of these is a client bug or a corrupted transfer. None of them may
         * leave an open session, because the next chunk would then be validated against a
         * length that never existed. */
        upload_abort(&s_upload);
        bitmap_store_abort_upload();
        return api_send_err(req, "409 Conflict",
                        r == UPLOAD_ERR_BAD_OFFSET ? "chunk out of order; upload aborted"
                                                   : "bad chunk; upload aborted");
    }

    /* Only now that the state machine has accepted the bytes (correct offset, correct
     * length) do they reach flash. Writing first and validating second would let a
     * mis-ordered chunk land at the wrong place in the image. */
    if (bitmap_store_write_chunk(offset, chunk, (uint32_t)got) != 0) {
        upload_abort(&s_upload);
        bitmap_store_abort_upload();
        api_note_error("api: flash write failed");
        return api_send_err(req, "500 Internal Server Error", "flash write failed");
    }

    /* Not the last chunk — acknowledge and wait for more. */
    if (s_upload.received < s_upload.total) {
        char body[64];
        snprintf(body, sizeof(body), "{\"received\":%u}", (unsigned)s_upload.received);
        return api_send_json(req, body, "200 OK");
    }

    /* Final chunk. The checksum rides in the same request's query string. */
    uint32_t crc = 0;
    if (api_query_u32(query, "crc", &crc) != 0) {
        upload_abort(&s_upload);
        bitmap_store_abort_upload();
        return api_send_err(req, "400 Bad Request", "final chunk needs crc");
    }

    r = upload_commit(&s_upload, crc);
    if (r != UPLOAD_OK) {
        /* The session is already closed on failure, so a bad upload cannot be resumed into
         * a good one. The previously live bitmap is untouched: everything so far went to
         * the spare slot, whose header is still zeroed. */
        bitmap_store_abort_upload();
        api_note_error("api: bitmap checksum mismatch");
        return api_send_err(req, "409 Conflict", "checksum mismatch; upload aborted");
    }

    /* upload_commit() proved `crc` matches the bytes it received; finish_upload() re-reads
     * the image from flash and proves the same CRC arrived there before making it live. */
    if (bitmap_store_finish_upload(crc) != 0) {
        api_note_error("api: bitmap promote failed");
        return api_send_err(req, "500 Internal Server Error", "could not store bitmap");
    }

    api_request_full_refresh();
    return api_send_json(req, "{\"status\":\"promoted\"}", "200 OK");
}


/* ------------------------------------------------------------ POST /api/artwork ---- */

/* ONE upload session at a time, owned by the server task — same rule as the bitmap. */
static struct {
    int      active;
    uint32_t total;
    uint32_t written;
} s_awup;

/* Per-page static artwork (FR-15). The client sends the whole set as one stream — header, entry
 * table, then the concatenated zlib streams — because the header carries the checksum and the
 * table is what that checksum covers.
 *
 * WHY ITS OWN ENDPOINT RATHER THAN REUSING /api/bitmap: the bitmap's total is a FIXED 78,200
 * bytes and upload_begin() rejects anything else, which is the right check for a single full
 * frame. An artwork set is variable-length (fewer pages, smaller pictures), so it needs a
 * different contract, and overloading one endpoint with two meanings would put the "which total
 * is legal?" decision in a client-supplied parameter. */
#define ARTWORK_UPLOAD_MAX_CHUNK 4096u

static esp_err_t h_artwork(httpd_req_t *req)
{
    /* Optional bearer auth (FR-31). Gated because this writes a flash partition. */
    if (api_auth_gate(req)) return ESP_OK;

    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return api_send_err(req, "400 Bad Request", "missing query");
    }
    uint32_t offset = 0, total = 0;
    if (api_query_u32(query, "offset", &offset) != 0) {
        return api_send_err(req, "400 Bad Request", "bad or missing offset");
    }
    if (api_query_u32(query, "total", &total) != 0) {
        return api_send_err(req, "400 Bad Request", "bad or missing total");
    }

    if (offset == 0) {
        /* Bounded so a hostile or buggy client cannot ask the device to erase and fill a
         * partition with an arbitrarily long stream (NFR-2 has no PSRAM to spare). */
        const uint32_t max_total = (uint32_t)artwork_blob_offset()
                                 + ARTWORK_MAX_COMP * ARTWORK_MAX_PAGES;
        if (total == 0 || total > max_total) {
            return api_send_err(req, "400 Bad Request", "artwork total out of range");
        }
        /* A new session starting while one is open is a restart, not an error — the same
         * recovery the bitmap handler uses, so a client that gave up mid-upload can begin again
         * without the device waiting forever for the rest of the abandoned one. */
        if (s_awup.active) artwork_store_abort_upload();
        if (artwork_store_begin_upload() != 0) {
            api_note_error("api: cannot open artwork slot");
            return api_send_err(req, "500 Internal Server Error", "cannot open artwork slot");
        }
        s_awup.active = 1;
        s_awup.total = total;
        s_awup.written = 0;
    }

    if (!s_awup.active) {
        return api_send_err(req, "409 Conflict", "no artwork upload in progress");
    }
    if (req->content_len == 0) {
        artwork_store_abort_upload(); s_awup.active = 0;
        return api_send_err(req, "400 Bad Request", "empty chunk");
    }
    if ((size_t)req->content_len > ARTWORK_UPLOAD_MAX_CHUNK) {
        artwork_store_abort_upload(); s_awup.active = 0;
        return api_send_err(req, "413 Payload Too Large", "chunk over 4096 bytes");
    }
    if (offset != s_awup.written) {
        artwork_store_abort_upload(); s_awup.active = 0;
        return api_send_err(req, "409 Conflict", "chunk out of order; upload aborted");
    }
    if (offset + (uint32_t)req->content_len > s_awup.total) {
        artwork_store_abort_upload(); s_awup.active = 0;
        return api_send_err(req, "400 Bad Request", "chunk past the declared total");
    }

    /* A stack buffer for the chunk, then straight to flash: a 4 KB body is safe on this task's
     * stack, and it keeps the whole set out of RAM, which matters because the uncompressed
     * layers are 78,200 bytes each (NFR-2). */
    uint8_t chunk[ARTWORK_UPLOAD_MAX_CHUNK];
    size_t got = 0;
    while (got < (size_t)req->content_len) {
        const int r = httpd_req_recv(req, (char *)chunk + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            artwork_store_abort_upload(); s_awup.active = 0;
            return api_send_err(req, "400 Bad Request", "short chunk read");
        }
        got += (size_t)r;
    }

    if (artwork_store_write_chunk(offset, chunk, (uint32_t)got) != 0) {
        artwork_store_abort_upload(); s_awup.active = 0;
        api_note_error("api: artwork flash write failed");
        return api_send_err(req, "500 Internal Server Error", "artwork write failed");
    }
    s_awup.written = offset + (uint32_t)got;

    if (s_awup.written < s_awup.total) {
        char body[64];
        snprintf(body, sizeof(body), "{\"received\":%u}", (unsigned)s_awup.written);
        return api_send_json(req, body, "200 OK");
    }

    /* Final chunk: the checksum rides in this request's query string, exactly as the bitmap's
     * does. Only now can the set become live — until this succeeds the spare slot's header stays
     * invalid and the PREVIOUS artwork remains selected. */
    uint32_t crc = 0;
    if (api_query_u32(query, "crc", &crc) != 0) {
        artwork_store_abort_upload(); s_awup.active = 0;
        return api_send_err(req, "400 Bad Request", "final chunk needs crc");
    }
    s_awup.active = 0;
    if (artwork_store_finish_upload(crc) != 0) {
        api_note_error("api: artwork promote failed");
        return api_send_err(req, "409 Conflict", "artwork checksum mismatch; upload aborted");
    }

    /* A full refresh, because the picture on the glass just changed. A partial would diff the
     * new artwork against the old and leave fragments of the previous layout behind. */
    api_request_full_refresh();
    return api_send_json(req, "{\"status\":\"promoted\"}", "200 OK");
}

/* ----------------------------------------------------------------- POST /refresh --- */

static esp_err_t h_refresh(httpd_req_t *req)
{
    /* Optional bearer auth (FR-31). Gated because it makes the panel change. */
    if (api_auth_gate(req)) return ESP_OK;

    /* `?page=N` asks for a SPECIFIC page (FR-15), which the layout editor uses so the page it is
     * editing appears without waiting for that page's rotation slot. Absent, the ordinary
     * on-demand full refresh is scheduled. An unparseable or out-of-range value is treated as
     * absent rather than rejected: the request still means "refresh", and failing it would leave
     * the user with a change on the device and nothing on the glass. */
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        uint32_t page = 0;
        if (api_query_u32(query, "page", &page) == 0) {
            api_request_page((int)page);
            return api_send_json(req, "{\"status\":\"page refresh scheduled\"}", "202 Accepted");
        }
    }

    api_request_full_refresh();
    return api_send_json(req, "{\"status\":\"refresh scheduled\"}", "202 Accepted");
}

/* --------------------------------------------------------------------- POST /ota ---- */

/* The handler lives in api_ota.c — see api_ota.h for why OTA is kept on its own. */

/* ------------------------------------------------------------------ /api/secrets ---- */

/* The credential store, writable from the config app once the device is running.
 *
 * WHY THIS EXISTS AT ALL: the OWM key and the HA URL/token could only ever reach NVS through
 * the captive portal (prov_store_extra_config), which runs on FIRST BOOT only — a device that
 * is already on the network has no way to be given them. The plan says exactly what was
 * missing ("the web UI writes the OWM key, HA URL/token and location"), and the symptom on the
 * bench was a device whose HA boxes read "--" with "HA entities are bound but no HA url/token
 * is stored" in its log, while HA_URL and HA_TOKEN sat in code/.env. There was no endpoint and
 * no UI field.
 *
 * WRITE-ONLY BY DESIGN. GET reports only WHICH secrets are set, never their values — the
 * device's own log and API are readable over the same LAN, and echoing a token back would put
 * it in the browser cache, the devtools network log and any proxy in between. The user who
 * needs the value has it where they got it.
 *
 * AN EMPTY FIELD LEAVES THE STORED VALUE ALONE, which is the same rule the portal applies and
 * the whole reason the OWM key survives a Save: the app sends the form every time, and treating
 * a blank as "erase" would wipe a working key because the user came back to change the HA URL.
 * Erasing is therefore a separate, explicit act — see the factory-reset button — rather than a
 * side effect of a blank box.
 *
 * GATED by api_auth_gate() when protection is on: this is the one endpoint that writes
 * credentials, so it must sit behind the same token as the other mutating verbs. */
static esp_err_t h_secrets_put(httpd_req_t *req)
{
    if (api_auth_gate(req)) return ESP_OK;

    if (req->content_len <= 0 || (size_t)req->content_len >= API_CONFIG_MAX_LEN) {
        return api_send_err(req, "413 Payload Too Large", "body too large");
    }
    char *body = alloc_body_retry((size_t)req->content_len + 1);
    if (!body) return api_send_err(req, "500 Internal Server Error", "oom");

    const int n = api_read_body(req, body, (size_t)req->content_len + 1);
    if (n < 0) { free(body); return ESP_OK; }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return api_send_err(req, "400 Bad Request", "body must be JSON");
    }

    /* Each field is read as a STRING and passed through only when non-empty. A field that is
     * absent, null, or "" means "leave it as it is" — see the note above. */
    const cJSON *owm = cJSON_GetObjectItemCaseSensitive(root, "owmKey");
    const cJSON *hau = cJSON_GetObjectItemCaseSensitive(root, "haUrl");
    const cJSON *hat = cJSON_GetObjectItemCaseSensitive(root, "haToken");

    const char *owm_s = (cJSON_IsString(owm) && owm->valuestring[0]) ? owm->valuestring : NULL;
    const char *hau_s = (cJSON_IsString(hau) && hau->valuestring[0]) ? hau->valuestring : NULL;
    const char *hat_s = (cJSON_IsString(hat) && hat->valuestring[0]) ? hat->valuestring : NULL;

    /* REJECT AN OVER-LONG VALUE rather than storing something the reader cannot hold. The store
     * has no length limit of its own, so a value longer than its buffer would sit in NVS and read
     * back as an empty string — the device would look unconfigured while holding the credential.
     * Better a 413 naming the field than a save that appears to work. */
    if (owm_s && strlen(owm_s) >= DEVENV_BUF_OWM_KEY) {
        cJSON_Delete(root);
        return api_send_err(req, "413 Payload Too Large", "owmKey is too long");
    }
    if (hau_s && strlen(hau_s) >= DEVENV_BUF_HA_URL) {
        cJSON_Delete(root);
        return api_send_err(req, "413 Payload Too Large", "haUrl is too long");
    }
    if (hat_s && strlen(hat_s) >= DEVENV_BUF_HA_TOKEN) {
        cJSON_Delete(root);
        return api_send_err(req, "413 Payload Too Large", "haToken is too long");
    }

    /* Validate and normalise the HA URL. A bare host with no scheme
     * ("homeassistant.local:8123") is the common typo, and net_http would report it as a generic
     * request failure with nothing pointing at the URL, so requiring http:// or https:// makes
     * that a 400 with the reason on it. A trailing slash is stripped because the firmware appends
     * "/api/template" — a double slash is a 404 on some HA reverse proxies, and that failure
     * reads like a bad token rather than a malformed URL. The rules live in devcfg_normalize_ha_url()
     * so the boundary cases are host-tested rather than reachable only on a live device. */
    char ha_url_norm[DEVENV_BUF_HA_URL];
    if (hau_s) {
        const int nrc = devcfg_normalize_ha_url(hau_s, ha_url_norm, sizeof(ha_url_norm));
        if (nrc == -2) {
            cJSON_Delete(root);
            return api_send_err(req, "400 Bad Request",
                                "haUrl must begin with http:// or https://");
        }
        if (nrc == -3) {
            cJSON_Delete(root);
            return api_send_err(req, "400 Bad Request", "haUrl has no host");
        }
        if (nrc != 0) {
            cJSON_Delete(root);
            return api_send_err(req, "400 Bad Request", "haUrl is not usable");
        }
        hau_s = ha_url_norm;
    }

    if (!owm_s && !hau_s && !hat_s) {
        cJSON_Delete(root);
        return api_send_err(req, "400 Bad Request", "no credential fields supplied");
    }

    /* prov_store_extra_config() already owns the "write only what was supplied" rule for every
     * field it stores, so this reuses it rather than opening NVS a second time with its own
     * copy of the rule — the exact duplication that made the WiFi keys diverge once already. */
    const int rc = prov_store_extra_config(owm_s, hau_s, hat_s, 0, 0, 0);
    cJSON_Delete(root);
    if (rc != 0) return api_send_err(req, "500 Internal Server Error", "could not store");

    ESP_LOGI(TAG, "stored credentials (%s%s%s)",
             owm_s ? "owm " : "", hau_s ? "ha-url " : "", hat_s ? "ha-token" : "");
    /* Ask the refresh path to redraw: a newly entered key changes what every widget resolves
     * to, and the user is looking at the panel when they press Save. Full, not partial — the
     * values can change from "--" to a number, which is a content change a partial handles fine,
     * but the request path forces a full refresh by design (see the serve loop). */
    api_request_full_refresh();
    return api_send_json(req, "{\"status\":\"stored\"}", "200 OK");
}

/* GET reports WHICH secrets are set, never what they are. The boolean shape is deliberate: it
 * is enough for the UI to show "configured" beside a blank field without ever receiving the
 * value, so a screenshot or a proxy log cannot leak a token. */
static esp_err_t h_secrets_get(httpd_req_t *req)
{
    nvs_handle_t h;
    char url[DEVENV_BUF_HA_URL] = {0};
    int have_owm = 0, have_url = 0, have_token = 0;

    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        char k[DEVENV_BUF_OWM_KEY] = {0};
        size_t kn = sizeof(k);
        have_owm = (nvs_get_str(h, DEVENV_KEY_OWM_KEY, k, &kn) == ESP_OK && k[0] != '\0');

        kn = sizeof(url);
        have_url = (nvs_get_str(h, DEVENV_KEY_HA_URL, url, &kn) == ESP_OK && url[0] != '\0');

        char t[DEVENV_BUF_HA_TOKEN] = {0};
        size_t tn = sizeof(t);
        have_token = (nvs_get_str(h, DEVENV_KEY_HA_TOKEN, t, &tn) == ESP_OK && t[0] != '\0');
        nvs_close(h);
    }

    /* The HA URL IS returned, unlike the token. It is not a secret — the app already needs it
     * to offer the entity picker (listEntities), and hiding it would make the user retype an
     * address the device is happily using. The token and the OWM key stay out. */
    cJSON *root = cJSON_CreateObject();
    if (!root) return api_send_err(req, "500 Internal Server Error", "oom");
    cJSON_AddBoolToObject(root, "owmKey", have_owm);
    cJSON_AddBoolToObject(root, "haToken", have_token);
    cJSON_AddStringToObject(root, "haUrl", have_url ? url : "");

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) return api_send_err(req, "500 Internal Server Error", "oom");
    const esp_err_t e = api_send_json(req, out, "200 OK");
    free(out);
    return e;
}

/* ----------------------------------------------------------------------- server --- */

esp_err_t api_start(void)
{
    /* The HTTP server needs a TCP/IP stack to listen on. Without esp_netif and the default
     * event loop it aborts inside lwIP with "tcpip_send_msg_wait_sem (Invalid mbox)" — and
     * the case that matters most is a device with NO credentials yet, where the API is the
     * only way to configure it. net_stack_init() is idempotent and does not connect. */
    esp_err_t e = net_stack_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "network stack init failed: %s", esp_err_to_name(e));
        return e;
    }

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    /* Several endpoints hold a 16 KB config body or a block during a flash write, and the
     * web app fetches status while an upload runs. The default 4 is tight enough to make a
     * legitimate client see spurious failures. */
    cfg.max_open_sockets = 7;
    /* RAISED FROM THE DEFAULT 8, which the API alone now fills (8 endpoints), leaving no slot
     * for the web UI's catch-all — it failed to register with ESP_ERR_HTTPD_HANDLERS_FULL and
     * the device served 404s for its own page while the API worked fine. Measured need: 10 API
     * routes + 1 for the UI, with headroom for the next endpoint. */
    cfg.max_uri_handlers = 18;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;      /* the OTA handler needs TLS headroom, like net_http */
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    e = httpd_start(&s_server, &cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(e));
        return e;
    }

    /* Load the auth settings BEFORE the first request can arrive. */
    api_auth_reload();

    static const httpd_uri_t uris[] = {
        { .uri = "/api/status",   .method = HTTP_GET,  .handler = h_status },
        { .uri = "/api/values",   .method = HTTP_GET,  .handler = h_values },
        { .uri = "/api/config",   .method = HTTP_GET,  .handler = h_config_get },
        { .uri = "/api/config",   .method = HTTP_PUT,  .handler = h_config_put },
        { .uri = "/api/auth",     .method = HTTP_GET,  .handler = h_auth_get },
        { .uri = "/api/auth",     .method = HTTP_PUT,  .handler = h_auth_put },
        { .uri = "/api/bitmap",   .method = HTTP_POST, .handler = h_bitmap },
        { .uri = "/api/artwork",  .method = HTTP_POST, .handler = h_artwork },
        { .uri = "/api/refresh",  .method = HTTP_POST, .handler = h_refresh },
        { .uri = "/api/ha/entities", .method = HTTP_GET, .handler = h_ha_entities },
        { .uri = "/api/secrets",  .method = HTTP_GET,  .handler = h_secrets_get },
        { .uri = "/api/secrets",  .method = HTTP_PUT,  .handler = h_secrets_put },
        { .uri = "/api/ota",      .method = HTTP_POST, .handler = api_ota_handler },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        e = httpd_register_uri_handler(s_server, &uris[i]);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed: %s", uris[i].uri, esp_err_to_name(e));
            return e;
        }
    }

    /* Mount the web UI AFTER the API routes. esp_http_server matches the most recently
     * registered handler first, so registering this catch-all earlier would shadow every
     * endpoint — the page would load and then every request it made would return the app
     * shell. The handler also refuses the API prefix, so a future reorder cannot reintroduce
     * it. */
    const esp_err_t ue = webui_mount(s_server);
    if (ue != ESP_OK) {
        /* NOT fatal: the device still configures over the API and the serial log. */
        ESP_LOGW(TAG, "web UI not mounted: %s", esp_err_to_name(ue));
    }

    ESP_LOGI(TAG, "API listening (%u endpoints)", (unsigned)(sizeof(uris) / sizeof(uris[0])));
    return ESP_OK;
}
