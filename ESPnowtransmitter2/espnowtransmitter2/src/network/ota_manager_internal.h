#pragma once
// ota_manager_internal.h
// Internal declarations shared across the ota_manager compilation unit family:
//   ota_manager.cpp   – singleton, session management, arm handler, server lifecycle
//   ota_http_utils.cpp – HTTP helpers, request validation, rate-limiting, PSK load
//   ota_upload_handler.cpp – OTA upload handler
//   ota_status_handlers.cpp – status/info/test-data handlers
//
// NOT part of the public API; do not include outside the network/ota_manager*.cpp files.

#include <esp_http_server.h>
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// HTTP status codes not defined by esp_http_server
// ---------------------------------------------------------------------------
constexpr int HTTP_STATUS_PAYLOAD_TOO_LARGE      = 413;
constexpr int HTTP_STATUS_REQUEST_TIMEOUT        = 408;
constexpr int HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415;
constexpr int HTTP_STATUS_SERVICE_UNAVAILABLE    = 503;
constexpr int HTTP_STATUS_TOO_MANY_REQUESTS      = 429;

// ---------------------------------------------------------------------------
// Upload constants
// ---------------------------------------------------------------------------
struct OtaUploadPolicy {
    size_t chunk_size_bytes;
    size_t progress_log_cadence_bytes;
    int timeout_warn_cadence;
    int max_recv_timeouts;
};

constexpr OtaUploadPolicy kOtaUploadPolicy{
    1024,
    32768,
    20,
    60,
};

constexpr size_t OTA_IMAGE_SHA256_HEX_LEN     = 64;

// ---------------------------------------------------------------------------
// HTTP response helpers
// ---------------------------------------------------------------------------
esp_err_t send_json_error(httpd_req_t* req,
                          int status_code,
                          const char* message,
                          const char* details = nullptr);

esp_err_t send_json_ok(httpd_req_t* req,
                       const char* message,
                       const char* details = nullptr);

esp_err_t reject_unexpected_request_body(httpd_req_t* req,
                                         const char* endpoint_name);

esp_err_t read_request_body_strict(httpd_req_t* req,
                                   char* buffer,
                                   size_t buffer_size,
                                   size_t* out_len = nullptr);

esp_err_t check_request_content_type(httpd_req_t* req,
                                     const char* expected_prefix);

bool get_request_client_ip(httpd_req_t* req, char* out, size_t out_len);

// ---------------------------------------------------------------------------
// Auth rate-limiting helpers
// ---------------------------------------------------------------------------
bool is_auth_rate_limited(const char* ip,
                          uint32_t now_ms,
                          uint32_t* retry_after_sec = nullptr);
void record_auth_failure(const char* ip, uint32_t now_ms);
void clear_auth_failures(const char* ip, uint32_t now_ms);

// ---------------------------------------------------------------------------
// Parsing / credential helpers
// ---------------------------------------------------------------------------
bool parse_uint32_strict(const char* value, uint32_t* out);
bool header_has_token_ci(const char* header_value, const char* expected_token);
bool is_hex_sha256(const char* value);
bool load_ota_psk(char* out_psk, size_t out_psk_len, bool* out_is_provisioned = nullptr);
