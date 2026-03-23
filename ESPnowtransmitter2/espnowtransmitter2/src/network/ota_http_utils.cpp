// ota_http_utils.cpp
// Defines all free HTTP-helper functions declared in ota_manager_internal.h:
//   – HTTP response builders  (send_json_error, send_json_ok, …)
//   – Request validation      (read_request_body_strict, check_request_content_type, …)
//   – Auth rate-limiting      (is_auth_rate_limited, record_auth_failure, clear_auth_failures)
//   – Parsing / credential    (parse_uint32_strict, header_has_token_ci, is_hex_sha256,
//                               load_ota_psk)
//
// All helpers are internal to the ota_manager compilation unit family.
// Do not call from outside network/ota_manager*.cpp.

#include "ota_manager_internal.h"
#include "../config/logging_config.h"
#include "../config/network_config.h"
#include <webserver_common_utils/ota_auth_utils.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_system.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// Module-private types and data (rate-limit table)
// ---------------------------------------------------------------------------
namespace {

constexpr size_t   OTA_PSK_MIN_LENGTH             = 32;
constexpr const char* OTA_PSK_PLACEHOLDER         = "CHANGE_ME_OTA_PSK_32B_MIN";
constexpr const char* OTA_PSK_NVS_NAMESPACE       = "security";
constexpr const char* OTA_PSK_NVS_KEY             = "ota_psk";

constexpr uint8_t  OTA_AUTH_MAX_FAILURES_PER_WINDOW = 5;
constexpr uint32_t OTA_AUTH_FAILURE_WINDOW_MS       = 60000;
constexpr uint32_t OTA_AUTH_BLOCK_MS                = 120000;
constexpr size_t   OTA_AUTH_RATE_LIMIT_TRACKED_IPS  = 8;

struct OtaAuthRateLimitEntry {
    char     ip[24]              = {0};
    uint8_t  failure_count       = 0;
    uint32_t window_started_ms   = 0;
    uint32_t blocked_until_ms    = 0;
};

OtaAuthRateLimitEntry g_ota_auth_rate_limit[OTA_AUTH_RATE_LIMIT_TRACKED_IPS];

// ---------------------------------------------------------------------------
// Internal helpers (not exposed via ota_manager_internal.h)
// ---------------------------------------------------------------------------

const char* http_status_text(int status_code) {
    switch (status_code) {
        case 200:                             return "200 OK";
        case HTTPD_400_BAD_REQUEST:           return "400 Bad Request";
        case HTTPD_401_UNAUTHORIZED:          return "401 Unauthorized";
        case HTTP_STATUS_REQUEST_TIMEOUT:     return "408 Request Timeout";
        case HTTP_STATUS_PAYLOAD_TOO_LARGE:   return "413 Payload Too Large";
        case HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE: return "415 Unsupported Media Type";
        case HTTP_STATUS_SERVICE_UNAVAILABLE: return "503 Service Unavailable";
        case HTTP_STATUS_TOO_MANY_REQUESTS:   return "429 Too Many Requests";
        case HTTPD_404_NOT_FOUND:             return "404 Not Found";
        case HTTPD_500_INTERNAL_SERVER_ERROR:
        default:                              return "500 Internal Server Error";
    }
}

OtaAuthRateLimitEntry* get_or_create_rate_limit_entry(const char* ip, uint32_t now_ms) {
    if (!ip || ip[0] == '\0') {
        return nullptr;
    }

    OtaAuthRateLimitEntry* first_empty = nullptr;
    OtaAuthRateLimitEntry* oldest_entry = &g_ota_auth_rate_limit[0];

    for (size_t i = 0; i < OTA_AUTH_RATE_LIMIT_TRACKED_IPS; ++i) {
        OtaAuthRateLimitEntry& entry = g_ota_auth_rate_limit[i];

        if (entry.ip[0] == '\0') {
            if (!first_empty) {
                first_empty = &entry;
            }
            continue;
        }

        if (strncmp(entry.ip, ip, sizeof(entry.ip)) == 0) {
            return &entry;
        }

        if (static_cast<uint32_t>(now_ms - entry.window_started_ms) >
            static_cast<uint32_t>(now_ms - oldest_entry->window_started_ms)) {
            oldest_entry = &entry;
        }
    }

    OtaAuthRateLimitEntry* target = first_empty ? first_empty : oldest_entry;
    memset(target, 0, sizeof(*target));
    strncpy(target->ip, ip, sizeof(target->ip) - 1);
    target->window_started_ms = now_ms;
    return target;
}

bool is_placeholder_psk(const char* value) {
    return value && strcmp(value, OTA_PSK_PLACEHOLDER) == 0;
}

void generate_random_psk_hex(char* out, size_t out_len) {
    if (!out || out_len < 65) {
        return;
    }
    // 32 random bytes => 64 hex chars.
    for (size_t i = 0; i < 32; ++i) {
        const uint8_t r = static_cast<uint8_t>(esp_random() & 0xFF);
        (void)snprintf(&out[i * 2], 3, "%02x", r);
    }
    out[64] = '\0';
}

}  // namespace

// ---------------------------------------------------------------------------
// HTTP response builders
// ---------------------------------------------------------------------------

esp_err_t send_json_error(httpd_req_t* req,
                          int status_code,
                          const char* message,
                          const char* details) {
    const char* safe_message = message ? message : "Unknown error";

    StaticJsonDocument<320> doc;
    doc["success"] = false;
    doc["code"]    = status_code;
    doc["message"] = safe_message;
    doc["error"]   = safe_message;  // Backward compatibility for existing clients.
    if (details && details[0] != '\0') {
        doc["details"] = details;
    }

    char response[320];
    const size_t response_len = serializeJson(doc, response, sizeof(response));
    if (response_len == 0 || response_len >= sizeof(response)) {
        httpd_resp_set_status(req, http_status_text(HTTPD_500_INTERNAL_SERVER_ERROR));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_sendstr(req,
            "{\"success\":false,\"code\":500,"
            "\"message\":\"Failed to format error response\","
            "\"error\":\"Failed to format error response\"}");
    }

    httpd_resp_set_status(req, http_status_text(status_code));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, response, response_len);
}

esp_err_t send_json_ok(httpd_req_t* req,
                       const char* message,
                       const char* details) {
    const char* safe_message = message ? message : "OK";

    StaticJsonDocument<256> doc;
    doc["success"] = true;
    doc["code"]    = 200;
    doc["message"] = safe_message;
    if (details && details[0] != '\0') {
        doc["details"] = details;
    }

    char response[256];
    const size_t response_len = serializeJson(doc, response, sizeof(response));
    if (response_len == 0 || response_len >= sizeof(response)) {
        return send_json_error(req,
                               HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Failed to format success response");
    }

    httpd_resp_set_status(req, http_status_text(200));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, response, response_len);
}

esp_err_t reject_unexpected_request_body(httpd_req_t* req, const char* endpoint_name) {
    if (!req || req->content_len <= 0) {
        return ESP_OK;
    }

    char details[96];
    const int details_len = snprintf(details,
                                     sizeof(details),
                                     "Endpoint '%s' does not accept a request body",
                                     endpoint_name ? endpoint_name : "(unknown)");
    if (details_len <= 0 || details_len >= static_cast<int>(sizeof(details))) {
        send_json_error(req, HTTPD_400_BAD_REQUEST, "Request body not supported");
        return ESP_FAIL;
    }

    send_json_error(req, HTTPD_400_BAD_REQUEST, "Request body not supported", details);
    return ESP_FAIL;
}

esp_err_t read_request_body_strict(httpd_req_t* req,
                                   char* buffer,
                                   size_t buffer_size,
                                   size_t* out_len) {
    if (!req || !buffer || buffer_size < 2) {
        return ESP_ERR_INVALID_ARG;
    }

    if (req->content_len <= 0) {
        send_json_error(req, HTTPD_400_BAD_REQUEST, "Request body is required");
        return ESP_FAIL;
    }

    if (req->content_len >= static_cast<int>(buffer_size)) {
        send_json_error(req, HTTP_STATUS_PAYLOAD_TOO_LARGE, "Request body too large");
        return ESP_FAIL;
    }

    int total_read = 0;
    while (total_read < req->content_len) {
        const int recv_len = httpd_req_recv(req,
                                            buffer + total_read,
                                            req->content_len - total_read);
        if (recv_len <= 0) {
            send_json_error(req, HTTPD_400_BAD_REQUEST, "Failed to read request body");
            return ESP_FAIL;
        }
        total_read += recv_len;
    }

    buffer[total_read] = '\0';
    if (out_len) {
        *out_len = static_cast<size_t>(total_read);
    }
    return ESP_OK;
}

// Validate Content-Type against an expected media type token.
// Matching is case-insensitive and accepts optional parameters after ';' or
// whitespace (e.g. "application/json; charset=utf-8").
esp_err_t check_request_content_type(httpd_req_t* req, const char* expected_prefix) {
    if (!req || !expected_prefix) {
        return ESP_ERR_INVALID_ARG;
    }
    char ct_buf[80] = {0};
    if (!OtaAuthUtils::get_header_value(req, "Content-Type", ct_buf, sizeof(ct_buf))) {
        char details[96];
        const int details_len = snprintf(details,
                                         sizeof(details),
                                         "Expected Content-Type: %s",
                                         expected_prefix);
        send_json_error(req,
                        HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE,
                        "Content-Type header is required",
                        (details_len > 0 && details_len < static_cast<int>(sizeof(details)))
                            ? details : nullptr);
        return ESP_FAIL;
    }

    auto matches_media_type = [](const char* value, const char* expected) -> bool {
        if (!value || !expected) { return false; }
        size_t i = 0;
        while (expected[i] != '\0' && value[i] != '\0') {
            const char lhs = static_cast<char>(
                std::tolower(static_cast<unsigned char>(value[i])));
            const char rhs = static_cast<char>(
                std::tolower(static_cast<unsigned char>(expected[i])));
            if (lhs != rhs) { return false; }
            ++i;
        }
        if (expected[i] != '\0') { return false; }
        const char next = value[i];
        return (next == '\0' || next == ';' ||
                std::isspace(static_cast<unsigned char>(next)));
    };

    if (!matches_media_type(ct_buf, expected_prefix)) {
        char details[144];
        const int details_len = snprintf(details, sizeof(details),
                                         "Expected '%s', got '%s'",
                                         expected_prefix, ct_buf);
        send_json_error(req,
                        HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE,
                        "Unsupported Content-Type",
                        (details_len > 0 && details_len < static_cast<int>(sizeof(details)))
                            ? details : nullptr);
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool get_request_client_ip(httpd_req_t* req, char* out, size_t out_len) {
    if (!req || !out || out_len < 2) { return false; }

    const int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0) { return false; }

    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(sockfd,
                    reinterpret_cast<struct sockaddr*>(&addr),
                    &addr_len) != 0) {
        return false;
    }

    if (addr.ss_family == AF_INET) {
        const auto* v4 = reinterpret_cast<const struct sockaddr_in*>(&addr);
        return inet_ntoa_r(v4->sin_addr, out, out_len) != nullptr;
    }

    // IPv6 textual conversion is not required for current deployment.
    return false;
}

// ---------------------------------------------------------------------------
// Auth rate-limiting
// ---------------------------------------------------------------------------

bool is_auth_rate_limited(const char* ip, uint32_t now_ms, uint32_t* retry_after_sec) {
    OtaAuthRateLimitEntry* entry = get_or_create_rate_limit_entry(ip, now_ms);
    if (!entry) { return false; }

    if (entry->blocked_until_ms == 0) { return false; }

    const int32_t remaining_ms =
        static_cast<int32_t>(entry->blocked_until_ms - now_ms);
    if (remaining_ms <= 0) {
        entry->blocked_until_ms  = 0;
        entry->failure_count     = 0;
        entry->window_started_ms = now_ms;
        return false;
    }

    if (retry_after_sec) {
        *retry_after_sec = static_cast<uint32_t>((remaining_ms + 999) / 1000);
    }
    return true;
}

void record_auth_failure(const char* ip, uint32_t now_ms) {
    OtaAuthRateLimitEntry* entry = get_or_create_rate_limit_entry(ip, now_ms);
    if (!entry) { return; }

    if (entry->blocked_until_ms != 0 &&
        static_cast<int32_t>(entry->blocked_until_ms - now_ms) > 0) {
        return;
    }

    if (static_cast<uint32_t>(now_ms - entry->window_started_ms) >
        OTA_AUTH_FAILURE_WINDOW_MS) {
        entry->window_started_ms = now_ms;
        entry->failure_count     = 0;
    }

    if (entry->failure_count < 255) {
        ++entry->failure_count;
    }

    if (entry->failure_count >= OTA_AUTH_MAX_FAILURES_PER_WINDOW) {
        entry->blocked_until_ms = now_ms + OTA_AUTH_BLOCK_MS;
    }
}

void clear_auth_failures(const char* ip, uint32_t now_ms) {
    OtaAuthRateLimitEntry* entry = get_or_create_rate_limit_entry(ip, now_ms);
    if (!entry) { return; }
    entry->failure_count     = 0;
    entry->window_started_ms = now_ms;
    entry->blocked_until_ms  = 0;
}

// ---------------------------------------------------------------------------
// Parsing / credential helpers
// ---------------------------------------------------------------------------

bool parse_uint32_strict(const char* value, uint32_t* out) {
    if (!value || !out || value[0] == '\0') { return false; }

    uint64_t parsed = 0;
    for (size_t i = 0; value[i] != '\0'; ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (!std::isdigit(ch)) { return false; }
        parsed = (parsed * 10ULL) + static_cast<uint64_t>(ch - '0');
        if (parsed > 0xFFFFFFFFULL) { return false; }
    }

    *out = static_cast<uint32_t>(parsed);
    return true;
}

bool header_has_token_ci(const char* header_value, const char* expected_token) {
    if (!header_value || !expected_token || expected_token[0] == '\0') {
        return false;
    }

    size_t token_len = 0;
    while (expected_token[token_len] != '\0') { ++token_len; }

    const char* p = header_value;
    while (*p != '\0') {
        while (*p != '\0' &&
               (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) {
            ++p;
        }
        if (*p == '\0') { break; }

        const char* start = p;
        while (*p != '\0' && *p != ',') { ++p; }
        const char* end = p;
        while (end > start &&
               std::isspace(static_cast<unsigned char>(*(end - 1)))) {
            --end;
        }

        const size_t len = static_cast<size_t>(end - start);
        if (len == token_len) {
            bool match = true;
            for (size_t i = 0; i < token_len; ++i) {
                const char lhs = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(start[i])));
                const char rhs = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(expected_token[i])));
                if (lhs != rhs) { match = false; break; }
            }
            if (match) { return true; }
        }
    }

    return false;
}

bool is_hex_sha256(const char* value) {
    if (!value) { return false; }

    size_t len = 0;
    while (value[len] != '\0') {
        const char ch = value[len];
        const bool is_hex = (ch >= '0' && ch <= '9') ||
                            (ch >= 'a' && ch <= 'f') ||
                            (ch >= 'A' && ch <= 'F');
        if (!is_hex) { return false; }
        ++len;
    }
    return len == OTA_IMAGE_SHA256_HEX_LEN;
}

bool load_ota_psk(char* out_psk, size_t out_psk_len, bool* out_is_provisioned) {
    if (!out_psk || out_psk_len < 2) { return false; }

    out_psk[0] = '\0';
    if (out_is_provisioned) { *out_is_provisioned = false; }

    Preferences prefs;
    if (!prefs.begin(OTA_PSK_NVS_NAMESPACE, false)) { return false; }

    const String nvs_psk = prefs.getString(OTA_PSK_NVS_KEY, "");
    if (nvs_psk.length() >= OTA_PSK_MIN_LENGTH) {
        strlcpy(out_psk, nvs_psk.c_str(), out_psk_len);
        if (out_is_provisioned) { *out_is_provisioned = true; }
        prefs.end();
        return true;
    }

    // If a strong build-time PSK is configured, persist it once into NVS.
    if (config::security::OTA_PSK &&
        strlen(config::security::OTA_PSK) >= OTA_PSK_MIN_LENGTH &&
        !is_placeholder_psk(config::security::OTA_PSK)) {
        prefs.putString(OTA_PSK_NVS_KEY, config::security::OTA_PSK);
        strlcpy(out_psk, config::security::OTA_PSK, out_psk_len);
        if (out_is_provisioned) { *out_is_provisioned = true; }
        prefs.end();
        return true;
    }

    // First-boot auto-provision: generate a per-device random PSK and persist.
    char generated_psk[65] = {0};
    generate_random_psk_hex(generated_psk, sizeof(generated_psk));
    if (generated_psk[0] == '\0') {
        prefs.end();
        return false;
    }

    if (prefs.putString(OTA_PSK_NVS_KEY, generated_psk) == 0) {
        prefs.end();
        return false;
    }

    strlcpy(out_psk, generated_psk, out_psk_len);
    if (out_is_provisioned) { *out_is_provisioned = true; }
    LOG_WARN("HTTP_OTA", "Auto-provisioned OTA PSK in NVS namespace '%s'",
             OTA_PSK_NVS_NAMESPACE);
    prefs.end();
    return true;
}
