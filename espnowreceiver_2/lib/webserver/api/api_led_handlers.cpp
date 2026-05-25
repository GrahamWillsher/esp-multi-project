#include "api_led_handlers.h"
#include <esp32common/mqtt/mqtt_topics_common.h>

#include "api_response_utils.h"
#include "../../src/mqtt/mqtt_client.h"
#include "../utils/transmitter_manager.h"

#include <ArduinoJson.h>

namespace {
constexpr const char* kRefreshLedTopic = mqtt::topics::rx::CMD_REFRESH_LED;

namespace LedStatusCode {
constexpr uint8_t kUnknown = 0;
constexpr uint8_t kOk = 1;
constexpr uint8_t kWarning = 2;
constexpr uint8_t kError = 3;
constexpr uint8_t kUpdating = 4;
}  // namespace LedStatusCode
}

namespace RuntimeState {
extern int current_led_color;
extern int current_led_effect;
extern volatile uint8_t current_led_status;
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

static const char* led_status_name(uint8_t status) {
    switch (status) {
        case LedStatusCode::kOk: return "OK";
        case LedStatusCode::kWarning: return "WARNING";
        case LedStatusCode::kError: return "ERROR";
        case LedStatusCode::kUpdating: return "UPDATING";
        default: return "UNKNOWN";
    }
}

esp_err_t api_get_led_runtime_status_handler(httpd_req_t *req) {
    const uint8_t current_color = static_cast<uint8_t>(RuntimeState::current_led_color);
    const uint8_t current_effect = static_cast<uint8_t>(RuntimeState::current_led_effect);
    const uint8_t current_status = RuntimeState::current_led_status;

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
    doc["current_status"] = static_cast<uint8_t>(current_status);
    doc["current_status_name"] = led_status_name(current_status);
    doc["expected_effect"] = expected_effect;
    doc["expected_effect_name"] = led_effect_name(expected_effect);
    doc["effect_synced"] = synced;

    return ApiResponseUtils::send_json_doc(req, doc);
}

esp_err_t api_resync_led_state_handler(httpd_req_t *req) {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        return ApiResponseUtils::send_error_message(req, "MQTT command channel unavailable");
    }

    StaticJsonDocument<128> cmd;
    cmd["request_id"] = esp_random();
    cmd["model"] = "led";

    char payload[192];
    if (serializeJson(cmd, payload, sizeof(payload)) > 0 &&
        MqttClient::publishJson(kRefreshLedTopic, payload, false)) {
        return ApiResponseUtils::send_success_message(req, "LED state refresh requested");
    }

    return ApiResponseUtils::send_error_message(req, "Failed to publish LED refresh command");
}
