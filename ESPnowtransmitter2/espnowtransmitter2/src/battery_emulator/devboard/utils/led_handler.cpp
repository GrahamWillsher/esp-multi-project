#include "led_handler.h"
#include "../../datalayer/datalayer.h"
#include "events.h"
#include "../../../config/logging_config.h"
#include "../../../network/mqtt_manager.h"
#include <esp32common/contracts/shared_contracts.h>

/*
 * LED Handler - Simplified Implementation
 * 
 * The Olimex ESP32-POE2 has no physical LED. This module determines
 * the system status and sends it to the receiver as a color value.
 * 
 * The receiver's simulated LED display handles all animation/flashing.
 * We only send:
 *   RED (0)    = Error/Fault
 *   GREEN (1)  = Normal operation
 *   ORANGE (2) = Warning
 *   BLUE (3)   = Updating
 */

static bool s_have_last_led_packet = false;
static uint8_t s_last_color = LED_WIRE_ORANGE;
static uint8_t s_last_effect = 0;

namespace {

uint8_t wire_color_from_status(EMULATOR_STATUS status) {
    switch (status) {
        case EMULATOR_STATUS::STATUS_OK:
            return LED_WIRE_GREEN;
        case EMULATOR_STATUS::STATUS_WARNING:
            return LED_WIRE_ORANGE;
        case EMULATOR_STATUS::STATUS_UPDATING:
            return LED_WIRE_BLUE;
        case EMULATOR_STATUS::STATUS_ERROR:
            return LED_WIRE_RED;
        default:
            return LED_WIRE_ORANGE;
    }
}

uint8_t wire_effect_from_led_mode(uint8_t led_mode) {
    switch (led_mode) {
        case 0:
            return LED_WIRE_CONTINUOUS;  // Classic
        case 1:
            return LED_WIRE_FLASH;       // Energy Flow
        case 2:
            return LED_WIRE_HEARTBEAT;   // Heartbeat
        default:
            return LED_WIRE_CONTINUOUS;
    }
}

const char* led_mode_name(uint8_t led_mode) {
    switch (led_mode) {
        case 0: return "Classic";
        case 1: return "Energy Flow";
        case 2: return "Heartbeat";
        default: return "Unknown";
    }
}

} // namespace

esp_err_t led_publish_current_state(bool force, const uint8_t* receiver_mac) {
    const EMULATOR_STATUS status = get_emulator_status();
    const uint8_t color = wire_color_from_status(status);
    const uint8_t led_mode = static_cast<uint8_t>(datalayer.battery.status.led_mode);
    uint8_t effect = wire_effect_from_led_mode(led_mode);

    // OTA UX override (transmitter): while OTA event is active, keep BLUE
    // color from STATUS_UPDATING and force HEARTBEAT animation so this board
    // is visually distinct from receiver self-OTA.
    const EVENTS_STRUCT_TYPE* ota_event = get_event_pointer(EVENT_OTA_UPDATE);
    const bool ota_active =
        (ota_event != nullptr) &&
        ((ota_event->state == EVENT_STATE_ACTIVE) ||
         (ota_event->state == EVENT_STATE_ACTIVE_LATCHED));
    if (ota_active) {
        effect = LED_WIRE_HEARTBEAT;
    }

    if (!force && s_have_last_led_packet && color == s_last_color && effect == s_last_effect) {
        return ESP_OK;
    }

    const bool ok = MqttManager::instance().publish_runtime_led();
    const esp_err_t result = ok ? ESP_OK : ESP_FAIL;

    if (result == ESP_OK) {
        s_last_color = color;
        s_last_effect = effect;
        s_have_last_led_packet = true;
        LOG_INFO("LED", "Published LED state: status=%s color=%u effect=%u mode=%s%s",
                 get_emulator_status_string(status),
                 color,
                 effect,
                 led_mode_name(led_mode),
                 force ? " [replay]" : "");
    }

    return result;
}

