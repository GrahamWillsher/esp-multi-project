#ifndef API_RESPONSE_UTILS_H
#define API_RESPONSE_UTILS_H

#include <esp_http_server.h>
#include <ArduinoJson.h>
#include <cstdint>

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

// Serialise an ArduinoJson document to JSON and send it as the HTTP response.
// Use this for all structured responses with nested objects, arrays, or more
// than three fields — avoids raw snprintf JSON assembly.
esp_err_t send_json_doc(httpd_req_t* req, JsonDocument& doc);

// Convenience: set doc["success"] = true then send_json_doc.
esp_err_t send_success_doc(httpd_req_t* req, JsonDocument& doc);

// Format a 4-byte IPv4 array as "d.d.d.d" into buf (must be >= 16 bytes).
void format_ipv4(char* buf, const uint8_t ip[4]);

// Safely escape double-quotes to single quotes in a string.
// Copies at most max_len-1 characters (plus null terminator) from src to dst.
void escape_double_quotes(const char* src, char* dst, size_t max_len);

// Send a standard ESP-NOW send result: success message on ESP_OK, error with
// esp_err_to_name string otherwise.  success_msg is used on the OK path.
esp_err_t send_espnow_send_result(httpd_req_t* req,
                                  esp_err_t espnow_result,
                                  const char* success_msg);

} // namespace ApiResponseUtils

#endif // API_RESPONSE_UTILS_H
