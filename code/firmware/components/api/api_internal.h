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
