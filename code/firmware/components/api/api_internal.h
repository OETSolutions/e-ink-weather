#pragma once

#include "esp_http_server.h"

/* Helpers shared between the API's translation units. Not a public interface — nothing
 * outside components/api should include this, which is why it lives beside the .c files
 * rather than in include/. */

/* Send a JSON body with the given HTTP status line (e.g. "400 Bad Request").
 * The body is sent in one write and is never cached. */
esp_err_t api_send_json(httpd_req_t *req, const char *body, const char *status);

/* Send `{"error": "<msg>"}` with the given status. The message is escaped with cJSON, so a
 * parser message containing quotes or backslashes cannot produce an unparseable body. */
esp_err_t api_send_err(httpd_req_t *req, const char *status, const char *msg);

/* Read the whole request body into `buf` (NUL-terminated), bounded by `cap`.
 * Returns the length, or -1 on failure — in which case it has ALREADY sent the error
 * response, so the caller must not send another. */
int api_read_body(httpd_req_t *req, char *buf, size_t cap);

/* Record an error for /api/status. */
void api_note_error(const char *msg);

/* Optional bearer-token authentication (FR-31), implemented in api_server.c.
 *
 * `api_auth_gate` is what a mutating handler calls first: it returns 1 when the request must
 * be refused, in which case it has ALREADY sent the 401 and the handler must return without
 * touching anything. Returns 0 when the request may proceed — which includes the whole
 * "auth is off" case, so a handler needs no branch of its own.
 *
 * It lives here because api_ota.c lives in another translation unit but is the endpoint that
 * matters most: it installs firmware from a client-supplied URL, so leaving it ungated while
 * the other three were gated would have been the entire vulnerability, unprotected. */
int api_auth_gate(httpd_req_t *req);

/* Re-read the enabled flag and token from NVS. Called when they change (the settings
 * endpoint) and at server start. */
void api_auth_reload(void);

/* 1 when a token is set AND the owner enabled the check — i.e. when requests are actually
 * being authenticated. */
int api_auth_enabled(void);

/* The stored token, or "" when unset. For the settings endpoint to show the owner. */
const char *api_auth_token(void);
