#include "api_led_handlers.h"

#include "api_response_utils.h"
#include "../utils/transmitter_manager.h"

#include <esp_now.h>
#include <esp32common/espnow/common.h>

namespace ESPNow {
extern uint8_t current_led_color;
extern uint8_t current_led_effect;
}

static const char* led_color_name(uint8_t color) {
    switch (color) {
        case 0: return "RED";
        case 1: return "GREEN";
        case 2: return "ORANGE";
        case 3: return "BLUE";
        default: return "UNKNOWN";
    }
}

static const char* led_effect_name(uint8_t effect) {
    switch (effect) {
        case 0: return "CONTINUOUS";
        case 1: return "FLASH";
        case 2: return "HEARTBEAT";
        default: return "UNKNOWN";
    }
}

esp_err_t api_get_led_runtime_status_handler(httpd_req_t *req) {
    const uint8_t current_color = ESPNow::current_led_color;
    const uint8_t current_effect = ESPNow::current_led_effect;

    const bool has_policy = TransmitterManager::hasBatteryEmulatorSettings();
    const uint8_t led_mode = has_policy ? TransmitterManager::getBatteryEmulatorSettings().led_mode : 0;
    const uint8_t expected_effect = (led_mode <= 2) ? led_mode : 0;

    const bool synced = has_policy && (static_cast<uint8_t>(current_effect) == expected_effect);

    StaticJsonDocument<256> doc;
    doc["success"] = true;
    doc["led_mode"] = led_mode;
    doc["has_led_policy"] = has_policy;
    doc["current_color"] = static_cast<uint8_t>(current_color);
    doc["current_color_name"] = led_color_name(current_color);
    doc["current_effect"] = static_cast<uint8_t>(current_effect);
    doc["current_effect_name"] = led_effect_name(current_effect);
    doc["expected_effect"] = expected_effect;
    doc["expected_effect_name"] = led_effect_name(expected_effect);
    doc["effect_synced"] = synced;

    return ApiResponseUtils::send_json_doc(req, doc);
}

esp_err_t api_resync_led_state_handler(httpd_req_t *req) {
    if (!TransmitterManager::isMACKnown()) {
        return ApiResponseUtils::send_transmitter_mac_unknown(req);
    }

    config_section_request_t request;
    request.type = msg_config_section_request;
    request.section = config_section_battery;
    request.requested_version = 0;

    const esp_err_t result = esp_now_send(
        TransmitterManager::getMAC(),
        reinterpret_cast<const uint8_t*>(&request),
        sizeof(request)
    );

    if (result == ESP_OK) {
        return ApiResponseUtils::send_success_message(req, "Battery section resync requested");
    } else {
        return ApiResponseUtils::send_jsonf(req, "{\"success\":false,\"message\":\"ESP-NOW send failed: %s\"}", esp_err_to_name(result));
    }
}
