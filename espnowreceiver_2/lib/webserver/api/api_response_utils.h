#ifndef API_RESPONSE_UTILS_H
#define API_RESPONSE_UTILS_H

#include <esp_http_server.h>

namespace ApiResponseUtils {

esp_err_t send_jsonf(httpd_req_t* req, const char* format, ...);
esp_err_t send_json_no_cache(httpd_req_t* req, const char* json);
esp_err_t send_success_message(httpd_req_t* req, const char* message);
esp_err_t send_error_message(httpd_req_t* req, const char* message);
esp_err_t send_error_with_status(httpd_req_t* req, const char* status, const char* message);

} // namespace ApiResponseUtils

#endif // API_RESPONSE_UTILS_H
