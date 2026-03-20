#ifndef TRANSMITTER_MQTT_QUERY_HELPER_H
#define TRANSMITTER_MQTT_QUERY_HELPER_H

#include <Arduino.h>
#include <cstdint>

namespace TransmitterMqttQueryHelper {

    bool is_enabled();
    const uint8_t* get_server();
    uint16_t get_port();
    const char* get_username();
    const char* get_password();
    const char* get_client_id();
    bool is_connected();
    bool is_config_known();
    uint32_t get_config_version();
    String get_server_string();

} // namespace TransmitterMqttQueryHelper

#endif // TRANSMITTER_MQTT_QUERY_HELPER_H
