#ifndef API_RESPONSE_UTILS_H
#define API_RESPONSE_UTILS_H

#include <esp_http_server.h>

namespace ApiResponseUtils {

// JSON response policy for this webserver codebase:
// 1) Prefer StaticJsonDocument + serializeJson for structured responses,
//    nested objects/arrays, or responses with more than three fields.
// 2) Reserve send_jsonf/snprintf-style formatting for very small flat responses
//    (typically success/message or similarly tiny payloads).
// 3) Avoid DynamicJsonDocument for small fixed-shape payloads.

esp_err_t send_success(httpd_req_t* req);
esp_err_t send_jsonf(httpd_req_t* req, const char* format, ...);
esp_err_t send_json_no_cache(httpd_req_t* req, const char* json);
esp_err_t send_success_message(httpd_req_t* req, const char* message);
esp_err_t send_error_message(httpd_req_t* req, const char* message);
esp_err_t send_json_parse_error(httpd_req_t* req);
esp_err_t send_transmitter_mac_unknown(httpd_req_t* req);
esp_err_t send_error_with_status(httpd_req_t* req, const char* status, const char* message);

} // namespace ApiResponseUtils

#endif // API_RESPONSE_UTILS_H
