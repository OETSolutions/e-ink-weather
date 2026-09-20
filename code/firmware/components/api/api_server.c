#include "api.h"
#include "api_internal.h"
#include "api_ota.h"
#include "api_status.h"
#include "owm_counter.h"
#include "net_wifi.h"
#include "cfg_store.h"
#include "devcfg.h"
#include "layout_model.h"
#include "nvs_keys.h"
#include "prov.h"
#include "nvs.h"
#include "bitmap_upload.h"
#include "bitmap_slot.h"
#include "api_wire.h"
#include "api_store.h"
#include "api_internal.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "api";

#define API_MAX_BODY    (API_CONFIG_MAX_LEN + 1024)

/* ------------------------------------------------------------------------ runtime state */

static httpd_handle_t s_server;

static int      s_partials_since_full;
static int      s_fulls_total;
static int64_t  s_last_refresh_us;      /* 0 = never */
static int      s_pending_full_refresh;
static uint32_t s_free_heap_min;

/* Task to notify when a refresh is requested (see api_set_refresh_task). */
static void    *s_refresh_task;

static char s_errors[API_STATUS_MAX_ERRORS][API_STATUS_ERROR_LEN];
static int  s_error_count;

/* Boot-time battery reading, cached because ADC2 cannot be read with WiFi up (HW-3). */
static double s_vbat;

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
    unlock();
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

    /* Type- and range-checked, for the same reason the portal checks: this value is about to
     * be interpolated into an OWM URL, so a bogus coordinate must not get that far. */
    if (cJSON_IsNumber(la) && cJSON_IsNumber(lo) &&
        la->valuedouble >= -90.0 && la->valuedouble <= 90.0 &&
        lo->valuedouble >= -180.0 && lo->valuedouble <= 180.0) {
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

static esp_err_t h_config_put(httpd_req_t *req)
{
    char *body = malloc(API_CONFIG_MAX_LEN);
    if (!body) return api_send_err(req, "500 Internal Server Error", "oom");

    const int n = api_read_body(req, body, API_CONFIG_MAX_LEN);
    if (n < 0) {
        free(body);
        return ESP_OK;      /* read_body already answered */
    }

    /* Validate, then store. cfg_store_put runs schemaVersion -> devcfg_migrate ->
     * layout_config_parse and writes NOTHING unless all three pass. A bad config can
     * therefore never be persisted, so the device cannot be bricked into an unparseable
     * state by a bad PUT (Task 14 Step 2). */
    if (cfg_store_put(cfg_store_nvs(), body) != 0) {
        ESP_LOGW(TAG, "rejected config (%d bytes)", n);
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

    return api_send_json(req, "{\"status\":\"stored\"}", "200 OK");
}

/* ------------------------------------------------------------- POST /api/bitmap ---- */

/* One upload session at a time, owned by the server task. The chunk state machine itself is
 * the host-tested module; this is the adapter that feeds it from sockets. */
static upload_session_t s_upload;

static esp_err_t h_bitmap(httpd_req_t *req)
{
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

/* ----------------------------------------------------------------- POST /refresh --- */

static esp_err_t h_refresh(httpd_req_t *req)
{
    api_request_full_refresh();
    return api_send_json(req, "{\"status\":\"refresh scheduled\"}", "202 Accepted");
}

/* --------------------------------------------------------------------- POST /ota ---- */

/* The handler lives in api_ota.c — see api_ota.h for why OTA is kept on its own. */

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
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;      /* the OTA handler needs TLS headroom, like net_http */
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    e = httpd_start(&s_server, &cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(e));
        return e;
    }

    static const httpd_uri_t uris[] = {
        { .uri = "/api/status",   .method = HTTP_GET,  .handler = h_status },
        { .uri = "/api/config",   .method = HTTP_GET,  .handler = h_config_get },
        { .uri = "/api/config",   .method = HTTP_PUT,  .handler = h_config_put },
        { .uri = "/api/bitmap",   .method = HTTP_POST, .handler = h_bitmap },
        { .uri = "/api/refresh",  .method = HTTP_POST, .handler = h_refresh },
        { .uri = "/api/ota",      .method = HTTP_POST, .handler = api_ota_handler },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        e = httpd_register_uri_handler(s_server, &uris[i]);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed: %s", uris[i].uri, esp_err_to_name(e));
            return e;
        }
    }

    ESP_LOGI(TAG, "API listening (%u endpoints)", (unsigned)(sizeof(uris) / sizeof(uris[0])));
    return ESP_OK;
}
