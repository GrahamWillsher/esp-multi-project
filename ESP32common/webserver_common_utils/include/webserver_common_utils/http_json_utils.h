#ifndef WEBSERVER_COMMON_UTILS_HTTP_JSON_UTILS_H
#define WEBSERVER_COMMON_UTILS_HTTP_JSON_UTILS_H

/**
 * JSON response pipeline — canonical layer split (Section 4.2):
 *
 * This header is the PRIMITIVE layer:
 *   send_json()       — sets content-type, sends a pre-formed JSON string.
 *   send_json_error() — formats a {"success":false,"message":"..."} envelope.
 *   read_request_body() — reads the HTTP request body into a caller-supplied buffer.
 *
 * Higher-level convenience helpers (ApiResponseUtils) live in the receiver-local
 * lib/webserver/api/api_response_utils.h and delegate here for all actual sends.
 * Do NOT duplicate send_json / send_json_error calls in new handlers; use
 * ApiResponseUtils instead so the full pipeline stays in one place.
 */

#include <esp_err.h>
#include <esp_http_server.h>

namespace HttpJsonUtils {

esp_err_t send_json(httpd_req_t* req, const char* json);
esp_err_t send_json_error(httpd_req_t* req, const char* message);

bool read_request_body(
    httpd_req_t* req,
    char* buffer,
    size_t buffer_size,
    int* out_length,
    const char** out_error_message);

} // namespace HttpJsonUtils

#endif // WEBSERVER_COMMON_UTILS_HTTP_JSON_UTILS_H
