#include "control_state_compat.h"

#include "logging_config.h"
#include "mqtt_client.h"
#include "mqtt_command_client.h"

namespace {
uint8_t g_last_debug_level = 6;
uint8_t g_last_test_data_mode = 0;
}

uint8_t get_last_debug_level() {
    return g_last_debug_level;
}

void set_last_debug_level(uint8_t level) {
    g_last_debug_level = (level <= 7) ? level : 7;
}

uint8_t get_last_test_data_mode() {
    return g_last_test_data_mode;
}

bool send_test_data_mode_control(uint8_t mode) {
    if (mode > 2) {
        LOG_ERROR("MQTT", "Invalid test data mode: %u (must be 0-2)", static_cast<unsigned>(mode));
        return false;
    }

    g_last_test_data_mode = mode;

    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        LOG_WARN("MQTT", "MQTT command channel unavailable for test data mode control");
        return false;
    }

    const bool sent = MqttCommandClient::sendTestDataMode(mode);
    if (!sent) {
        LOG_WARN("MQTT", "Failed to publish test data mode control");
    }
    return sent;
}

bool send_event_logs_clear_request() {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        LOG_WARN("MQTT", "MQTT command channel unavailable for event log clear request");
        return false;
    }

    const bool sent = MqttCommandClient::sendEventLogsClear();
    if (!sent) {
        LOG_WARN("MQTT", "Failed to publish event log clear request");
    }
    return sent;
}
