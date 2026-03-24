#ifndef TRANSMITTER_MQTT_SPECS_H
#define TRANSMITTER_MQTT_SPECS_H

#include <Arduino.h>
#include <ArduinoJson.h>

namespace TransmitterMqttSpecs {

void load_from_prefs(void* prefs_ptr);
void save_to_prefs(void* prefs_ptr);

void store_mqtt_config(bool enabled,
                       const uint8_t* server,
                       uint16_t port,
                       const char* username,
                       const char* password,
                       const char* client_id,
                       bool connected,
                       uint32_t version,
                       bool persist = true);

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

void store_static_specs(const JsonObject& specs);
void store_battery_specs(const JsonObject& specs);
void store_inverter_specs(const JsonObject& specs);
void store_charger_specs(const JsonObject& specs);
void store_system_specs(const JsonObject& specs);

bool has_static_specs();
String get_static_specs_json();
String get_battery_specs_json();
String get_inverter_specs_json();
String get_charger_specs_json();
String get_system_specs_json();

} // namespace TransmitterMqttSpecs

#endif // TRANSMITTER_MQTT_SPECS_H
