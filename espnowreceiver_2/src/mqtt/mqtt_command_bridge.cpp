#include "mqtt_command_bridge.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp32common/mqtt/mqtt_topics_common.h>

#include "mqtt_client.h"

namespace {
uint8_t g_last_debug_level = 6;
uint8_t g_last_test_data_mode = 0;
}

uint8_t get_last_debug_level() {
    return g_last_debug_level;
}

uint8_t get_last_test_data_mode() {
    return g_last_test_data_mode;
}

void cache_last_debug_level(uint8_t level) {
    if (level <= 7) {
        g_last_debug_level = level;
    }
}

bool send_test_data_mode_control(uint8_t mode) {
    if (mode > 2) {
        return false;
    }

    g_last_test_data_mode = mode;

    if (!(MqttClient::isEnabled() && MqttClient::isConnected())) {
        return false;
    }

    StaticJsonDocument<128> cmd;
    char request_id[32];
    snprintf(request_id, sizeof(request_id), "tmode-%lu", static_cast<unsigned long>(millis()));
    cmd["request_id"] = request_id;
    cmd["origin"] = "receiver_tft";
    cmd["schema"] = 1;
    cmd["mode"] = mode;

    char payload[128];
    if (serializeJson(cmd, payload, sizeof(payload)) == 0) {
        return false;
    }

    return MqttClient::publishJson(mqtt::topics::rx::CMD_CONTROL_TEST_DATA_MODE, payload, false);
}
