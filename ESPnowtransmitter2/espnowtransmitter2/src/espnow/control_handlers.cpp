#include "control_handlers.h"

#include "tx_send_guard.h"
#include "../network/mqtt_manager.h"
#include "../network/ota_manager.h"
#include "../test_data/test_data_config.h"
#include "../config/logging_config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <mqtt_logger.h>
#include <esp32common/config/timing_config.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

void handle_test_data_mode_control(const debug_control_t* pkt) {
    const char* mode_names[] = {"OFF", "SOC_POWER_ONLY", "FULL_BATTERY_DATA"};

    LOG_INFO("TEST_DATA_CTRL", "Received test data mode change request: %u (%s)",
             pkt->level, pkt->level < 3 ? mode_names[pkt->level] : "INVALID");

    if (pkt->level > 2) {
        LOG_WARN("TEST_DATA_CTRL", "Invalid test data mode: %u (must be 0-2)", pkt->level);
        return;
    }

    TestDataConfig::Config config = TestDataConfig::get_config();
    TestDataConfig::Mode previous_mode = config.mode;

    TestDataConfig::Mode new_mode;
    switch (pkt->level) {
        case 0: new_mode = TestDataConfig::Mode::OFF; break;
        case 1: new_mode = TestDataConfig::Mode::SOC_POWER_ONLY; break;
        case 2: new_mode = TestDataConfig::Mode::FULL_BATTERY_DATA; break;
        default: return;
    }

    config.mode = new_mode;
    TestDataConfig::set_config(config);
    TestDataConfig::apply_config();

    LOG_INFO("TEST_DATA_CTRL", "Test data mode changed: %s → %s",
             TestDataConfig::mode_to_string(previous_mode),
             TestDataConfig::mode_to_string(new_mode));
}

} // namespace

namespace TxControlHandlers {

void handle_reboot(const espnow_queue_msg_t& msg) {
    LOG_INFO("CMD", "REBOOT command from %02X:%02X:%02X:%02X:%02X:%02X",
             msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    LOG_INFO("CMD", ">>> Rebooting in 1 second...");
    Serial.flush();

    MqttManager::instance().disconnect();

    vTaskDelay(pdMS_TO_TICKS(TimingConfig::REBOOT_DELAY_MS));
    ESP.restart();
}

void handle_ota_start(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(ota_start_t)) {
        return;
    }

    const ota_start_t* ota = reinterpret_cast<const ota_start_t*>(msg.data);
    LOG_INFO("CMD", "OTA_START command (size=%u bytes) from %02X:%02X:%02X:%02X:%02X:%02X",
             ota->size, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    if (OtaManager::instance().arm_ota_session_from_control_plane(msg.mac)) {
        LOG_INFO("CMD", ">>> OTA session armed - fetch /api/ota_status (or /api/ota_arm) for challenge, then POST /ota_upload with auth headers");
    } else {
        LOG_ERROR("CMD", "Failed to arm OTA session");
    }
}

void handle_debug_control(const espnow_queue_msg_t& msg, uint8_t* receiver_mac) {
    if (msg.len < (int)sizeof(debug_control_t)) {
        LOG_WARN("DEBUG_CTRL", "Invalid debug_control packet size: %d", msg.len);
        return;
    }

    const debug_control_t* pkt = reinterpret_cast<const debug_control_t*>(msg.data);
    memcpy(receiver_mac, msg.mac, 6);

    if (pkt->flags & 0x80) {
        handle_test_data_mode_control(pkt);
        return;
    }

    LOG_INFO("DEBUG_CTRL", "Received debug level change request: %u", pkt->level);

    if (pkt->level > MQTT_LOG_DEBUG) {
        LOG_WARN("DEBUG_CTRL", "Invalid debug level: %u", pkt->level);
        send_debug_ack(receiver_mac, pkt->level, MQTT_LOG_DEBUG, 1);
        return;
    }

    MqttLogLevel previous = MqttLogger::instance().get_level();
    MqttLogger::instance().set_level((MqttLogLevel)pkt->level);
    save_debug_level(pkt->level);

    LOG_INFO("DEBUG_CTRL", "Debug level changed: %s → %s",
             MqttLogger::instance().level_to_string(previous),
             MqttLogger::instance().level_to_string((MqttLogLevel)pkt->level));

    send_debug_ack(receiver_mac, pkt->level, (uint8_t)previous, 0);
}

void send_debug_ack(const uint8_t* receiver_mac, uint8_t applied, uint8_t previous, uint8_t status) {
    debug_ack_t ack = {
        .type = msg_debug_ack,
        .applied = applied,
        .previous = previous,
        .status = status
    };

    esp_err_t result = TxSendGuard::send_to_receiver_guarded(
        receiver_mac,
        (const uint8_t*)&ack,
        sizeof(ack),
        "debug_ack"
    );

    if (result == ESP_OK) {
        LOG_DEBUG("DEBUG_CTRL", "Debug ACK sent (applied=%u, status=%u)", applied, status);
    } else {
        LOG_WARN("DEBUG_CTRL", "Failed to send debug ACK: %s", esp_err_to_name(result));
    }
}

void save_debug_level(uint8_t level) {
    Preferences prefs;
    if (prefs.begin("debug", false)) {
        prefs.putUChar("log_level", level);
        prefs.end();
        LOG_DEBUG("DEBUG_CTRL", "Debug level saved to NVS: %u", level);
    } else {
        LOG_WARN("DEBUG_CTRL", "Failed to open preferences for debug level save");
    }
}

uint8_t load_debug_level() {
    Preferences prefs;
    uint8_t level = MQTT_LOG_INFO;

    if (prefs.begin("debug", true)) {
        level = prefs.getUChar("log_level", MQTT_LOG_INFO);
        prefs.end();
        LOG_INFO("DEBUG_CTRL", "Debug level loaded from NVS: %u", level);
    } else {
        LOG_INFO("DEBUG_CTRL", "No saved debug level, using default: INFO");
    }

    return level;
}

} // namespace TxControlHandlers
