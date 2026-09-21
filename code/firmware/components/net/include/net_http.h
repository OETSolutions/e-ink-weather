#pragma once
#include "esp_err.h"
#include <stddef.h>

/* TLS handshake stack budget. Sized deliberately, NOT inherited: the Arduino crash class
 * this project exists to avoid is exactly a too-small handshake stack (spec §9.1 — the
 * Arduino loopTask overflows at ~8 KB). The IDF main task is only 3.5 KB
 * (CONFIG_ESP_MAIN_TASK_STACK_SIZE), so the handshake MUST NOT run on it — every request
 * goes through a worker task created with NET_TLS_TASK_STACK.
 *
 * SIZED FROM MEASUREMENT, which the original 16 KB was not. net_http_stack_hwm() was written for
 * exactly this and then never read by anything, so the 16 KB was a guess — and 16 KB of DRAM,
 * allocated and freed inside the render window, is what fragments the region the resident
 * framebuffer must come from. Measured on the bench with HEAP_TRACE (7 samples over 10
 * refreshes): the worker never touches more than ~3.4 KB, leaving 12,848-13,000 bytes untouched
 * every time. The observed worst case is ~3.5 KB, i.e. the same as the IDF main task this
 * project explicitly refuses to run TLS on.
 *
 * 8 KB keeps ~2.3x headroom over the worst observed use — the same ratio the 16 KB was aiming
 * for, against the real number rather than a guessed one — and hands 8 KB of contiguous DRAM
 * back to the render window. If this ever proves small the symptom is a stack overflow panic,
 * not a silent failure, and net_http_stack_hwm() is still published for re-checking. */
#define NET_TLS_TASK_STACK  8192
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

/* The same GET, reporting the HTTP status as well.
 *
 * WHY A SECOND ENTRY POINT RATHER THAN CHANGING THE FIRST: most callers only need "did it work",
 * and the existing signature is used from several places where a status parameter would be dead
 * weight. This one exists for FR-6's One Call probe, which must tell a 401 ("this key has no One
 * Call subscription" — a NORMAL, expected answer) apart from a transport failure or an oversized
 * response.
 *
 * THE STATUS IS REPORTED EVEN WHEN THE BODY OVERFLOWED `out`, and that combination is the reason
 * this function exists rather than the caller inferring the answer from `err`. `err` merges every
 * failure into one value, so a probe built on it treats an over-long body (ESP_ERR_NO_MEM) as
 * proof that the key is unsubscribed — and then tells the user, on the glass, that official
 * alerts are unavailable on a key that has them. */
esp_err_t net_http_get_json_status(const char *url, const char *bearer,
                                   char *out, size_t outlen, int *status_out);
