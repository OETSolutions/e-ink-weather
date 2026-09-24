#include "net_http.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "net_http";

/* The response sink. `overflow` is sticky: once the body has been clipped there is no way
 * to un-clip it, and handing a truncated JSON document to the parser would surface as a
 * syntax error instead of the real problem (a too-small buffer). */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    int    overflow;
} http_sink_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    http_sink_t *s = (http_sink_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && s && evt->data_len > 0) {
        if (s->len + (size_t)evt->data_len < s->cap) {
            memcpy(s->buf + s->len, evt->data, (size_t)evt->data_len);
            s->len += (size_t)evt->data_len;
            s->buf[s->len] = '\0';
        } else {
            s->overflow = 1;
            ESP_LOGE(TAG, "response overflow (%u + %d >= %u)",
                     (unsigned)s->len, evt->data_len, (unsigned)s->cap);
        }
    }
    return ESP_OK;
}

/* Everything that touches TLS runs on the worker task, never on the caller's stack. */
typedef struct {
    const char         *url;
    const char         *bearer;
    const char         *body;
    http_sink_t         sink;
    SemaphoreHandle_t   done;
    esp_err_t           err;
    /* The HTTP status, or 0 if no response was received at all. Reported separately from `err`
     * because the CALLER sometimes needs to tell two failures apart that `err` deliberately
     * merges: FR-6's One Call probe must distinguish the documented 401 ("this key has no
     * subscription") from a transport failure or an oversized response, and treating the latter
     * as the former is what makes a paying key look unsubscribed. */
    int                 status;
    unsigned            hwm;      /* stack headroom in bytes, measured after the work */
} http_job_t;

static unsigned s_hwm;

static void http_worker(void *arg)
{
    http_job_t *j = (http_job_t *)arg;
    esp_err_t err = ESP_FAIL;

    esp_http_client_config_t cfg = {
        .url = j->url,
        .event_handler = on_http_event,
        .user_data = &j->sink,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .keep_alive_enable = false,
        /* THE TX BUFFER MUST HOLD A REDIRECT TARGET, NOT JUST THIS REQUEST. GitHub's release
         * download URL 302s to a CDN URL carrying a long signed query string — measured at 921
         * bytes for a release asset — and esp_http_client re-issues the request with THAT path
         * and query. At the 512-byte default it fails with `HTTP_CLIENT: Out of buffer` after a
         * SUCCESSFUL handshake (observed on the bench: "Certificate validated" then "Out of
         * buffer", so the failure looks like the network but is the buffer).
         *
         * 2048 covers the observed 921 with room for a longer path or a chunkier signature; the
         * cost is 2 KB of DRAM for the life of one request, taken inside the fetch window when
         * the render layer is already released. */
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        err = ESP_FAIL;
        goto finish;
    }

    esp_http_client_set_header(c, "Content-Type", "application/json");
    if (j->bearer) {
        /* A long-lived HA token is ~180 chars; 512 leaves generous room without a heap
         * allocation on the hot path. A clipped token would produce a 401 that looks like
         * a bad token, so the truncation is detected rather than ignored. */
        char hdr[512];
        int n = snprintf(hdr, sizeof(hdr), "Bearer %s", j->bearer);
        if (n < 0 || (size_t)n >= sizeof(hdr)) {
            ESP_LOGE(TAG, "bearer token too long (%d)", n);
            esp_http_client_cleanup(c);
            err = ESP_ERR_INVALID_ARG;
            goto finish;
        }
        esp_http_client_set_header(c, "Authorization", hdr);
    }
    if (j->body) {
        esp_http_client_set_method(c, HTTP_METHOD_POST);
        esp_http_client_set_post_field(c, j->body, (int)strlen(j->body));
    }

    esp_err_t perr = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    j->status = status;

    if (perr != ESP_OK) {
        err = perr;
    } else if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "HTTP %d for %s", status, j->url);
        err = ESP_ERR_INVALID_RESPONSE;
    } else if (j->sink.overflow) {
        err = ESP_ERR_NO_MEM;      /* buffer too small — not a parse error */
    } else {
        err = ESP_OK;
    }

finish:
    /* Measured AFTER the work: this is the evidence that justifies NET_TLS_TASK_STACK. */
    j->hwm = (unsigned)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
    j->err = err;
    xSemaphoreGive(j->done);
    vTaskDelete(NULL);              /* a FreeRTOS task must not simply return */
}

static esp_err_t do_request(const char *url, const char *bearer, const char *body,
                            char *out, size_t outlen, int *status_out)
{
    if (!url || !out || outlen == 0) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';
    if (status_out) *status_out = 0;

    http_job_t job = {
        .url = url, .bearer = bearer, .body = body,
        .sink = { .buf = out, .cap = outlen, .len = 0, .overflow = 0 },
        .done = NULL, .err = ESP_FAIL, .status = 0, .hwm = 0,
    };
    job.done = xSemaphoreCreateBinary();
    if (!job.done) return ESP_ERR_NO_MEM;

    /* The caller (app_main) has only ~3.5 KB of stack
     * (CONFIG_ESP_MAIN_TASK_STACK_SIZE), so the handshake cannot run on it. This task is
     * created with an explicit size, joined, and then self-deletes — it must not stay
     * resident, because the device deep-sleeps between refreshes and a stray task would
     * block that. */
    BaseType_t ok = xTaskCreate(http_worker, "net_tls", NET_TLS_TASK_STACK, &job,
                                NET_TLS_TASK_PRIO, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "cannot create TLS task (%u bytes)", (unsigned)NET_TLS_TASK_STACK);
        vSemaphoreDelete(job.done);
        return ESP_ERR_NO_MEM;
    }

    /* Bounded wait, so a wedged worker fails this wake instead of hanging the device.
     * The HTTP client's own 10 s timeout is the primary bound; this is the backstop. */
    if (xSemaphoreTake(job.done, pdMS_TO_TICKS(30000)) != pdTRUE) {
        ESP_LOGE(TAG, "TLS task still running after 30 s; waiting for it to finish");
        /* WAIT FOR THE WORKER ANYWAY — do NOT return here.
         *
         * `job` lives on THIS stack frame and the worker holds `&job` for its whole life,
         * writing job.sink.buf (the caller's forecast block or a caller stack array) as the
         * response arrives. Returning on the timeout would unwind this frame and continue the
         * refresh while a live task was still dereferencing both — a use-after-free that the 30 s
         * backstop makes REACHABLE rather than theoretical, because esp_http_client's timeout
         * bounds each SOCKET OPERATION (SO_RCVTIMEO via esp_tls) and not the whole transfer: a
         * server trickling the body slowly enough to beat the per-read timeout keeps the worker
         * alive past 30 s.
         *
         * Waiting cannot hang forever: every socket read/write is timed out by esp_tls, so the
         * worker always reaches its own exit. Memory corruption is the worse outcome. */
        xSemaphoreTake(job.done, portMAX_DELAY);
    }

    s_hwm = job.hwm;
    esp_err_t err = job.err;
    if (status_out) *status_out = job.status;
    vSemaphoreDelete(job.done);
    return err;
}

unsigned net_http_stack_hwm(void)
{
    return s_hwm;
}

esp_err_t net_http_get_json(const char *url, const char *bearer, char *out, size_t outlen)
{
    return do_request(url, bearer, NULL, out, outlen, NULL);
}

esp_err_t net_http_get_json_status(const char *url, const char *bearer, char *out, size_t outlen,
                                   int *status_out)
{
    return do_request(url, bearer, NULL, out, outlen, status_out);
}

esp_err_t net_http_post_json(const char *url, const char *bearer, const char *body,
                             char *out, size_t outlen)
{
    return do_request(url, bearer, body, out, outlen, NULL);
}
