#include "transmitter_mqtt_query_helper.h"
#include "transmitter_mqtt_cache.h"

namespace TransmitterMqttQueryHelper {

bool is_enabled() {
    return TransmitterMqttCache::is_enabled();
}

const uint8_t* get_server() {
    return TransmitterMqttCache::get_server();
}

uint16_t get_port() {
    return TransmitterMqttCache::get_port();
}

const char* get_username() {
    return TransmitterMqttCache::get_username();
}

const char* get_password() {
    return TransmitterMqttCache::get_password();
}

const char* get_client_id() {
    return TransmitterMqttCache::get_client_id();
}

bool is_connected() {
    return TransmitterMqttCache::is_connected();
}

bool is_config_known() {
    return TransmitterMqttCache::is_config_known();
}

uint32_t get_config_version() {
    return TransmitterMqttCache::get_config_version();
}

String get_server_string() {
    return TransmitterMqttCache::get_server_string();
}

} // namespace TransmitterMqttQueryHelper
