#ifndef TRANSMITTER_MQTT_CACHE_H
#define TRANSMITTER_MQTT_CACHE_H

#include <Arduino.h>

namespace TransmitterMqttCache {

void load_from_prefs(void* prefs_ptr);
void save_to_prefs(void* prefs_ptr);

void store_config(bool enabled,
                  const uint8_t* server,
                  uint16_t port,
                  const char* username,
                  const char* password,
                  const char* client_id,
                  uint32_t version);

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

bool update_runtime_connection(bool mqtt_connected);

} // namespace TransmitterMqttCache

#endif // TRANSMITTER_MQTT_CACHE_H
