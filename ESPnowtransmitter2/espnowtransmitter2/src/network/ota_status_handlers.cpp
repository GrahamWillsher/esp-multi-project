// ota_status_handlers.cpp
// Implements the read-only informational HTTP handlers extracted from ota_manager.cpp:
//   root_handler, health_handler, event_logs_handler, ota_status_handler,
//   firmware_info_handler, and the four test-data management handlers.

#include "ota_manager.h"
#include "ota_manager_internal.h"
#include "ethernet_manager.h"
#include "mqtt_manager.h"
#include "../config/logging_config.h"
#include "../test_data/test_data_config.h"
#include <esp32common/espnow/connection_manager.h>
#include <runtime_common_utils/ota_boot_guard.h>
#include <firmware_metadata.h>
#include <firmware_version.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <algorithm>
#include <vector>
#include <cstring>

#ifdef CONFIG_BATTERY_EMULATOR_ENABLED
// Event system symbols are defined in the battery-emulator component.
extern const EVENTS_STRUCT_TYPE* get_event_pointer(EVENTS_ENUM_TYPE event);
extern const char* get_event_enum_string(EVENTS_ENUM_TYPE event);
extern String get_event_message_string(EVENTS_ENUM_TYPE event);
extern const char* get_event_level_string(EVENTS_ENUM_TYPE event);
#endif

// ---------------------------------------------------------------------------

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
    const bool eth_connected    = EthernetManager::instance().is_connected();
    const bool eth_ready        = EthernetManager::instance().is_fully_ready();
    const bool mqtt_connected   = MqttManager::instance().is_connected();
    const bool espnow_connected = EspNowConnectionManager::instance().is_connected();
    bool ota_psk_provisioned    = false;
    char psk_tmp[96]            = {0};
    const bool ota_psk_available =
        load_ota_psk(psk_tmp, sizeof(psk_tmp), &ota_psk_provisioned);

    StaticJsonDocument<512> doc;
    doc["success"]               = true;
    doc["status"]                = "ok";
    doc["uptime_ms"]             = static_cast<unsigned long>(millis());
    doc["heap_free"]             = static_cast<unsigned>(ESP.getFreeHeap());
    doc["heap_max_alloc"]        = static_cast<unsigned>(ESP.getMaxAllocHeap());
    doc["eth_connected"]         = eth_connected;
    doc["eth_ready"]             = eth_ready;
    doc["mqtt_connected"]        = mqtt_connected;
    doc["espnow_connected"]      = espnow_connected;
    doc["ota_in_progress"]       = ota.ota_in_progress_;
    doc["ota_ready_for_reboot"]  = ota.ota_ready_for_reboot_;
    doc["ota_commit_state"]      = ota.ota_commit_state_;
    doc["ota_commit_detail"]     = ota.ota_commit_detail_;
    doc["ota_psk_available"]     = ota_psk_available;
    doc["ota_psk_provisioned"]   = ota_psk_provisioned;
    doc["boot_guard_state"]      = OtaBootGuard::state_string();
    doc["boot_guard_reason"]     = OtaBootGuard::last_reason();
    doc["rollback_pending"]      = OtaBootGuard::is_pending_verification();
    doc["boot_guard_passed"]     =
        (OtaBootGuard::state() == OtaBootGuard::State::Confirmed);

    char json[512];
    const size_t json_len = serializeJson(doc, json, sizeof(json));
    if (json_len == 0 || json_len >= sizeof(json)) {
        return send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Health JSON formatting error");
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

    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char limit_str[16];
        if (httpd_query_key_value(buf, "limit", limit_str,
                                  sizeof(limit_str)) == ESP_OK) {
            uint32_t parsed_limit = 0;
            if (parse_uint32_strict(limit_str, &parsed_limit) &&
                parsed_limit >= 1 && parsed_limit <= 500) {
                limit = static_cast<int>(parsed_limit);
            }
            // Invalid or out-of-range values silently keep the default of 50.
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

#ifdef CONFIG_BATTERY_EMULATOR_ENABLED
    // Collect active events
    std::vector<std::pair<EVENTS_ENUM_TYPE, const EVENTS_STRUCT_TYPE*>>
        active_events;

    for (int i = 0; i < EVENT_NOF_EVENTS; i++) {
        const EVENTS_STRUCT_TYPE* event_ptr =
            get_event_pointer(static_cast<EVENTS_ENUM_TYPE>(i));
        if (event_ptr && event_ptr->occurences > 0) {
            active_events.push_back(
                {static_cast<EVENTS_ENUM_TYPE>(i), event_ptr});
        }
    }

    // Sort by timestamp descending (newest first)
    std::sort(active_events.begin(), active_events.end(),
              [](const auto& a, const auto& b) {
                  return a.second->timestamp > b.second->timestamp;
              });

    int event_count = static_cast<int>(active_events.size());
    if (event_count > limit) { event_count = limit; }

    char prefix[96];
    const int prefix_len = snprintf(prefix, sizeof(prefix),
                                    "{\"success\":true,\"event_count\":%d,"
                                    "\"events\":[",
                                    event_count);
    if (prefix_len <= 0 ||
        httpd_resp_send_chunk(req, prefix, prefix_len) != ESP_OK) {
        return ESP_FAIL;
    }

    for (int i = 0; i < event_count; i++) {
        if (i > 0) {
            if (httpd_resp_send_chunk(req, ",", 1) != ESP_OK) {
                return ESP_FAIL;
            }
        }

        const auto& event_data        = active_events[i];
        EVENTS_ENUM_TYPE event_handle = event_data.first;
        const EVENTS_STRUCT_TYPE* event_ptr = event_data.second;

        StaticJsonDocument<384> edoc;
        edoc["type"]         = get_event_enum_string(event_handle);
        edoc["level"]        = get_event_level_string(event_handle);
        edoc["timestamp_ms"] = static_cast<uint32_t>(event_ptr->timestamp);
        edoc["count"]        = static_cast<uint32_t>(event_ptr->occurences);
        edoc["message"]      = get_event_message_string(event_handle);

        char event_json[384];
        const size_t event_json_len =
            serializeJson(edoc, event_json, sizeof(event_json));
        if (event_json_len == 0 ||
            httpd_resp_send_chunk(req, event_json, event_json_len) != ESP_OK) {
            return ESP_FAIL;
        }
    }
#else
    const char* no_emulator_json =
        "{\"success\":false,\"error\":\"Battery emulator not enabled\","
        "\"events\":[]}";
    httpd_resp_send(req, no_emulator_json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
#endif

    if (httpd_resp_send_chunk(req, "]}", 2) != ESP_OK) { return ESP_FAIL; }
    if (httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) { return ESP_FAIL; }
    return ESP_OK;
}

esp_err_t OtaManager::ota_status_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/ota_status") != ESP_OK) {
        return ESP_FAIL;
    }

    auto& mgr = instance();

    LOG_DEBUG("HTTP_OTA",
              "Status requested: in_progress=%d, ready_for_reboot=%d, "
              "last_success=%d",
              mgr.ota_in_progress_      ? 1 : 0,
              mgr.ota_ready_for_reboot_ ? 1 : 0,
              mgr.ota_last_success_     ? 1 : 0);

    bool ota_psk_provisioned = false;
    char psk_tmp[96]         = {0};
    const bool ota_psk_available =
        load_ota_psk(psk_tmp, sizeof(psk_tmp), &ota_psk_provisioned);
    const bool rollback_pending  = OtaBootGuard::is_pending_verification();
    const bool boot_guard_passed =
        (OtaBootGuard::state() == OtaBootGuard::State::Confirmed);
    const char* boot_guard_state = OtaBootGuard::state_string();
    const char* rollback_reason  = OtaBootGuard::last_reason();

    const char* commit_state  = mgr.ota_commit_state_;
    const char* commit_detail = mgr.ota_commit_detail_;

    if (!mgr.ota_in_progress_ && !mgr.ota_ready_for_reboot_) {
        if (boot_guard_passed && OtaBootGuard::was_pending_at_boot()) {
            // This boot started as an OTA pending-verify reboot AND the boot guard confirmed it.
            // Report committed_validated regardless of txn_id (which is reset to 0 on reboot).
            commit_state  = "committed_validated";
            commit_detail = "boot guard passed and app confirmed";
        } else if (rollback_pending) {
            commit_state  = "boot_pending_validation";
            commit_detail = "app rebooted; waiting for boot guard health gate";
        } else if (strcmp(boot_guard_state, "rollback_triggered") == 0) {
            commit_state  = "rollback_triggered";
            commit_detail = rollback_reason;
        } else if (strcmp(boot_guard_state, "error") == 0) {
            commit_state  = "boot_guard_error";
            commit_detail = rollback_reason;
        }
    }

    StaticJsonDocument<896> doc;
    doc["success"]           = true;
    doc["in_progress"]       = mgr.ota_in_progress_;
    doc["ready_for_reboot"]  = mgr.ota_ready_for_reboot_;
    doc["last_success"]      = mgr.ota_last_success_;
    doc["ota_txn_id"]        = mgr.ota_txn_id_;
    doc["commit_state"]      = commit_state;
    doc["commit_detail"]     = commit_detail;
    doc["state_since_ms"]    = mgr.ota_state_since_ms_;
    doc["last_update_ms"]    = mgr.ota_last_update_ms_;
    doc["ota_psk_available"] = ota_psk_available;
    doc["ota_psk_provisioned"] = ota_psk_provisioned;
    doc["last_error"]        = mgr.ota_last_error_;
    doc["auth_required"]     = true;
    doc["session_active"]    = mgr.ota_session_.is_active();
    doc["session_consumed"]  = mgr.ota_session_.is_consumed();
    doc["session_id"]        = mgr.ota_session_.is_active()
                                    ? mgr.ota_session_.session_id() : "";
    doc["nonce"]             = mgr.ota_session_.is_active()
                                    ? mgr.ota_session_.nonce() : "";
    doc["expires_at_ms"]     = static_cast<unsigned long>(
        mgr.ota_session_.is_active()
            ? mgr.ota_session_.expires_at_ms() : 0);
    doc["attempts_remaining"] = mgr.ota_session_.is_active()
                                    ? mgr.ota_session_.attempts_remaining() : 0;

    // Provide upload signature in status so control-plane OTA_START can be
    // followed by a direct /api/ota_status challenge fetch.
    char status_sig[65]   = {0};
    bool status_sig_ok    = false;
    if (mgr.ota_session_.is_active() && ota_psk_available) {
        status_sig_ok = mgr.ota_session_.compute_signature(
            psk_tmp,
            static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFFUL),
            status_sig,
            sizeof(status_sig));
    }
    doc["signature"]           = status_sig_ok ? status_sig : "";
    doc["signature_available"] = status_sig_ok;
    doc["boot_guard_state"]    = boot_guard_state;
    doc["rollback_pending"]    = rollback_pending;
    doc["boot_guard_passed"]   = boot_guard_passed;
    doc["rollback_reason"]     = rollback_reason;

    char json[896];
    const size_t json_len = serializeJson(doc, json, sizeof(json));
    if (json_len == 0 || json_len >= sizeof(json)) {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Status JSON formatting error");
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
        char version_str[16];
        (void)snprintf(version_str, sizeof(version_str), "%d.%d.%d",
                       FirmwareMetadata::metadata.version_major,
                       FirmwareMetadata::metadata.version_minor,
                       FirmwareMetadata::metadata.version_patch);
        doc["valid"]       = true;
        doc["env"]         = FirmwareMetadata::metadata.env_name;
        doc["device"]      = FirmwareMetadata::metadata.device_type;
        doc["version"]     = version_str;
        doc["build_date"]  = FirmwareMetadata::metadata.build_date;
    } else {
        char version_str[16];
        (void)snprintf(version_str, sizeof(version_str), "%d.%d.%d",
                       FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
        char build_date_str[32];
        (void)snprintf(build_date_str, sizeof(build_date_str), "%s %s",
                       __DATE__, __TIME__);
        doc["valid"]       = false;
        doc["version"]     = version_str;
        doc["build_date"]  = build_date_str;
    }

    char json[384];
    const size_t json_len = serializeJson(doc, json, sizeof(json));
    if (json_len == 0 || json_len >= sizeof(json)) {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Firmware info JSON formatting error");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, json_len);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Test-data management handlers
// ---------------------------------------------------------------------------

esp_err_t OtaManager::test_data_config_get_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/test_data_config") != ESP_OK) {
        return ESP_FAIL;
    }

    char json_buffer[1024];

    if (TestDataConfig::get_config_json(json_buffer, sizeof(json_buffer))) {
        char response[1152];
        int response_len = snprintf(response, sizeof(response),
                                    "{\"success\":true,\"config\":%s}",
                                    json_buffer);
        if (response_len <= 0 ||
            response_len >= static_cast<int>(sizeof(response))) {
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
