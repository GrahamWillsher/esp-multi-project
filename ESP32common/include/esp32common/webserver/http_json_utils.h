#ifndef ESP32COMMON_WEBSERVER_HTTP_JSON_UTILS_H
#define ESP32COMMON_WEBSERVER_HTTP_JSON_UTILS_H

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

#endif // ESP32COMMON_WEBSERVER_HTTP_JSON_UTILS_H
