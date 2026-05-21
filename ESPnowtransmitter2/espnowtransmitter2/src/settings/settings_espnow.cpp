// settings_espnow.cpp
// Legacy transport glue for SettingsManager.
//
// NOTE (Phase 7 MQTT-only cleanup):
// - ESP-NOW settings transport is disabled.
// - `send_settings_changed_notification()` now republishes MQTT retained state.
// - Legacy ESP-NOW entry points remain as no-op compatibility shims.

#include "settings_manager.h"
#include "../network/mqtt_manager.h"
#include "../config/logging_config.h"
#include <cstring>

bool SettingsManager::apply_settings_update(uint8_t category,
                                            uint8_t field_id,
                                            uint32_t value_uint32,
                                            float value_float,
                                            const char* value_string,
                                            uint32_t* out_new_version,
                                            char* out_error_msg,
                                            size_t out_error_msg_len) {
    const char* safe_value_string = (value_string != nullptr) ? value_string : "";
    bool success = false;
    uint32_t new_version = 0;
    const char* error_msg = "";

    clear_last_apply_failure();
    set_apply_context(category, field_id);

    switch (category) {
        case SETTINGS_BATTERY:
            success = save_battery_setting(field_id, value_uint32, value_float, safe_value_string);
            new_version = battery_settings_version_;
            if (!success) {
                error_msg = "Battery setting rejected";
            }
            break;

        case SETTINGS_POWER:
            success = save_power_setting(field_id, value_uint32);
            new_version = power_settings_version_;
            if (!success) {
                error_msg = "Power setting rejected";
            }
            break;

        case SETTINGS_INVERTER:
            success = save_inverter_setting(field_id, value_uint32);
            new_version = inverter_settings_version_;
            if (!success) {
                error_msg = "Inverter setting rejected";
            }
            break;

        case SETTINGS_CAN:
            success = save_can_setting(field_id, value_uint32);
            new_version = can_settings_version_;
            if (!success) {
                error_msg = "CAN setting rejected";
            }
            break;

        case SETTINGS_CONTACTOR:
            success = save_contactor_setting(field_id, value_uint32);
            new_version = contactor_settings_version_;
            if (!success) {
                error_msg = "Contactor setting rejected";
            }
            break;

        case SETTINGS_CHARGER:
        case SETTINGS_SYSTEM:
        case SETTINGS_MQTT:
        case SETTINGS_NETWORK:
            error_msg = "Category not implemented yet";
            LOG_WARN("SETTINGS", "Category %d not yet implemented", category);
            break;

        default:
            error_msg = "Unknown settings category";
            LOG_ERROR("SETTINGS", "Unknown category: %d", category);
            break;
    }

    if (out_new_version != nullptr) {
        *out_new_version = new_version;
    }

    if (out_error_msg != nullptr && out_error_msg_len > 0) {
        out_error_msg[0] = '\0';
        if (!success) {
            ApplyFailureInfo failure = get_last_apply_failure();
            if (!failure.valid) {
                set_last_apply_failure(category,
                                       field_id,
                                       "FIELD_VALIDATE",
                                       "VALIDATION_FAILED",
                                       error_msg[0] != '\0' ? error_msg : "Field validation rejected update");
                failure = get_last_apply_failure();
            }
            if (failure.valid) {
                if (failure.nvs_key[0] != '\0') {
                    snprintf(out_error_msg,
                             out_error_msg_len,
                             "stage=%s;reason=%s;detail=%s;key=%s",
                             failure.stage,
                             failure.reason_code,
                             failure.detail,
                             failure.nvs_key);
                } else {
                    snprintf(out_error_msg,
                             out_error_msg_len,
                             "stage=%s;reason=%s;detail=%s",
                             failure.stage,
                             failure.reason_code,
                             failure.detail);
                }
            } else if (error_msg[0] != '\0') {
                strlcpy(out_error_msg, error_msg, out_error_msg_len);
            } else {
                strlcpy(out_error_msg, "settings apply failed", out_error_msg_len);
            }
        }
    }

    clear_apply_context();

    return success;
}

void SettingsManager::handle_settings_update(const espnow_queue_msg_t& msg) {
    (void)msg;
    LOG_WARN("SETTINGS", "ESP-NOW settings update ignored (MQTT transport mode)");
}

void SettingsManager::send_settings_ack(const uint8_t* mac,
                                        uint8_t category,
                                        uint8_t field_id,
                                        bool success,
                                        uint32_t new_version,
                                        const char* error_msg) {
    (void)mac;
    LOG_DEBUG("SETTINGS",
              "ESP-NOW settings ACK suppressed (cat=%u field=%u success=%d ver=%lu err=%s)",
              static_cast<unsigned>(category),
              static_cast<unsigned>(field_id),
              success ? 1 : 0,
              static_cast<unsigned long>(new_version),
              error_msg ? error_msg : "");
}

void SettingsManager::send_settings_changed_notification(uint8_t category,
                                                         uint32_t new_version) {
    MqttManager& mqtt = MqttManager::instance();
    const bool meta_ok = mqtt.publish_meta_schema_versions();

    bool model_ok = true;
    switch (category) {
        case SETTINGS_BATTERY:
            model_ok = mqtt.publish_battery_specs() && mqtt.publish_static_power() && mqtt.publish_static_settings();
            break;
        case SETTINGS_POWER:
            model_ok = mqtt.publish_static_power() && mqtt.publish_static_settings();
            break;
        case SETTINGS_CAN:
        case SETTINGS_CONTACTOR:
            model_ok = mqtt.publish_static_settings();
            break;
        case SETTINGS_INVERTER:
            model_ok = mqtt.publish_inverter_specs();
            break;
        case SETTINGS_NETWORK:
            model_ok = mqtt.publish_static_network();
            break;
        case SETTINGS_MQTT:
            model_ok = mqtt.publish_static_mqtt();
            break;
        default:
            // Other categories currently have no dedicated retained model topic.
            model_ok = true;
            break;
    }

    LOG_INFO("SETTINGS",
             "Settings changed via MQTT path (category=%u, version=%lu, meta=%s, model=%s)",
             static_cast<unsigned>(category),
             static_cast<unsigned long>(new_version),
             meta_ok ? "ok" : "fail",
             model_ok ? "ok" : "fail");
}
