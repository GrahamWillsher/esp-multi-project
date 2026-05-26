// ota_status_handlers.cpp
// Implements the read-only informational HTTP handlers extracted from ota_manager.cpp:
//   root_handler, health_handler, event_logs_handler, ota_status_handler,
//   firmware_info_handler, and the four test-data management handlers.

#include "ota_manager.h"
#include "ota_manager_internal.h"
#include "ethernet_manager.h"
#include "mqtt_manager.h"
#include "time_manager.h"
#include "../config/logging_config.h"
#include "../test_data/test_data_config.h"
#include <runtime_common_utils/ota_boot_guard.h>
#include <firmware_metadata.h>
#include <firmware_version.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <algorithm>
#include <vector>
#include <cstring>

#if __has_include("../battery_emulator/devboard/utils/events.h")
#define EVENT_LOGS_BACKEND_AVAILABLE 1
#include "../battery_emulator/devboard/utils/events.h"
#else
#define EVENT_LOGS_BACKEND_AVAILABLE 0
#endif

// ---------------------------------------------------------------------------

namespace {
constexpr int kEventLogsDefaultLimit = 50;
constexpr int kEventLogsMinLimit = 1;
constexpr int kEventLogsMaxLimit = 500;

constexpr size_t kHealthDocBytes = 512;
constexpr size_t kHealthJsonBytes = 512;
constexpr size_t kEventLogsQueryBufferBytes = 128;
constexpr size_t kEventLogsLimitBufferBytes = 16;
constexpr size_t kEventLogsPrefixBytes = 96;
constexpr size_t kEventDocBytes = 384;
constexpr size_t kEventMessageBytes = 384;
constexpr size_t kEventJsonBytes = 384;
constexpr size_t kStatusDocBytes = 896;
constexpr size_t kStatusJsonBytes = 896;
constexpr size_t kFirmwareInfoDocBytes = 384;
constexpr size_t kFirmwareInfoJsonBytes = 384;
constexpr size_t kTestConfigJsonBytes = 1024;
constexpr size_t kTestConfigResponseBytes = 1152;
constexpr size_t kTestConfigPostBodyBytes = 1024;

static esp_err_t send_response_checked(httpd_req_t* req,
                                       const char* payload,
                                       ssize_t len,
                                       const char* tag) {
    const esp_err_t rc = httpd_resp_send(req, payload, len);
    if (rc != ESP_OK) {
        LOG_WARN("HTTP_OTA", "%s: httpd_resp_send failed (%d)", tag, static_cast<int>(rc));
    }
    return rc;
}

static esp_err_t send_response_str_checked(httpd_req_t* req,
                                           const char* payload,
                                           const char* tag) {
    return send_response_checked(req, payload, HTTPD_RESP_USE_STRLEN, tag);
}

static esp_err_t send_chunk_checked(httpd_req_t* req,
                                    const char* payload,
                                    ssize_t len,
                                    const char* tag) {
    const esp_err_t rc = httpd_resp_send_chunk(req, payload, len);
    if (rc != ESP_OK) {
        LOG_WARN("HTTP_OTA", "%s: httpd_resp_send_chunk failed (%d)", tag, static_cast<int>(rc));
    }
    return rc;
}
} // namespace

esp_err_t OtaManager::root_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/") != ESP_OK) {
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/plain");
    return (send_response_str_checked(req,
                                      "Battery Emulator Transmitter - Ready for OTA",
                                      "root_handler") == ESP_OK)
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t OtaManager::health_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/health") != ESP_OK) {
        return ESP_FAIL;
    }

    auto& ota = instance();
    const bool eth_connected    = EthernetManager::instance().is_connected();
    const bool eth_ready        = EthernetManager::instance().is_fully_ready();
    const bool mqtt_connected   = MqttManager::instance().is_connected();
    const bool receiver_mac_known = false;
    bool ota_psk_provisioned    = false;
    char psk_tmp[96]            = {0};
    const bool ota_psk_available =
        load_ota_psk(psk_tmp, sizeof(psk_tmp), &ota_psk_provisioned);
    StaticJsonDocument<kHealthDocBytes> doc;
    doc["success"]               = true;
    doc["status"]                = "ok";
    doc["uptime_ms"]             = static_cast<unsigned long long>(TimeManager::instance().get_uptime_ms());
    doc["unix_time"]             = static_cast<unsigned long long>(TimeManager::instance().get_unix_time());
    doc["time_source"]           = TimeManager::instance().get_time_source_byte();
    doc["heap_free"]             = static_cast<unsigned>(ESP.getFreeHeap());
    doc["heap_max_alloc"]        = static_cast<unsigned>(ESP.getMaxAllocHeap());
    doc["eth_connected"]         = eth_connected;
    doc["eth_ready"]             = eth_ready;
    doc["mqtt_connected"]        = mqtt_connected;
    doc["receiver_mac_known"]    = receiver_mac_known;
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

    char json[kHealthJsonBytes];
    const size_t json_len = serializeJson(doc, json, sizeof(json));
    if (json_len == 0 || json_len >= sizeof(json)) {
        return send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Health JSON formatting error");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return (send_response_checked(req, json, static_cast<ssize_t>(json_len), "health_handler") == ESP_OK)
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t OtaManager::routes_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/routes") != ESP_OK) {
        return ESP_FAIL;
    }

    static constexpr const char* kRoutes[] = {
        "GET /",
        "GET /api/health",
        "GET /api/routes",
        "POST /ota_upload",
        "POST /api/ota_arm",
        "GET /api/ota_status",
        "GET /api/firmware_info",
        "GET /api/get_event_logs",
        "POST /api/clear_event_logs",
        "GET /api/test_data_config",
        "POST /api/test_data_config",
        "POST /api/test_data_apply",
        "POST /api/test_data_reset"
    };

    StaticJsonDocument<768> doc;
    doc["success"] = true;
    JsonArray routes = doc.createNestedArray("routes");
    for (const char* route : kRoutes) {
        routes.add(route);
    }
    doc["route_count"] = routes.size();

    char json[768];
    const size_t json_len = serializeJson(doc, json, sizeof(json));
    if (json_len == 0 || json_len >= sizeof(json)) {
        return send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Route JSON formatting error");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return (send_response_checked(req, json, static_cast<ssize_t>(json_len), "routes_handler") == ESP_OK)
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t OtaManager::event_logs_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/get_event_logs") != ESP_OK) {
        return ESP_FAIL;
    }

    // Query parameters: limit (default 50)
    char buf[kEventLogsQueryBufferBytes];
    int limit = kEventLogsDefaultLimit;

    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char limit_str[kEventLogsLimitBufferBytes];
        if (httpd_query_key_value(buf, "limit", limit_str,
                                  sizeof(limit_str)) == ESP_OK) {
            uint32_t parsed_limit = 0;
            if (parse_uint32_strict(limit_str, &parsed_limit) &&
                parsed_limit >= kEventLogsMinLimit && parsed_limit <= kEventLogsMaxLimit) {
                limit = static_cast<int>(parsed_limit);
            }
            // Invalid or out-of-range values silently keep the default.
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

#if EVENT_LOGS_BACKEND_AVAILABLE
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
    typedef std::pair<EVENTS_ENUM_TYPE, const EVENTS_STRUCT_TYPE*> EventPair;
    std::sort(active_events.begin(), active_events.end(),
              [](const EventPair& a, const EventPair& b) {
                  return a.second->timestamp > b.second->timestamp;
              });

    int event_count = static_cast<int>(active_events.size());
    if (event_count > limit) { event_count = limit; }

    char prefix[kEventLogsPrefixBytes];
    const int prefix_len = snprintf(prefix, sizeof(prefix),
                                    "{\"success\":true,\"event_count\":%d,"
                                    "\"events\":[",
                                    event_count);
    if (prefix_len <= 0 ||
        send_chunk_checked(req, prefix, prefix_len, "event_logs_handler_prefix") != ESP_OK) {
        return ESP_FAIL;
    }

    for (int i = 0; i < event_count; i++) {
        if (i > 0) {
            if (send_chunk_checked(req, ",", 1, "event_logs_handler_separator") != ESP_OK) {
                return ESP_FAIL;
            }
        }

        const auto& event_data        = active_events[i];
        EVENTS_ENUM_TYPE event_handle = event_data.first;
        const EVENTS_STRUCT_TYPE* event_ptr = event_data.second;

        StaticJsonDocument<kEventDocBytes> edoc;
        char event_message[kEventMessageBytes] = {0};
        const bool have_event_message =
            get_event_message(event_handle, event_message, sizeof(event_message), event_ptr->data);
        edoc["type"]         = get_event_enum_string(event_handle);
        edoc["level"]        = get_event_level_string(event_handle);
        edoc["timestamp_ms"] = static_cast<unsigned long long>(event_ptr->timestamp);
        edoc["count"]        = static_cast<uint32_t>(event_ptr->occurences);
        edoc["data"]         = event_ptr->data;
        edoc["message"]      = have_event_message ? event_message : "";

        char event_json[kEventJsonBytes];
        const size_t event_json_len =
            serializeJson(edoc, event_json, sizeof(event_json));
        if (event_json_len == 0 ||
            send_chunk_checked(req, event_json, event_json_len, "event_logs_handler_event") != ESP_OK) {
            return ESP_FAIL;
        }
    }
#else
    const char* no_emulator_json =
        "{\"success\":false,\"error\":\"Battery emulator not enabled\","
        "\"events\":[]}";
    return (send_response_str_checked(req, no_emulator_json, "event_logs_handler_no_emulator") == ESP_OK)
               ? ESP_OK
               : ESP_FAIL;
#endif

    if (send_chunk_checked(req, "]}", 2, "event_logs_handler_suffix") != ESP_OK) { return ESP_FAIL; }
    if (send_chunk_checked(req, nullptr, 0, "event_logs_handler_finalize") != ESP_OK) { return ESP_FAIL; }
    return ESP_OK;
}

esp_err_t OtaManager::clear_event_logs_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/clear_event_logs") != ESP_OK) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

#if EVENT_LOGS_BACKEND_AVAILABLE
    reset_all_events();
    const char* ok_json =
        "{\"success\":true,\"message\":\"Event logs cleared\",\"event_count\":0}";
    return (send_response_str_checked(req, ok_json, "clear_event_logs_handler_ok") == ESP_OK)
               ? ESP_OK
               : ESP_FAIL;
#else
    const char* err_json =
        "{\"success\":false,\"error\":\"Battery emulator not enabled\"}";
    return (send_response_str_checked(req, err_json, "clear_event_logs_handler_err") == ESP_OK)
               ? ESP_OK
               : ESP_FAIL;
#endif
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

    StaticJsonDocument<kStatusDocBytes> doc;
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

    char json[kStatusJsonBytes];
    const size_t json_len = serializeJson(doc, json, sizeof(json));
    if (json_len == 0 || json_len >= sizeof(json)) {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Status JSON formatting error");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return (send_response_checked(req, json, static_cast<ssize_t>(json_len), "ota_status_handler") == ESP_OK)
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t OtaManager::firmware_info_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/firmware_info") != ESP_OK) {
        return ESP_FAIL;
    }

    StaticJsonDocument<kFirmwareInfoDocBytes> doc;

    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        char version_str[16];
        (void)snprintf(version_str, sizeof(version_str), "v%d.%d.%d",
                       FirmwareMetadata::metadata.version_major,
                       FirmwareMetadata::metadata.version_minor,
                       FirmwareMetadata::metadata.version_patch);
        doc["valid"]       = true;
        doc["env"]         = FirmwareMetadata::metadata.env_name;
        doc["device"]      = FirmwareMetadata::metadata.device_type;
        doc["version"]     = version_str;
        doc["build_date"]  = FirmwareMetadata::metadata.build_date;
    } else {
        doc["valid"]       = false;
        doc["version"]     = FW_VERSION_STRING;
        doc["build_date"]  = FW_BUILD_DATE;
    }

    char json[kFirmwareInfoJsonBytes];
    const size_t json_len = serializeJson(doc, json, sizeof(json));
    if (json_len == 0 || json_len >= sizeof(json)) {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Firmware info JSON formatting error");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return (send_response_checked(req, json, static_cast<ssize_t>(json_len), "firmware_info_handler") == ESP_OK)
               ? ESP_OK
               : ESP_FAIL;
}

// ---------------------------------------------------------------------------
// Test-data management handlers
// ---------------------------------------------------------------------------

esp_err_t OtaManager::test_data_config_get_handler(httpd_req_t *req) {
    if (reject_unexpected_request_body(req, "/api/test_data_config") != ESP_OK) {
        return ESP_FAIL;
    }

    char json_buffer[kTestConfigJsonBytes];

    if (TestDataConfig::get_config_json(json_buffer, sizeof(json_buffer))) {
        char response[kTestConfigResponseBytes];
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
        return (send_response_checked(req, response, response_len, "test_data_config_get_handler") == ESP_OK)
                   ? ESP_OK
                   : ESP_FAIL;
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
    char content[kTestConfigPostBodyBytes];
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
