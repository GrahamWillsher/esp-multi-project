#include "api_response_utils.h"

#include "../utils/http_json_utils.h"

#include <cstdarg>
#include <cstdio>

namespace ApiResponseUtils {

esp_err_t send_jsonf(httpd_req_t* req, const char* format, ...) {
    char json[512];
    va_list args;
    va_start(args, format);
    vsnprintf(json, sizeof(json), format, args);
    va_end(args);
    return HttpJsonUtils::send_json(req, json);
}

esp_err_t send_json_no_cache(httpd_req_t* req, const char* json) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    return HttpJsonUtils::send_json(req, json);
}

esp_err_t send_success_message(httpd_req_t* req, const char* message) {
    return send_jsonf(req, "{\"success\":true,\"message\":\"%s\"}", message);
}

esp_err_t send_error_message(httpd_req_t* req, const char* message) {
    return HttpJsonUtils::send_json_error(req, message);
}

esp_err_t send_error_with_status(httpd_req_t* req, const char* status, const char* message) {
    httpd_resp_set_status(req, status);
    return send_error_message(req, message);
}

} // namespace ApiResponseUtils
