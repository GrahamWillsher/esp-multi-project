#include "ota_manager.h"
#include "ota_manager_internal.h"
#include "../config/logging_config.h"
#include "../config/network_config.h"
#include <webserver_common_utils/ota_auth_utils.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstring>
// Handler implementations live in companion translation units:
//   ota_http_utils.cpp      – free HTTP helpers, rate-limiting, PSK load
//   ota_upload_handler.cpp  – OtaManager::ota_upload_handler
//   ota_status_handlers.cpp – root/health/event_logs/ota_status/firmware_info
//                              and test-data handlers

OtaManager& OtaManager::instance() {
    static OtaManager instance;
    return instance;
}

void OtaManager::set_commit_state(const char* state, const char* detail) {
    if (state && state[0] != '\0') {
        strlcpy(ota_commit_state_, state, sizeof(ota_commit_state_));
    }

    if (detail && detail[0] != '\0') {
        strlcpy(ota_commit_detail_, detail, sizeof(ota_commit_detail_));
    }

    const uint32_t now_ms = millis();
    ota_last_update_ms_ = now_ms;
    ota_state_since_ms_ = now_ms;
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
    char ota_psk[96] = {0};
    if (!load_ota_psk(ota_psk, sizeof(ota_psk), nullptr)) {
        send_json_error(req,
                        HTTP_STATUS_SERVICE_UNAVAILABLE,
                        "OTA secret not provisioned",
                        "Set security/ota_psk in NVS (min 32 chars) before OTA");
        return false;
    }

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
        ota_psk,
        static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFFUL),
        now_ms);

    switch (result) {
        case OtaSessionUtils::ValidationResult::Valid:
            if (has_client_ip) {
                clear_auth_failures(client_ip, now_ms);
            }
            LOG_INFO("HTTP_OTA", "OTA auth validated");
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

    char ota_psk[96] = {0};
    if (!load_ota_psk(ota_psk, sizeof(ota_psk), nullptr)) {
        send_json_error(req,
                        HTTP_STATUS_SERVICE_UNAVAILABLE,
                        "OTA secret not provisioned",
                        "Set security/ota_psk in NVS (min 32 chars) before OTA");
        return ESP_FAIL;
    }

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
            ota_psk,
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