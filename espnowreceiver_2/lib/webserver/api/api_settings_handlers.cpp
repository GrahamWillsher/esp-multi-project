#include "api_settings_handlers.h"

#include "api_request_utils.h"
#include "api_response_utils.h"
#include "../utils/transmitter_manager.h"
#include <webserver_common_utils/http_json_utils.h>
#include "../logging.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <esp32common/espnow/common.h>
#include <cstring>

esp_err_t api_get_battery_settings_handler(httpd_req_t *req) {
    char json[512];

    bool requested = false;
    if (TransmitterManager::isMACKnown()) {
        request_data_t req_msg = { msg_request_data, subtype_battery_config };
        esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&req_msg, sizeof(req_msg));
        if (result == ESP_OK) {
            requested = true;
            LOG_DEBUG("API", "Requested battery settings from transmitter");
        } else {
            LOG_WARN("API", "Failed to request battery settings: %s", esp_err_to_name(result));
        }
    }

    const bool known = TransmitterManager::hasBatterySettings();
    auto settings = TransmitterManager::getBatterySettings();
    const uint8_t led_mode = TransmitterManager::hasBatteryEmulatorSettings()
        ? TransmitterManager::getBatteryEmulatorSettings().led_mode
        : 0;

    snprintf(json, sizeof(json),
        "{"
        "\"success\":%s,"
        "\"requested\":%s,"
        "\"capacity_wh\":%u,"
        "\"max_voltage_mv\":%u,"
        "\"min_voltage_mv\":%u,"
        "\"max_charge_current_a\":%.1f,"
        "\"max_discharge_current_a\":%.1f,"
        "\"soc_high_limit\":%u,"
        "\"soc_low_limit\":%u,"
        "\"cell_count\":%u,"
        "\"chemistry\":%u,"
        "\"led_mode\":%u"
        "}",
        known ? "true" : "false",
        requested ? "true" : "false",
        settings.capacity_wh,
        settings.max_voltage_mv,
        settings.min_voltage_mv,
        settings.max_charge_current_a,
        settings.max_discharge_current_a,
        settings.soc_high_limit,
        settings.soc_low_limit,
        settings.cell_count,
        settings.chemistry,
        led_mode
    );

    return HttpJsonUtils::send_json(req, json);
}

esp_err_t api_save_setting_handler(httpd_req_t *req) {
    char buf[512];

    LOG_INFO("API: ===== API SAVE SETTING CALLED =====");
    LOG_INFO("API: save_setting called, content_len=%d", req->content_len);

    StaticJsonDocument<256> doc;
    esp_err_t response_error = ESP_OK;
    const char* parse_error = nullptr;
    if (!ApiRequestUtils::read_json_body_or_respond(req, buf, sizeof(buf), doc, &response_error, &parse_error)) {
        if (parse_error) {
            LOG_ERROR("API: JSON parse error: %s", parse_error);
        }
        return response_error;
    }

    LOG_INFO("API: Received JSON: %s", buf);

    if (!doc.containsKey("category") || !doc.containsKey("field") || !doc.containsKey("value")) {
        LOG_ERROR("API: Missing required fields in JSON");
        return ApiResponseUtils::send_error_message(req, "Missing required fields (category, field, value)");
    }

    uint8_t category = doc["category"];
    uint8_t field = doc["field"];

    LOG_INFO("API: Parsed - category=%d, field=%d", category, field);

    settings_update_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = msg_battery_settings_update;
    msg.category = category;
    msg.field_id = field;

    JsonVariant value = doc["value"];
    if (value.is<bool>()) {
        msg.value_uint32 = value.as<bool>() ? 1u : 0u;
        LOG_INFO("API: Value type=bool, value=%u", msg.value_uint32);
    } else if (value.is<int>() || value.is<uint32_t>()) {
        msg.value_uint32 = value.as<uint32_t>();
        LOG_INFO("API: Value type=uint32, value=%u", msg.value_uint32);
    } else if (value.is<float>() || value.is<double>()) {
        msg.value_float = value.as<float>();
        LOG_INFO("API: Value type=float, value=%.2f", msg.value_float);
    } else if (value.is<const char*>()) {
        strncpy(msg.value_string, value.as<const char*>(), sizeof(msg.value_string) - 1);
        LOG_INFO("API: Value type=string, value=%s", msg.value_string);
    } else {
        LOG_ERROR("API: Unsupported value type in save_setting request");
        return ApiResponseUtils::send_error_message(req, "Unsupported value type for field update");
    }

    msg.checksum = 0;
    uint8_t* bytes = (uint8_t*)&msg;
    for (size_t i = 0; i < sizeof(msg) - sizeof(msg.checksum); i++) {
        msg.checksum ^= bytes[i];
    }

    LOG_INFO("API: Message prepared - type=%d, category=%d, field=%d, checksum=%u, size=%d bytes",
             msg.type, msg.category, msg.field_id, msg.checksum, sizeof(msg));

    if (!TransmitterManager::isMACKnown()) {
        LOG_ERROR("API: Transmitter not connected");
        return ApiResponseUtils::send_error_message(req, "Transmitter not connected");
    }

    LOG_INFO("API: Sending to transmitter MAC: %s", TransmitterManager::getMACString().c_str());

    esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&msg, sizeof(msg));
    if (result == ESP_OK) {
        LOG_INFO("API: ✓ ESP-NOW send SUCCESS (category=%d, field=%d)", category, field);
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
                default: break;
            }
            TransmitterManager::storeCanSettings(can);
        } else if (category == SETTINGS_CONTACTOR) {
            auto contactor = TransmitterManager::getContactorSettings();
            switch (field) {
                case CONTACTOR_CONTROL_ENABLED: contactor.control_enabled = msg.value_uint32 ? true : false; break;
                case CONTACTOR_NC_MODE: contactor.nc_contactor = msg.value_uint32 ? true : false; break;
                case CONTACTOR_PWM_FREQUENCY_HZ: contactor.pwm_frequency_hz = msg.value_uint32; break;
                default: break;
            }
            TransmitterManager::storeContactorSettings(contactor);
        }
        return ApiResponseUtils::send_success_message(req, "Setting sent to transmitter");
    } else {
        LOG_ERROR("API: ✗ ESP-NOW send FAILED: %s (0x%x)", esp_err_to_name(result), result);
        LOG_ERROR("API: Failed details - category=%d, field=%d, msg_size=%d", category, field, sizeof(msg));
        return ApiResponseUtils::send_jsonf(req, "{\"success\":false,\"message\":\"ESP-NOW send failed: %s\"}", esp_err_to_name(result));
    }
}
