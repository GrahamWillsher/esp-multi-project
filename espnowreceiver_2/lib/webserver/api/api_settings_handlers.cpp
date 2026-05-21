#include "api_settings_handlers.h"

#include "api_request_utils.h"
#include "api_response_utils.h"
#include "../../src/mqtt/mqtt_client.h"
#include "../utils/transmitter_manager.h"
#include "../logging.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/common.h>
#include <esp32common/mqtt/mqtt_feature_flags.h>
#include <cstring>

namespace {
constexpr const char* MQTT_TOPIC_RX_CMD_SETTINGS_UPDATE = "batt-emu/mqtt-v1/rx/cmd/update/battery";
constexpr const char* MQTT_TOPIC_TX_ACK_SETTINGS_UPDATE = "batt-emu/mqtt-v1/tx/ack/battery";
constexpr const char* MQTT_TOPIC_RX_CMD_REFRESH_SETTINGS = "batt-emu/mqtt-v1/rx/cmd/refresh/settings";
}

esp_err_t api_get_battery_settings_handler(httpd_req_t *req) {
    bool requested = false;
#if MQTT_FEATURE_COMMANDS
    if (MqttClient::isEnabled() && MqttClient::isConnected()) {
        StaticJsonDocument<96> cmd;
        char request_id[32];
        snprintf(request_id, sizeof(request_id), "refresh-batt-%lu", static_cast<unsigned long>(millis()));
        cmd["request_id"] = request_id;
        cmd["origin"] = "receiver_2";
        cmd["schema"] = 1;

        char payload[96];
        if (serializeJson(cmd, payload, sizeof(payload)) > 0) {
            requested = MqttClient::publishJson(MQTT_TOPIC_RX_CMD_REFRESH_SETTINGS, payload, false);
            if (!requested) {
                LOG_WARN("API", "Failed to publish battery settings refresh command via MQTT");
            }
        }
    }
#endif

    const bool known = TransmitterManager::hasBatterySettings();
    auto settings = TransmitterManager::getBatterySettings();
    auto power_settings = TransmitterManager::getPowerSettings();
    auto can_settings = TransmitterManager::getCanSettings();
    auto contactor_settings = TransmitterManager::getContactorSettings();
    const uint8_t led_mode = TransmitterManager::hasBatteryEmulatorSettings()
        ? TransmitterManager::getBatteryEmulatorSettings().led_mode
        : 0;

    StaticJsonDocument<512> doc;
    doc["success"]                   = known;
    doc["requested"]                 = requested;
    doc["capacity_wh"]               = settings.capacity_wh;
    doc["max_voltage_mv"]            = settings.max_voltage_mv;
    doc["min_voltage_mv"]            = settings.min_voltage_mv;
    doc["max_charge_current_a"]      = serialized(String(settings.max_charge_current_a, 1));
    doc["max_discharge_current_a"]   = serialized(String(settings.max_discharge_current_a, 1));
    doc["soc_high_limit"]            = settings.soc_high_limit;
    doc["soc_low_limit"]             = settings.soc_low_limit;
    doc["cell_count"]                = settings.cell_count;
    doc["chemistry"]                 = settings.chemistry;
    doc["led_mode"]                  = led_mode;
    doc["max_precharge_ms"]          = power_settings.max_precharge_ms;
    doc["precharge_duration_ms"]     = power_settings.precharge_duration_ms;
    doc["external_precharge_enabled"] = power_settings.external_precharge_enabled;
    doc["no_inverter_disconnect_contactor"] = power_settings.no_inverter_disconnect_contactor;
    doc["can_frequency_khz"]         = can_settings.frequency_khz;
    doc["can_fd_frequency_mhz"]      = can_settings.fd_frequency_mhz;
    doc["use_canfd_as_classic"]      = can_settings.use_canfd_as_classic;
    doc["contactor_control_enabled"]  = contactor_settings.control_enabled;
    doc["contactor_nc_mode"]          = contactor_settings.nc_contactor;
    doc["contactor_pwm_frequency_hz"] = contactor_settings.pwm_frequency_hz;
    doc["contactor_pwm_enabled"]      = contactor_settings.pwm_control_enabled;
    doc["contactor_pwm_hold_duty"]    = contactor_settings.pwm_hold_duty;
    doc["periodic_bms_reset"]         = contactor_settings.periodic_bms_reset;
    doc["bms_first_align_enabled"]    = contactor_settings.bms_first_align_enabled;
    doc["bms_first_align_target_minutes"] = contactor_settings.bms_first_align_target_minutes;
    doc["equipment_stop_type"]        = power_settings.equipment_stop_type;

    return ApiResponseUtils::send_json_doc(req, doc);
}

esp_err_t api_save_setting_handler(httpd_req_t *req) {
    char buf[512];

    LOG_INFO("API", "===== API SAVE SETTING CALLED =====");
    LOG_INFO("API", "save_setting called, content_len=%d", req->content_len);

    StaticJsonDocument<256> doc;
    esp_err_t response_error = ESP_OK;
    const char* parse_error = nullptr;
    if (!ApiRequestUtils::read_json_body_or_respond(req, buf, sizeof(buf), doc, &response_error, &parse_error)) {
        if (parse_error) {
            LOG_ERROR("API", "JSON parse error: %s", parse_error);
        }
        return response_error;
    }

    LOG_INFO("API", "Received JSON: %s", buf);

    if (!doc.containsKey("category") || !doc.containsKey("field") || !doc.containsKey("value")) {
        LOG_ERROR("API", "Missing required fields in JSON");
        return ApiResponseUtils::send_error_message(req, "Missing required fields (category, field, value)");
    }

    uint8_t category = doc["category"];
    uint8_t field = doc["field"];

    LOG_INFO("API", "Parsed - category=%d, field=%d", category, field);

    settings_update_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = msg_battery_settings_update;
    msg.category = category;
    msg.field_id = field;

    JsonVariant value = doc["value"];
    if (value.isNull()) {
        LOG_ERROR("API", "Null value is not allowed for save_setting (category=%u, field=%u)", category, field);
        return ApiResponseUtils::send_error_message(req, "Invalid null value for field update");
    } else if (value.is<bool>()) {
        msg.value_uint32 = value.as<bool>() ? 1u : 0u;
        LOG_INFO("API", "Value type=bool, value=%u", msg.value_uint32);
    } else if (value.is<int>() || value.is<uint32_t>()) {
        msg.value_uint32 = value.as<uint32_t>();
        LOG_INFO("API", "Value type=uint32, value=%u", msg.value_uint32);
    } else if (value.is<float>() || value.is<double>()) {
        msg.value_float = value.as<float>();
        LOG_INFO("API", "Value type=float, value=%.2f", msg.value_float);
    } else if (value.is<const char*>()) {
        strncpy(msg.value_string, value.as<const char*>(), sizeof(msg.value_string) - 1);
        LOG_INFO("API", "Value type=string, value=%s", msg.value_string);
    } else {
        LOG_ERROR("API", "Unsupported value type in save_setting request (category=%u, field=%u)", category, field);
        return ApiResponseUtils::send_error_message(req, "Unsupported value type for field update");
    }

    LOG_INFO("API", "Message prepared - type=%d, category=%d, field=%d, size=%d bytes",
             msg.type, msg.category, msg.field_id, static_cast<int>(sizeof(msg)));

    auto apply_local_cache = [&]() {
        if (category == SETTINGS_BATTERY) {
            auto emu = TransmitterManager::getBatteryEmulatorSettings();
            switch (field) {
                case BATTERY_DOUBLE_ENABLED: emu.double_battery = msg.value_uint32 ? true : false; break;
                case BATTERY_PACK_MAX_VOLTAGE_DV: emu.pack_max_voltage_dV = msg.value_uint32; break;
                case BATTERY_PACK_MIN_VOLTAGE_DV: emu.pack_min_voltage_dV = msg.value_uint32; break;
                case BATTERY_CELL_MAX_VOLTAGE_MV: emu.cell_max_voltage_mV = msg.value_uint32; break;
                case BATTERY_CELL_MIN_VOLTAGE_MV: emu.cell_min_voltage_mV = msg.value_uint32; break;
                case BATTERY_SOC_ESTIMATED: emu.soc_estimated = msg.value_uint32 ? true : false; break;
                case BATTERY_LED_MODE: emu.led_mode = msg.value_uint32; break;
                default: break;
            }
            TransmitterManager::storeBatteryEmulatorSettings(emu);
        } else if (category == SETTINGS_POWER) {
            auto power = TransmitterManager::getPowerSettings();
            switch (field) {
                case POWER_CHARGE_W: power.charge_w = msg.value_uint32; break;
                case POWER_DISCHARGE_W: power.discharge_w = msg.value_uint32; break;
                case POWER_MAX_PRECHARGE_MS: power.max_precharge_ms = msg.value_uint32; break;
                case POWER_PRECHARGE_DURATION_MS: power.precharge_duration_ms = msg.value_uint32; break;
                case POWER_EQUIPMENT_STOP_TYPE: power.equipment_stop_type = static_cast<uint8_t>(msg.value_uint32); break;
                case POWER_EXTERNAL_PRECHARGE_ENABLED: power.external_precharge_enabled = msg.value_uint32 ? true : false; break;
                case POWER_NO_INVERTER_DISCONNECT_CONTACTOR: power.no_inverter_disconnect_contactor = msg.value_uint32 ? true : false; break;
                default: break;
            }
            TransmitterManager::storePowerSettings(power);
        } else if (category == SETTINGS_INVERTER) {
            auto inverter = TransmitterManager::getInverterSettings();
            switch (field) {
                case INVERTER_CELLS: inverter.cells = msg.value_uint32; break;
                case INVERTER_MODULES: inverter.modules = msg.value_uint32; break;
                case INVERTER_CELLS_PER_MODULE: inverter.cells_per_module = msg.value_uint32; break;
                case INVERTER_VOLTAGE_LEVEL: inverter.voltage_level = msg.value_uint32; break;
                case INVERTER_CAPACITY_AH: inverter.capacity_ah = msg.value_uint32; break;
                case INVERTER_BATTERY_TYPE: inverter.battery_type = msg.value_uint32; break;
                default: break;
            }
            TransmitterManager::storeInverterSettings(inverter);
        } else if (category == SETTINGS_CAN) {
            auto can = TransmitterManager::getCanSettings();
            switch (field) {
                case CAN_FREQUENCY_KHZ: can.frequency_khz = msg.value_uint32; break;
                case CAN_FD_FREQUENCY_MHZ: can.fd_frequency_mhz = msg.value_uint32; break;
                case CAN_SOFAR_ID: can.sofar_id = msg.value_uint32; break;
                case CAN_PYLON_SEND_INTERVAL_MS: can.pylon_send_interval_ms = msg.value_uint32; break;
                case CAN_USE_CANFD_AS_CLASSIC: can.use_canfd_as_classic = msg.value_uint32 ? true : false; break;
                default: break;
            }
            TransmitterManager::storeCanSettings(can);
        } else if (category == SETTINGS_CONTACTOR) {
            auto contactor = TransmitterManager::getContactorSettings();
            switch (field) {
                case CONTACTOR_CONTROL_ENABLED: contactor.control_enabled = msg.value_uint32 ? true : false; break;
                case CONTACTOR_NC_MODE: contactor.nc_contactor = msg.value_uint32 ? true : false; break;
                case CONTACTOR_PWM_FREQUENCY_HZ: contactor.pwm_frequency_hz = msg.value_uint32; break;
                case CONTACTOR_PWM_ENABLED: contactor.pwm_control_enabled = msg.value_uint32 ? true : false; break;
                case CONTACTOR_PWM_HOLD_DUTY: contactor.pwm_hold_duty = msg.value_uint32; break;
                case CONTACTOR_PERIODIC_BMS_RESET: contactor.periodic_bms_reset = msg.value_uint32 ? true : false; break;
                case CONTACTOR_BMS_FIRST_ALIGN_ENABLED: contactor.bms_first_align_enabled = msg.value_uint32 ? true : false; break;
                case CONTACTOR_BMS_FIRST_ALIGN_TARGET_MINUTES: contactor.bms_first_align_target_minutes = msg.value_uint32; break;
                default: break;
            }
            TransmitterManager::storeContactorSettings(contactor);
        }
    };

    bool mqtt_sent = false;

#if MQTT_FEATURE_COMMANDS
    if (MqttClient::isEnabled() && MqttClient::isConnected()) {
        StaticJsonDocument<256> cmd;
        char request_id[32];
        snprintf(request_id, sizeof(request_id), "set-%lu", static_cast<unsigned long>(millis()));
        cmd["request_id"] = request_id;
        cmd["origin"] = "receiver_2";
        cmd["schema"] = 1;
        cmd["category"] = category;
        cmd["field"] = field;

        const bool battery_float_field =
            (category == SETTINGS_BATTERY) &&
            (field == BATTERY_MAX_CHARGE_CURRENT_A || field == BATTERY_MAX_DISCHARGE_CURRENT_A);
        if (battery_float_field) {
            cmd["value"] = value.as<float>();
        } else {
            cmd["value"] = value;
        }

        char payload[256];
        const size_t n = serializeJson(cmd, payload, sizeof(payload));
        if (n > 0) {
            char ack_payload[384] = {0};
            mqtt_sent = MqttClient::publishJsonAndWaitForAck(
                MQTT_TOPIC_RX_CMD_SETTINGS_UPDATE,
                payload,
                request_id,
                MQTT_TOPIC_TX_ACK_SETTINGS_UPDATE,
                TimingConfig::SETTINGS_UPDATE_DELAY_MS,
                ack_payload,
                sizeof(ack_payload));

            if (mqtt_sent) {
                StaticJsonDocument<256> ack_doc;
                if (deserializeJson(ack_doc, ack_payload) == DeserializationError::Ok) {
                    const bool success = ack_doc["success"] | false;
                    const char* message = ack_doc["message"] | "";
                    if (success) {
                        LOG_INFO("API", "✓ MQTT settings ACK received (category=%d, field=%d)", category, field);
                        apply_local_cache();
                        return ApiResponseUtils::send_jsonf(req,
                                                            "{\"success\":true,\"message\":\"Setting applied via MQTT\",\"category\":%u,\"field\":%u}",
                                                            static_cast<unsigned>(category),
                                                            static_cast<unsigned>(field));
                    }

                    LOG_WARN("API", "MQTT settings ACK reported failure: %s", message);
                    return ApiResponseUtils::send_jsonf(req,
                                                        "{\"success\":false,\"error\":\"%s\"}",
                                                        message[0] != '\0' ? message : "MQTT settings update failed");
                }

                LOG_WARN("API", "MQTT settings ACK received but could not be parsed");
                return ApiResponseUtils::send_success_message(req, "Setting command sent via MQTT");
            }
        }
    }
#endif

    (void)mqtt_sent;
    LOG_WARN("API", "MQTT command channel unavailable for settings update");
    return ApiResponseUtils::send_error_message(req, "MQTT command channel unavailable");
}