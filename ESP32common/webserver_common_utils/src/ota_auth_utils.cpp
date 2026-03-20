#include <webserver_common_utils/ota_auth_utils.h>

#include <esp_system.h>
#include <mbedtls/md.h>

#include <cstdio>
#include <cstring>

namespace OtaAuthUtils {

bool get_header_value(httpd_req_t* req, const char* key, char* out, size_t out_len) {
    if (!req || !key || !out || out_len == 0) {
        return false;
    }

    if (httpd_req_get_hdr_value_str(req, key, out, out_len) != ESP_OK) {
        return false;
    }

    out[out_len - 1] = '\0';
    return true;
}

void random_hex(char* out, size_t bytes) {
    static const char* kHexChars = "0123456789abcdef";
    if (!out || bytes == 0) {
        return;
    }

    for (size_t i = 0; i < bytes; ++i) {
        uint8_t value = 0;
        esp_fill_random(&value, 1);
        out[i * 2] = kHexChars[(value >> 4) & 0x0F];
        out[i * 2 + 1] = kHexChars[value & 0x0F];
    }

    out[bytes * 2] = '\0';
}

bool constant_time_equals(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }

    const size_t a_len = strlen(a);
    const size_t b_len = strlen(b);
    if (a_len != b_len) {
        return false;
    }

    uint8_t diff = 0;
    for (size_t i = 0; i < a_len; ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }

    return diff == 0;
}

bool compute_hmac_sha256_hex(const char* key,
                             const char* message,
                             char* out_hex,
                             size_t out_len) {
    if (!key || !message || !out_hex || out_len < 65) {
        return false;
    }

    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        return false;
    }

    unsigned char digest[32] = {0};
    const int result = mbedtls_md_hmac(md_info,
                                       reinterpret_cast<const unsigned char*>(key),
                                       strlen(key),
                                       reinterpret_cast<const unsigned char*>(message),
                                       strlen(message),
                                       digest);
    if (result != 0) {
        return false;
    }

    for (size_t i = 0; i < sizeof(digest); ++i) {
        snprintf(&out_hex[i * 2], out_len - (i * 2), "%02x", digest[i]);
    }
    out_hex[64] = '\0';
    return true;
}

} // namespace OtaAuthUtils
