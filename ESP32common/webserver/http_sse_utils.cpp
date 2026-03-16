#include <esp32common/webserver/http_sse_utils.h>

#include <cstdio>
#include <cstring>

namespace HttpSseUtils {

esp_err_t begin_sse(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    return ESP_OK;
}

esp_err_t send_retry_hint(httpd_req_t* req, uint32_t retry_ms) {
    char hint[32];
    snprintf(hint, sizeof(hint), "retry: %lu\n\n", static_cast<unsigned long>(retry_ms));
    return httpd_resp_send_chunk(req, hint, strlen(hint));
}

esp_err_t send_ping(httpd_req_t* req) {
    static const char* kPing = ": ping\n\n";
    return httpd_resp_send_chunk(req, kPing, strlen(kPing));
}

esp_err_t end_sse(httpd_req_t* req) {
    return httpd_resp_send_chunk(req, nullptr, 0);
}

} // namespace HttpSseUtils
