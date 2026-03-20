#ifndef WEBSERVER_COMMON_UTILS_OTA_AUTH_UTILS_H
#define WEBSERVER_COMMON_UTILS_OTA_AUTH_UTILS_H

#include <stddef.h>
#include <esp_http_server.h>

namespace OtaAuthUtils {

bool get_header_value(httpd_req_t* req, const char* key, char* out, size_t out_len);

void random_hex(char* out, size_t bytes);

bool constant_time_equals(const char* a, const char* b);

bool compute_hmac_sha256_hex(const char* key,
                             const char* message,
                             char* out_hex,
                             size_t out_len);

} // namespace OtaAuthUtils

#endif // WEBSERVER_COMMON_UTILS_OTA_AUTH_UTILS_H
