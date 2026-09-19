#pragma once
#include "esp_err.h"
#include <stddef.h>

/* TLS handshake stack budget. Sized deliberately, NOT inherited: the Arduino crash class
 * this project exists to avoid is exactly a too-small handshake stack (spec §9.1 — the
 * Arduino loopTask overflows at ~8 KB). The IDF main task is only 3.5 KB
 * (CONFIG_ESP_MAIN_TASK_STACK_SIZE), so the handshake MUST NOT run on it — every request
 * goes through a worker task created with NET_TLS_TASK_STACK.
 *
 * 16 KB is comfortably above measured need; lower it only with evidence from
 * net_http_stack_hwm(). */
#define NET_TLS_TASK_STACK  16384
#define NET_TLS_TASK_PRIO   5

/* Heap-free bytes observed at the LOWEST point of the most recent request's TLS work
 * (uxTaskGetStackHighWaterMark). This is the evidence required before changing the stack
 * size, and it is logged on the verification build. Returns 0 if no request has run. */
unsigned net_http_stack_hwm(void);

/* GET/POST JSON with a bearer token. `out` is NUL-terminated on success.
 * Returns ESP_OK only for a 2xx response with a body that fit `out` entirely. */
esp_err_t net_http_get_json(const char *url, const char *bearer,
                            char *out, size_t outlen);
esp_err_t net_http_post_json(const char *url, const char *bearer,
                             const char *body, char *out, size_t outlen);
