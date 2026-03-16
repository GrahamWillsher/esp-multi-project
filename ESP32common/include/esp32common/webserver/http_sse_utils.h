#ifndef ESP32COMMON_WEBSERVER_HTTP_SSE_UTILS_H
#define ESP32COMMON_WEBSERVER_HTTP_SSE_UTILS_H

#include <esp_err.h>
#include <esp_http_server.h>

namespace HttpSseUtils {

esp_err_t begin_sse(httpd_req_t* req);
esp_err_t send_retry_hint(httpd_req_t* req, uint32_t retry_ms = 5000);
esp_err_t send_ping(httpd_req_t* req);
esp_err_t end_sse(httpd_req_t* req);

} // namespace HttpSseUtils

#endif // ESP32COMMON_WEBSERVER_HTTP_SSE_UTILS_H
