#include "ota_manager.h"
#include "ethernet_manager.h"
#include "mqtt_manager.h"
#include "../config/logging_config.h"
#include <ETH.h>
#include "../config/network_config.h"
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/connection_manager.h>
#include <webserver_common_utils/ota_auth_utils.h>
#include "../test_data/test_data_config.h"
#include <Arduino.h>
#include <Update.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <firmware_metadata.h>
#include <firmware_version.h>
#include <ArduinoJson.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cctype>

OtaManager& OtaManager::instance() {
    static OtaManager instance;
    return instance;
}

namespace {
constexpr int HTTP_STATUS_PAYLOAD_TOO_LARGE = 413;
constexpr int HTTP_STATUS_REQUEST_TIMEOUT = 408;
constexpr int HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415;
constexpr int HTTP_STATUS_TOO_MANY_REQUESTS = 429;
constexpr int OTA_UPLOAD_MAX_RECV_TIMEOUTS = 60;

constexpr uint8_t OTA_AUTH_MAX_FAILURES_PER_WINDOW = 5;
constexpr uint32_t OTA_AUTH_FAILURE_WINDOW_MS = 60000;
constexpr uint32_t OTA_AUTH_BLOCK_MS = 120000;
constexpr size_t OTA_AUTH_RATE_LIMIT_TRACKED_IPS = 8;

struct OtaAuthRateLimitEntry {
    char ip[24] = {0};
    uint8_t failure_count = 0;
    uint32_t window_started_ms = 0;
    uint32_t blocked_until_ms = 0;
};

OtaAuthRateLimitEntry g_ota_auth_rate_limit[OTA_AUTH_RATE_LIMIT_TRACKED_IPS];

const char* http_status_text(int status_code) {
    switch (status_code) {
        case 200:
            return "200 OK";
        case HTTPD_400_BAD_REQUEST:
            return "400 Bad Request";
        case HTTPD_401_UNAUTHORIZED:
            return "401 Unauthorized";
        case HTTP_STATUS_REQUEST_TIMEOUT:
            return "408 Request Timeout";
        case HTTP_STATUS_PAYLOAD_TOO_LARGE:
            return "413 Payload Too Large";
        case HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE:
            return "415 Unsupported Media Type";
        case HTTP_STATUS_TOO_MANY_REQUESTS:
            return "429 Too Many Requests";
        case HTTPD_404_NOT_FOUND:
            return "404 Not Found";
        case HTTPD_500_INTERNAL_SERVER_ERROR:
        default:
            return "500 Internal Server Error";
    }
}

esp_err_t send_json_error(httpd_req_t* req,
                          int status_code,
                          const char* message,
                          const char* details = nullptr) {
    const char* safe_message = message ? message : "Unknown error";

    StaticJsonDocument<320> doc;
    doc["success"] = false;
    doc["code"] = status_code;
    doc["message"] = safe_message;
    doc["error"] = safe_message;  // Backward compatibility for existing clients.
    if (details && details[0] != '\0') {
        doc["details"] = details;
    }

    char response[320];
    const size_t response_len = serializeJson(doc, response, sizeof(response));
    if (response_len == 0 || response_len >= sizeof(response)) {
        httpd_resp_set_status(req, http_status_text(HTTPD_500_INTERNAL_SERVER_ERROR));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_sendstr(req, "{\"success\":false,\"code\":500,\"message\":\"Failed to format error response\",\"error\":\"Failed to format error response\"}");
    }

    httpd_resp_set_status(req, http_status_text(status_code));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, response, response_len);
}

esp_err_t send_json_ok(httpd_req_t* req,
                       const char* message,
                       const char* details = nullptr) {
    const char* safe_message = message ? message : "OK";

    StaticJsonDocument<256> doc;
    doc["success"] = true;
    doc["code"] = 200;
    doc["message"] = safe_message;
    if (details && details[0] != '\0') {
        doc["details"] = details;
    }

    char response[256];
    const size_t response_len = serializeJson(doc, response, sizeof(response));
    if (response_len == 0 || response_len >= sizeof(response)) {
        return send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to format success response");
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
                                   size_t* out_len = nullptr) {
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
        const int recv_len = httpd_req_recv(req, buffer + total_read, req->content_len - total_read);
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
                            ? details
                            : nullptr);
        return ESP_FAIL;
    }
    auto matches_media_type = [](const char* value, const char* expected) -> bool {
        if (!value || !expected) {
            return false;
        }

        size_t i = 0;
        while (expected[i] != '\0' && value[i] != '\0') {
            const char lhs = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
            const char rhs = static_cast<char>(std::tolower(static_cast<unsigned char>(expected[i])));
            if (lhs != rhs) {
                return false;
            }
            ++i;
        }

        if (expected[i] != '\0') {
            return false;
        }

        const char next = value[i];
        return (next == '\0' || next == ';' || std::isspace(static_cast<unsigned char>(next)));
    };

    if (!matches_media_type(ct_buf, expected_prefix)) {
        char details[144];
        const int details_len = snprintf(details,
                                         sizeof(details),
                                         "Expected '%s', got '%s'",
                                         expected_prefix,
                                         ct_buf);
        send_json_error(req,
                        HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE,
                        "Unsupported Content-Type",
                        (details_len > 0 && details_len < static_cast<int>(sizeof(details)))
                            ? details
                            : nullptr);
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool get_request_client_ip(httpd_req_t* req, char* out, size_t out_len) {
    if (!req || !out || out_len < 2) {
        return false;
    }

    const int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0) {
        return false;
    }

    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(sockfd, reinterpret_cast<struct sockaddr*>(&addr), &addr_len) != 0) {
        return false;
    }

    if (addr.ss_family == AF_INET) {
        const auto* v4 = reinterpret_cast<const struct sockaddr_in*>(&addr);
        return inet_ntoa_r(v4->sin_addr, out, out_len) != nullptr;
    }

    // IPv6 textual conversion is not required for current deployment.
    return false;
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

bool is_auth_rate_limited(const char* ip, uint32_t now_ms, uint32_t* retry_after_sec) {
    OtaAuthRateLimitEntry* entry = get_or_create_rate_limit_entry(ip, now_ms);
    if (!entry) {
        return false;
    }

    if (entry->blocked_until_ms == 0) {
        return false;
    }

    const int32_t remaining_ms = static_cast<int32_t>(entry->blocked_until_ms - now_ms);
    if (remaining_ms <= 0) {
        entry->blocked_until_ms = 0;
        entry->failure_count = 0;
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
    if (!entry) {
        return;
    }

    if (entry->blocked_until_ms != 0 && static_cast<int32_t>(entry->blocked_until_ms - now_ms) > 0) {
        return;
    }

    if (static_cast<uint32_t>(now_ms - entry->window_started_ms) > OTA_AUTH_FAILURE_WINDOW_MS) {
        entry->window_started_ms = now_ms;
        entry->failure_count = 0;
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
    if (!entry) {
        return;
    }

    entry->failure_count = 0;
    entry->window_started_ms = now_ms;
    entry->blocked_until_ms = 0;
}

bool parse_uint32_strict(const char* value, uint32_t* out) {
    if (!value || !out || value[0] == '\0') {
        return false;
    }

    uint64_t parsed = 0;
    for (size_t i = 0; value[i] != '\0'; ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (!std::isdigit(ch)) {
            return false;
        }

        parsed = (parsed * 10ULL) + static_cast<uint64_t>(ch - '0');
        if (parsed > 0xFFFFFFFFULL) {
            return false;
        }
    }

    *out = static_cast<uint32_t>(parsed);
    return true;
}

bool header_has_token_ci(const char* header_value, const char* expected_token) {
    if (!header_value || !expected_token || expected_token[0] == '\0') {
        return false;
    }

    size_t token_len = 0;
    while (expected_token[token_len] != '\0') {
        ++token_len;
    }

    const char* p = header_value;
    while (*p != '\0') {
        while (*p != '\0' && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) {
            ++p;
        }
        if (*p == '\0') {
            break;
        }

        const char* start = p;
        while (*p != '\0' && *p != ',') {
            ++p;
        }

        const char* end = p;
        while (end > start && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
            --end;
        }

        const size_t len = static_cast<size_t>(end - start);
        if (len == token_len) {
            bool match = true;
            for (size_t i = 0; i < token_len; ++i) {
                const char lhs = static_cast<char>(std::tolower(static_cast<unsigned char>(start[i])));
                const char rhs = static_cast<char>(std::tolower(static_cast<unsigned char>(expected_token[i])));
                if (lhs != rhs) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
    }

    return false;
}
}

bool OtaManager::arm_ota_session() {
    ota_session_.arm(
        millis(),
        config::security::OTA_SESSION_TTL_MS,
        config::security::OTA_SESSION_MAX_ATTEMPTS,
        nullptr);

    LOG_INFO("HTTP_OTA", "Armed OTA session id=%s expires_in_ms=%lu attempts=%u",
             ota_session_.session_id(),
             static_cast<unsigned long>(config::security::OTA_SESSION_TTL_MS),
             ota_session_.attempts_remaining());
    return true;
}

bool OtaManager::arm_ota_session_from_control_plane(const uint8_t* requester_mac) {
    const uint32_t now_ms = millis();

    // Compatibility/safety: OTA_START may be delivered more than once.
    // Do not rotate session credentials while a still-valid unconsumed
    // session exists, otherwise a receiver holding the previous challenge
    // will fail with "Invalid OTA session" during upload.
    if (ota_session_.is_active() && !ota_session_.is_consumed()) {
        const uint32_t expires_at = ota_session_.expires_at_ms();
        if (expires_at > now_ms) {
            LOG_WARN("HTTP_OTA", "Control-plane OTA arm ignored: active session id=%s expires_in_ms=%lu attempts=%u",
                     ota_session_.session_id(),
                     static_cast<unsigned long>(expires_at - now_ms),
                     ota_session_.attempts_remaining());
            return true;
        }
    }

    ota_session_.arm(
        now_ms,
        config::security::OTA_SESSION_TTL_MS,
        config::security::OTA_SESSION_MAX_ATTEMPTS,
        requester_mac);

    LOG_INFO("HTTP_OTA", "Armed OTA session id=%s expires_in_ms=%lu attempts=%u",
             ota_session_.session_id(),
             static_cast<unsigned long>(config::security::OTA_SESSION_TTL_MS),
             ota_session_.attempts_remaining());
    return true;
}

bool OtaManager::validate_ota_auth_headers(httpd_req_t* req) {
    char client_ip[24] = "unknown";
    const bool has_client_ip = get_request_client_ip(req, client_ip, sizeof(client_ip));
    const uint32_t now_ms = millis();

    uint32_t retry_after_sec = 0;
    if (has_client_ip && is_auth_rate_limited(client_ip, now_ms, &retry_after_sec)) {
        char retry_after[16] = {0};
        (void)snprintf(retry_after, sizeof(retry_after), "%lu", static_cast<unsigned long>(retry_after_sec));
        httpd_resp_set_hdr(req, "Retry-After", retry_after);

        char details[96] = {0};
        (void)snprintf(details,
                       sizeof(details),
                       "Too many failed attempts. Retry after %lu seconds",
                       static_cast<unsigned long>(retry_after_sec));
        LOG_WARN("HTTP_OTA", "OTA auth temporarily rate-limited for %s (retry_after=%lus)",
                 client_ip,
                 static_cast<unsigned long>(retry_after_sec));
        send_json_error(req, HTTP_STATUS_TOO_MANY_REQUESTS, "Too many OTA auth failures", details);
        return false;
    }

    char session_id[40] = {0};
    char nonce[40] = {0};
    char expires_str[24] = {0};
    char signature[80] = {0};

    if (!OtaAuthUtils::get_header_value(req, "X-OTA-Session", session_id, sizeof(session_id)) ||
        !OtaAuthUtils::get_header_value(req, "X-OTA-Nonce", nonce, sizeof(nonce)) ||
        !OtaAuthUtils::get_header_value(req, "X-OTA-Expires", expires_str, sizeof(expires_str)) ||
        !OtaAuthUtils::get_header_value(req, "X-OTA-Signature", signature, sizeof(signature))) {
        if (has_client_ip) {
            record_auth_failure(client_ip, now_ms);
        }
        LOG_WARN("HTTP_OTA", "Missing OTA auth headers from %s", client_ip);
        send_json_error(req, HTTPD_401_UNAUTHORIZED, "Missing OTA auth headers");
        return false;
    }

    uint32_t expires = 0;
    if (!parse_uint32_strict(expires_str, &expires)) {
        if (has_client_ip) {
            record_auth_failure(client_ip, now_ms);
        }
        send_json_error(req,
                        HTTPD_401_UNAUTHORIZED,
                        "Invalid OTA auth headers",
                        "X-OTA-Expires must be an unsigned integer");
        return false;
    }

    const OtaSessionUtils::ValidationResult result = ota_session_.validate_and_consume(
        session_id,
        nonce,
        expires,
        signature,
        config::security::OTA_PSK,
        static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFFUL),
        now_ms);

    switch (result) {
        case OtaSessionUtils::ValidationResult::Valid:
            if (has_client_ip) {
                clear_auth_failures(client_ip, now_ms);
            }
            // Keep this path lightweight: avoid formatted logger call on httpd task
            // stack immediately before large OTA upload handling.
            Serial.println("[HTTP_OTA] OTA auth validated");
            return true;
        case OtaSessionUtils::ValidationResult::NotArmed:
            if (has_client_ip) {
                record_auth_failure(client_ip, now_ms);
            }
            send_json_error(req, HTTPD_401_UNAUTHORIZED, "OTA session not armed");
            return false;
        case OtaSessionUtils::ValidationResult::Consumed:
            if (has_client_ip) {
                record_auth_failure(client_ip, now_ms);
            }
            send_json_error(req, HTTPD_401_UNAUTHORIZED, "OTA session already consumed");
            return false;
        case OtaSessionUtils::ValidationResult::Expired:
            if (has_client_ip) {
                record_auth_failure(client_ip, now_ms);
            }
            send_json_error(req, HTTPD_401_UNAUTHORIZED, "OTA session expired");
            return false;
        case OtaSessionUtils::ValidationResult::Locked:
            if (has_client_ip) {
                record_auth_failure(client_ip, now_ms);
            }
            send_json_error(req, HTTPD_401_UNAUTHORIZED, "OTA session locked");
            return false;
        case OtaSessionUtils::ValidationResult::InvalidSession:
            if (has_client_ip) {
                record_auth_failure(client_ip, now_ms);
            }
            LOG_WARN("HTTP_OTA", "OTA session mismatch from %s, attempts_remaining=%u", client_ip, ota_session_.attempts_remaining());
            {
                char details[96] = {0};
                (void)snprintf(details,
                               sizeof(details),
                               "Session invalid. Attempts remaining: %u",
                               ota_session_.attempts_remaining());
                send_json_error(req, HTTPD_401_UNAUTHORIZED, "Invalid OTA session", details);
            }
            return false;
        case OtaSessionUtils::ValidationResult::InvalidSignature:
            if (has_client_ip) {
                record_auth_failure(client_ip, now_ms);
            }
            LOG_WARN("HTTP_OTA", "OTA signature mismatch from %s, attempts_remaining=%u", client_ip, ota_session_.attempts_remaining());
            {
                char details[96] = {0};
                (void)snprintf(details,
                               sizeof(details),
                               "Signature invalid. Attempts remaining: %u",
                               ota_session_.attempts_remaining());
                send_json_error(req, HTTPD_401_UNAUTHORIZED, "Invalid OTA signature", details);
            }
            return false;
        case OtaSessionUtils::ValidationResult::InternalError:
        default:
            send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to verify OTA signature");
            return false;
    }
}

esp_err_t OtaManager::ota_arm_handler(httpd_req_t *req) {
    auto& mgr = instance();

    char client_ip[24] = "unknown";
    (void)get_request_client_ip(req, client_ip, sizeof(client_ip));

    // Compatibility: accept either no body or an explicit empty JSON object ({}).
    // Some HTTP clients send a minimal POST payload when calling OTA arm.
    if (req->content_len > 0) {
        char body[40];
        size_t body_len = 0;
        if (read_request_body_strict(req, body, sizeof(body), &body_len) != ESP_OK) {
            return ESP_FAIL;
        }

        StaticJsonDocument<32> body_doc;
        DeserializationError body_err = deserializeJson(body_doc, body, body_len);
        const bool allow_empty_object =
            (!body_err && body_doc.is<JsonObject>() && body_doc.as<JsonObject>().size() == 0);
        if (!allow_empty_object) {
            send_json_error(req,
                            HTTPD_400_BAD_REQUEST,
                            "Request body not supported",
                            "Endpoint '/api/ota_arm' accepts no body or an empty JSON object");
            return ESP_FAIL;
        }
    }

    if (!mgr.arm_ota_session()) {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to arm OTA session");
        return ESP_FAIL;
    }

    char sig[65] = {0};
    if (!mgr.ota_session_.compute_signature(
            config::security::OTA_PSK,
            static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFFUL),
            sig,
            sizeof(sig))) {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to derive OTA signature");
        return ESP_FAIL;
    }

    LOG_INFO("HTTP_OTA", "OTA session armed from %s, session_id=%s expires_at_ms=%lu attempts=%u",
             client_ip,
             mgr.ota_session_.session_id(),
             static_cast<unsigned long>(mgr.ota_session_.expires_at_ms()),
             mgr.ota_session_.attempts_remaining());

    StaticJsonDocument<320> doc;
    doc["success"] = true;
    doc["session_id"] = mgr.ota_session_.session_id();
    doc["nonce"] = mgr.ota_session_.nonce();
    doc["expires_at_ms"] = static_cast<unsigned long>(mgr.ota_session_.expires_at_ms());
    doc["signature"] = sig;
    doc["attempts_remaining"] = mgr.ota_session_.attempts_remaining();

    char response[320];
    const size_t response_len = serializeJson(doc, response, sizeof(response));
    if (response_len == 0 || response_len >= sizeof(response)) {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to format OTA arm response");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, response, response_len);
    return ESP_OK;
}

esp_err_t OtaManager::ota_upload_handler(httpd_req_t *req) {
    auto& mgr = instance();

    char client_ip[24] = "unknown";
    (void)get_request_client_ip(req, client_ip, sizeof(client_ip));

    if (!mgr.validate_ota_auth_headers(req)) {
        LOG_WARN("HTTP_OTA", "OTA upload rejected during auth from %s", client_ip);
        return ESP_FAIL;
    }

    // Reject chunked Transfer-Encoding: OTA upload requires Content-Length for
    // pre-flight partition size validation and bounded receive loop termination.
    {
        char te_buf[32] = {0};
        if (OtaAuthUtils::get_header_value(req, "Transfer-Encoding", te_buf, sizeof(te_buf))) {
            const bool is_chunked = header_has_token_ci(te_buf, "chunked");
            if (is_chunked) {
                send_json_error(req, HTTPD_400_BAD_REQUEST,
                                "Chunked transfer encoding not supported; use Content-Length");
                return ESP_FAIL;
            }
        }
    }

    if (check_request_content_type(req, "application/octet-stream") != ESP_OK) {
        return ESP_FAIL;
    }

    if (req->content_len <= 0) {
        send_json_error(req, HTTPD_400_BAD_REQUEST, "Firmware payload is required");
        return ESP_FAIL;
    }

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (update_partition != nullptr &&
        static_cast<size_t>(req->content_len) > static_cast<size_t>(update_partition->size)) {
        LOG_WARN("HTTP_OTA", "Upload rejected: payload=%u exceeds update partition=%u",
                 static_cast<unsigned>(req->content_len),
                 static_cast<unsigned>(update_partition->size));
        send_json_error(req, HTTP_STATUS_PAYLOAD_TOO_LARGE, "Firmware image too large for update partition");
        return ESP_FAIL;
    }

    constexpr size_t kOtaChunkSize = 1024;
    uint8_t* buf = static_cast<uint8_t*>(malloc(kOtaChunkSize));
    if (!buf) {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate OTA buffer");
        return ESP_FAIL;
    }

    auto fail_ota = [&](int status_code, const char* message) -> esp_err_t {
        free(buf);
        send_json_error(req, status_code, message);
        mgr.ota_in_progress_ = false;
        return ESP_FAIL;
    };

    auto fail_ota_with_update_abort = [&](int status_code, const char* message) -> esp_err_t {
        Update.abort();
        free(buf);
        send_json_error(req, status_code, message);
        mgr.ota_in_progress_ = false;
        return ESP_FAIL;
    };

    size_t remaining = req->content_len;
    const size_t expected_size = req->content_len;
    size_t total_written = 0;
    size_t last_reported = 0;
    int timeout_count = 0;
    
    LOG_INFO("HTTP_OTA", "=== OTA START ===");
    LOG_INFO("HTTP_OTA", "OTA upload source IP: %s", client_ip);
    LOG_INFO("HTTP_OTA", "Receiving OTA update, size: %u bytes", (unsigned)remaining);
    LOG_INFO("HTTP_OTA", "Heap before OTA: free=%u, max_alloc=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    
    // Stop other tasks to free resources

    mgr.ota_in_progress_ = true;
    mgr.ota_ready_for_reboot_ = false;
    mgr.ota_last_success_ = false;
    strncpy(mgr.ota_last_error_, "", sizeof(mgr.ota_last_error_) - 1);
    mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
    
    LOG_INFO("HTTP_OTA", "Calling Update.begin(UPDATE_SIZE_UNKNOWN)...");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        LOG_ERROR("HTTP_OTA", "Update.begin failed: %s", Update.errorString());
        LOG_ERROR("HTTP_OTA", "Update.begin error code: %d", Update.getError());
        strncpy(mgr.ota_last_error_, Update.errorString(), sizeof(mgr.ota_last_error_) - 1);
        mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
        return fail_ota(HTTPD_500_INTERNAL_SERVER_ERROR, "Update begin failed");
    }
    LOG_INFO("HTTP_OTA", "Update.begin OK");
    
    // Receive and write firmware data
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req,
                                      reinterpret_cast<char*>(buf),
                                      (remaining < kOtaChunkSize) ? remaining : kOtaChunkSize);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                timeout_count++;
                if (timeout_count >= OTA_UPLOAD_MAX_RECV_TIMEOUTS) {
                    LOG_ERROR("HTTP_OTA", "Upload timed out after %d recv timeouts (remaining=%u, written=%u)",
                              timeout_count, (unsigned)remaining, (unsigned)total_written);
                    strncpy(mgr.ota_last_error_, "Upload timeout", sizeof(mgr.ota_last_error_) - 1);
                    mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
                    return fail_ota_with_update_abort(HTTP_STATUS_REQUEST_TIMEOUT, "Upload timed out");
                }
                if ((timeout_count % 20) == 0) {
                    LOG_WARN("HTTP_OTA", "recv timeout x%d, remaining=%u, written=%u", timeout_count, (unsigned)remaining, (unsigned)total_written);
                }
                continue;
            }
            LOG_ERROR("HTTP_OTA", "Connection error during upload (recv_len=%d, remaining=%u, written=%u)", recv_len, (unsigned)remaining, (unsigned)total_written);
            strncpy(mgr.ota_last_error_, "Connection error during upload", sizeof(mgr.ota_last_error_) - 1);
            mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
            return fail_ota_with_update_abort(HTTPD_500_INTERNAL_SERVER_ERROR, "Connection error");
        }
        
        // Write firmware chunk
        if (Update.write((uint8_t*)buf, recv_len) != (size_t)recv_len) {
            LOG_ERROR("HTTP_OTA", "Update.write failed: %s", Update.errorString());
            LOG_ERROR("HTTP_OTA", "Update.write error code: %d (chunk=%d, remaining=%u, written=%u)", Update.getError(), recv_len, (unsigned)remaining, (unsigned)total_written);
            strncpy(mgr.ota_last_error_, Update.errorString(), sizeof(mgr.ota_last_error_) - 1);
            mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
            return fail_ota_with_update_abort(HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
        }
        
        remaining -= recv_len;
        total_written += (size_t)recv_len;

        if ((total_written - last_reported) >= 32768 || remaining == 0) {
            last_reported = total_written;
            const unsigned percent = (expected_size > 0) ? (unsigned)((total_written * 100U) / expected_size) : 0U;
            LOG_DEBUG("HTTP_OTA", "Progress: %u%%, written=%u/%u, remaining=%u, heap=%u",
                      percent, (unsigned)total_written, (unsigned)expected_size, (unsigned)remaining, ESP.getFreeHeap());
        }
    }

    LOG_INFO("HTTP_OTA", "Receive loop complete: written=%u bytes, expected=%u bytes", (unsigned)total_written, (unsigned)expected_size);
    LOG_INFO("HTTP_OTA", "Calling Update.end(true)...");
    
    // Finalize update
    if (Update.end(true)) {
        free(buf);
        LOG_INFO("HTTP_OTA", "Update successful! Size: %u bytes", Update.size());
        LOG_INFO("HTTP_OTA", "OTA ready for reboot. Heap after OTA: free=%u, max_alloc=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        mgr.ota_in_progress_ = false;
        mgr.ota_ready_for_reboot_ = true;
        mgr.ota_last_success_ = true;
        mgr.ota_session_.deactivate();

        StaticJsonDocument<192> ok_doc;
        ok_doc["success"] = true;
        ok_doc["code"] = 200;
        ok_doc["message"] = "OTA applied. Waiting for reboot command";
        ok_doc["ready_for_reboot"] = true;
        char ok_json[192];
        const size_t ok_json_len = serializeJson(ok_doc, ok_json, sizeof(ok_json));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_send(req, ok_json, ok_json_len > 0 ? ok_json_len : HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    } else {
        free(buf);
        LOG_ERROR("HTTP_OTA", "Update.end failed: %s", Update.errorString());
        LOG_ERROR("HTTP_OTA", "Update.end error code: %d, written=%u, expected=%u", Update.getError(), (unsigned)total_written, (unsigned)expected_size);
        LOG_ERROR("HTTP_OTA", "Heap on failure: free=%u, max_alloc=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        strncpy(mgr.ota_last_error_, Update.errorString(), sizeof(mgr.ota_last_error_) - 1);
        mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, Update.errorString());
        mgr.ota_in_progress_ = false;
        mgr.ota_session_.deactivate();
        return ESP_FAIL;
    }
}

esp_err_t OtaManager::root_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/") != ESP_OK) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "ESP-NOW Transmitter - Ready for OTA");
    return ESP_OK;
}

esp_err_t OtaManager::health_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/health") != ESP_OK) {
        return ESP_FAIL;
    }

    auto& ota = instance();
    const bool eth_connected = EthernetManager::instance().is_connected();
    const bool eth_ready = EthernetManager::instance().is_fully_ready();
    const bool mqtt_connected = MqttManager::instance().is_connected();
    const bool espnow_connected = EspNowConnectionManager::instance().is_connected();

    StaticJsonDocument<384> doc;
    doc["success"] = true;
    doc["status"] = "ok";
    doc["uptime_ms"] = static_cast<unsigned long>(millis());
    doc["heap_free"] = static_cast<unsigned>(ESP.getFreeHeap());
    doc["heap_max_alloc"] = static_cast<unsigned>(ESP.getMaxAllocHeap());
    doc["eth_connected"] = eth_connected;
    doc["eth_ready"] = eth_ready;
    doc["mqtt_connected"] = mqtt_connected;
    doc["espnow_connected"] = espnow_connected;
    doc["ota_in_progress"] = ota.ota_in_progress_;
    doc["ota_ready_for_reboot"] = ota.ota_ready_for_reboot_;

    char json[384];
    const size_t json_len = serializeJson(doc, json, sizeof(json));
    if (json_len == 0 || json_len >= sizeof(json)) {
        return send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Health JSON formatting error");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, json_len);
    return ESP_OK;
}

esp_err_t OtaManager::event_logs_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/get_event_logs") != ESP_OK) {
        return ESP_FAIL;
    }

    // Query parameters: limit (default 50)
    char buf[128];
    int limit = 50;
    
    // Try to extract limit parameter from query string
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char limit_str[16];
        if (httpd_query_key_value(buf, "limit", limit_str, sizeof(limit_str)) == ESP_OK) {
            uint32_t parsed_limit = 0;
            if (parse_uint32_strict(limit_str, &parsed_limit) && parsed_limit >= 1 && parsed_limit <= 500) {
                limit = static_cast<int>(parsed_limit);
            }
            // Invalid or out-of-range values silently keep the default of 50.
        }
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    #ifdef CONFIG_BATTERY_EMULATOR_ENABLED
        // Import event system
        extern const EVENTS_STRUCT_TYPE* get_event_pointer(EVENTS_ENUM_TYPE event);
        extern const char* get_event_enum_string(EVENTS_ENUM_TYPE event);
        extern String get_event_message_string(EVENTS_ENUM_TYPE event);
        extern const char* get_event_level_string(EVENTS_ENUM_TYPE event);
        
        // Collect active events
        std::vector<std::pair<EVENTS_ENUM_TYPE, const EVENTS_STRUCT_TYPE*>> active_events;
        
        for (int i = 0; i < EVENT_NOF_EVENTS; i++) {
            const EVENTS_STRUCT_TYPE* event_ptr = get_event_pointer((EVENTS_ENUM_TYPE)i);
            if (event_ptr && event_ptr->occurences > 0) {
                active_events.push_back({(EVENTS_ENUM_TYPE)i, event_ptr});
            }
        }
        
        // Sort by timestamp descending (newest first)
        std::sort(active_events.begin(), active_events.end(),
            [](const auto& a, const auto& b) {
                return a.second->timestamp > b.second->timestamp;
            });
        
        // Limit number of events in response
        int event_count = active_events.size();
        if (event_count > limit) {
            event_count = limit;
        }

        // Send JSON prefix
        char prefix[96];
        const int prefix_len = snprintf(prefix, sizeof(prefix),
                                        "{\"success\":true,\"event_count\":%d,\"events\":[",
                                        event_count);
        if (prefix_len <= 0 || httpd_resp_send_chunk(req, prefix, prefix_len) != ESP_OK) {
            return ESP_FAIL;
        }

        // Stream each event as a JSON object (avoids large String concatenation)
        for (int i = 0; i < event_count; i++) {
            if (i > 0) {
                if (httpd_resp_send_chunk(req, ",", 1) != ESP_OK) {
                    return ESP_FAIL;
                }
            }

            const auto& event_data = active_events[i];
            EVENTS_ENUM_TYPE event_handle = event_data.first;
            const EVENTS_STRUCT_TYPE* event_ptr = event_data.second;

            StaticJsonDocument<384> doc;
            doc["type"] = get_event_enum_string(event_handle);
            doc["level"] = get_event_level_string(event_handle);
            doc["timestamp_ms"] = static_cast<uint32_t>(event_ptr->timestamp);
            doc["count"] = static_cast<uint32_t>(event_ptr->occurences);
            doc["message"] = get_event_message_string(event_handle);

            char event_json[384];
            const size_t event_json_len = serializeJson(doc, event_json, sizeof(event_json));
            if (event_json_len == 0 || httpd_resp_send_chunk(req, event_json, event_json_len) != ESP_OK) {
                return ESP_FAIL;
            }
        }
    #else
        const char* no_emulator_json = "{\"success\":false,\"error\":\"Battery emulator not enabled\",\"events\":[]}";
        httpd_resp_send(req, no_emulator_json, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    #endif

    if (httpd_resp_send_chunk(req, "]}", 2) != ESP_OK) {
        return ESP_FAIL;
    }
    if (httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t OtaManager::ota_status_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/ota_status") != ESP_OK) {
        return ESP_FAIL;
    }

    auto& mgr = instance();

    LOG_DEBUG("HTTP_OTA", "Status requested: in_progress=%d, ready_for_reboot=%d, last_success=%d",
              mgr.ota_in_progress_ ? 1 : 0,
              mgr.ota_ready_for_reboot_ ? 1 : 0,
              mgr.ota_last_success_ ? 1 : 0);

    // Build via ArduinoJson so last_error and session fields are properly escaped
    // regardless of what characters the Update library places in the error string.
    StaticJsonDocument<512> doc;
    doc["success"] = true;
    doc["in_progress"] = mgr.ota_in_progress_;
    doc["ready_for_reboot"] = mgr.ota_ready_for_reboot_;
    doc["last_success"] = mgr.ota_last_success_;
    doc["last_error"] = mgr.ota_last_error_;
    doc["auth_required"] = true;
    doc["session_active"] = mgr.ota_session_.is_active();
    doc["session_consumed"] = mgr.ota_session_.is_consumed();
    doc["session_id"] = mgr.ota_session_.is_active() ? mgr.ota_session_.session_id() : "";
    doc["nonce"] = mgr.ota_session_.is_active() ? mgr.ota_session_.nonce() : "";
    doc["expires_at_ms"] = static_cast<unsigned long>(
        mgr.ota_session_.is_active() ? mgr.ota_session_.expires_at_ms() : 0);
    doc["attempts_remaining"] = mgr.ota_session_.is_active()
        ? mgr.ota_session_.attempts_remaining()
        : 0;

    char json[512];
    const size_t json_len = serializeJson(doc, json, sizeof(json));
    if (json_len == 0 || json_len >= sizeof(json)) {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status JSON formatting error");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, json_len);
    return ESP_OK;
}

esp_err_t OtaManager::firmware_info_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/firmware_info") != ESP_OK) {
        return ESP_FAIL;
    }

    StaticJsonDocument<384> doc;

    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        // Metadata is valid – use embedded metadata (ArduinoJson escapes all fields).
        char version_str[16];
        (void)snprintf(version_str, sizeof(version_str), "%d.%d.%d",
                       FirmwareMetadata::metadata.version_major,
                       FirmwareMetadata::metadata.version_minor,
                       FirmwareMetadata::metadata.version_patch);
        doc["valid"] = true;
        doc["env"] = FirmwareMetadata::metadata.env_name;
        doc["device"] = FirmwareMetadata::metadata.device_type;
        doc["version"] = version_str;
        doc["build_date"] = FirmwareMetadata::metadata.build_date;
    } else {
        // Metadata not valid – return fallback from build flags.
        char version_str[16];
        (void)snprintf(version_str, sizeof(version_str), "%d.%d.%d",
                       FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
        char build_date_str[32];
        (void)snprintf(build_date_str, sizeof(build_date_str), "%s %s", __DATE__, __TIME__);
        doc["valid"] = false;
        doc["version"] = version_str;
        doc["build_date"] = build_date_str;
    }

    char json[384];
    const size_t json_len = serializeJson(doc, json, sizeof(json));
    if (json_len == 0 || json_len >= sizeof(json)) {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware info JSON formatting error");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, json_len);
    return ESP_OK;
}

esp_err_t OtaManager::test_data_config_get_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/test_data_config") != ESP_OK) {
        return ESP_FAIL;
    }

    char json_buffer[1024];
    
    if (TestDataConfig::get_config_json(json_buffer, sizeof(json_buffer))) {
        char response[1152];
        int response_len = snprintf(response, sizeof(response),
                                    "{\"success\":true,\"config\":%s}", json_buffer);
        if (response_len <= 0 || response_len >= static_cast<int>(sizeof(response))) {
            send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Configuration response too large");
            return ESP_FAIL;
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_send(req, response, response_len);
        return ESP_OK;
    } else {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to generate configuration JSON");
        return ESP_FAIL;
    }
}

esp_err_t OtaManager::test_data_config_post_handler(httpd_req_t *req) {
    if (check_request_content_type(req, "application/json") != ESP_OK) {
        return ESP_FAIL;
    }
    char content[1024];
    if (read_request_body_strict(req, content, sizeof(content)) != ESP_OK) {
        return ESP_FAIL;
    }

    // Parse and apply configuration (persist=true)
    if (TestDataConfig::set_config_from_json(content, true)) {
        return send_json_ok(req, "Configuration updated and saved");
    } else {
        send_json_error(req, HTTPD_400_BAD_REQUEST,
                        "Invalid configuration or parse error");
        return ESP_FAIL;
    }
}

esp_err_t OtaManager::test_data_apply_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/test_data_apply") != ESP_OK) {
        return ESP_FAIL;
    }

    if (TestDataConfig::apply_config()) {
        return send_json_ok(req, "Configuration applied");
    } else {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to apply configuration");
        return ESP_FAIL;
    }
}

esp_err_t OtaManager::test_data_reset_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/test_data_reset") != ESP_OK) {
        return ESP_FAIL;
    }

    if (TestDataConfig::reset_to_defaults(true)) {
        return send_json_ok(req, "Configuration reset to defaults");
    } else {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to reset configuration");
        return ESP_FAIL;
    }
}

void OtaManager::init_http_server() {
    if (http_server_ != nullptr) {
        LOG_INFO("HTTP_SERVER", "HTTP server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 12;
    config.max_resp_headers = 8;
    // OTA + JSON logging paths can be stack-heavy on the HTTP worker task.
    // Raise stack to avoid stack canary trips during OTA uploads.
    config.stack_size = 10240;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    
    // Start HTTP server
    if (httpd_start(&http_server_, &config) == ESP_OK) {
        // Register OTA upload handler
        httpd_uri_t ota_upload_uri = {
            .uri = "/ota_upload",
            .method = HTTP_POST,
            .handler = ota_upload_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &ota_upload_uri);

        // Register OTA arm handler (creates short-lived signed session)
        httpd_uri_t ota_arm_uri = {
            .uri = "/api/ota_arm",
            .method = HTTP_POST,
            .handler = ota_arm_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &ota_arm_uri);

        // Register OTA status handler
        httpd_uri_t ota_status_uri = {
            .uri = "/api/ota_status",
            .method = HTTP_GET,
            .handler = ota_status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &ota_status_uri);
        
        // Register firmware info handler
        httpd_uri_t firmware_info_uri = {
            .uri = "/api/firmware_info",
            .method = HTTP_GET,
            .handler = firmware_info_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &firmware_info_uri);
        
        // Register event logs handler
        httpd_uri_t event_logs_uri = {
            .uri = "/api/get_event_logs",
            .method = HTTP_GET,
            .handler = event_logs_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &event_logs_uri);
        
        // Register root handler
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &root_uri);

        // Register consolidated runtime health endpoint
        httpd_uri_t health_uri = {
            .uri = "/api/health",
            .method = HTTP_GET,
            .handler = health_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &health_uri);
        
        // Register test data configuration GET handler
        httpd_uri_t test_data_config_get_uri = {
            .uri = "/api/test_data_config",
            .method = HTTP_GET,
            .handler = test_data_config_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &test_data_config_get_uri);
        
        // Register test data configuration POST handler
        httpd_uri_t test_data_config_post_uri = {
            .uri = "/api/test_data_config",
            .method = HTTP_POST,
            .handler = test_data_config_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &test_data_config_post_uri);
        
        // Register test data apply handler
        httpd_uri_t test_data_apply_uri = {
            .uri = "/api/test_data_apply",
            .method = HTTP_POST,
            .handler = test_data_apply_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &test_data_apply_uri);
        
        // Register test data reset handler
        httpd_uri_t test_data_reset_uri = {
            .uri = "/api/test_data_reset",
            .method = HTTP_POST,
            .handler = test_data_reset_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &test_data_reset_uri);
        
        LOG_INFO("HTTP_SERVER", "HTTP server started on port 80");
    } else {
        LOG_ERROR("HTTP_SERVER", "Failed to start HTTP server");
    }
}

void OtaManager::stop_http_server() {
    if (http_server_ == nullptr) {
        return;
    }

    if (ota_in_progress_) {
        LOG_WARN("HTTP_SERVER", "Stopping HTTP server while OTA in progress (network disconnected)");
    }

    if (httpd_stop(http_server_) == ESP_OK) {
        LOG_INFO("HTTP_SERVER", "HTTP server stopped");
    } else {
        LOG_WARN("HTTP_SERVER", "Failed to stop HTTP server cleanly");
    }

    http_server_ = nullptr;
}
