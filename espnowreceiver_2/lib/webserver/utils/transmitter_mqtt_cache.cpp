#include "transmitter_mqtt_cache.h"

#include <Preferences.h>
#include <string.h>

namespace {
    constexpr const char* kKeyMqttEnabled = "mqtt_enabled";
    constexpr const char* kKeyMqttServer = "mqtt_server";
    constexpr const char* kKeyMqttPort = "mqtt_port";
    constexpr const char* kKeyMqttUser = "mqtt_user";
    constexpr const char* kKeyMqttPass = "mqtt_pass";
    constexpr const char* kKeyMqttClient = "mqtt_client";
    constexpr const char* kKeyMqttVersion = "mqtt_ver";
    constexpr const char* kKeyMqttKnown = "mqtt_known";

    struct MqttCache {
        bool mqtt_enabled = false;
        uint8_t mqtt_server[4] = {0, 0, 0, 0};
        uint16_t mqtt_port = 1883;
        char mqtt_username[32] = {0};
        char mqtt_password[32] = {0};
        char mqtt_client_id[32] = {0};
        bool mqtt_connected = false;
        uint32_t mqtt_config_version = 0;
        bool mqtt_config_known = false;
    };

    MqttCache mqtt_cache;
}

namespace TransmitterMqttCache {

void load_from_prefs(void* prefs_ptr) {
    if (prefs_ptr == nullptr) return;
    Preferences& prefs = *static_cast<Preferences*>(prefs_ptr);

    mqtt_cache.mqtt_enabled = prefs.getBool(kKeyMqttEnabled, false);
    prefs.getBytes(kKeyMqttServer, mqtt_cache.mqtt_server, sizeof(mqtt_cache.mqtt_server));
    mqtt_cache.mqtt_port = prefs.getUShort(kKeyMqttPort, 1883);

    String mqtt_user = prefs.getString(kKeyMqttUser, "");
    String mqtt_pass = prefs.getString(kKeyMqttPass, "");
    String mqtt_client = prefs.getString(kKeyMqttClient, "");

    strncpy(mqtt_cache.mqtt_username, mqtt_user.c_str(), sizeof(mqtt_cache.mqtt_username) - 1);
    strncpy(mqtt_cache.mqtt_password, mqtt_pass.c_str(), sizeof(mqtt_cache.mqtt_password) - 1);
    strncpy(mqtt_cache.mqtt_client_id, mqtt_client.c_str(), sizeof(mqtt_cache.mqtt_client_id) - 1);

    mqtt_cache.mqtt_username[sizeof(mqtt_cache.mqtt_username) - 1] = '\0';
    mqtt_cache.mqtt_password[sizeof(mqtt_cache.mqtt_password) - 1] = '\0';
    mqtt_cache.mqtt_client_id[sizeof(mqtt_cache.mqtt_client_id) - 1] = '\0';

    mqtt_cache.mqtt_config_version = prefs.getUInt(kKeyMqttVersion, 0);
    mqtt_cache.mqtt_config_known = prefs.getBool(kKeyMqttKnown, false);
}

void save_to_prefs(void* prefs_ptr) {
    if (prefs_ptr == nullptr) return;
    Preferences& prefs = *static_cast<Preferences*>(prefs_ptr);

    prefs.putBool(kKeyMqttEnabled, mqtt_cache.mqtt_enabled);
    prefs.putBytes(kKeyMqttServer, mqtt_cache.mqtt_server, sizeof(mqtt_cache.mqtt_server));
    prefs.putUShort(kKeyMqttPort, mqtt_cache.mqtt_port);
    prefs.putString(kKeyMqttUser, mqtt_cache.mqtt_username);
    prefs.putString(kKeyMqttPass, mqtt_cache.mqtt_password);
    prefs.putString(kKeyMqttClient, mqtt_cache.mqtt_client_id);
    prefs.putUInt(kKeyMqttVersion, mqtt_cache.mqtt_config_version);
    prefs.putBool(kKeyMqttKnown, mqtt_cache.mqtt_config_known);
}

void store_config(bool enabled,
                  const uint8_t* server,
                  uint16_t port,
                  const char* username,
                  const char* password,
                  const char* client_id,
                  uint32_t version) {
    mqtt_cache.mqtt_enabled = enabled;

    if (server != nullptr) {
        memcpy(mqtt_cache.mqtt_server, server, 4);
    }

    mqtt_cache.mqtt_port = port;

    if (username != nullptr) {
        strncpy(mqtt_cache.mqtt_username, username, sizeof(mqtt_cache.mqtt_username) - 1);
        mqtt_cache.mqtt_username[sizeof(mqtt_cache.mqtt_username) - 1] = '\0';
    }

    if (password != nullptr) {
        strncpy(mqtt_cache.mqtt_password, password, sizeof(mqtt_cache.mqtt_password) - 1);
        mqtt_cache.mqtt_password[sizeof(mqtt_cache.mqtt_password) - 1] = '\0';
    }

    if (client_id != nullptr) {
        strncpy(mqtt_cache.mqtt_client_id, client_id, sizeof(mqtt_cache.mqtt_client_id) - 1);
        mqtt_cache.mqtt_client_id[sizeof(mqtt_cache.mqtt_client_id) - 1] = '\0';
    }

    mqtt_cache.mqtt_config_version = version;
    mqtt_cache.mqtt_config_known = true;
}

bool is_enabled() {
    return mqtt_cache.mqtt_enabled;
}

const uint8_t* get_server() {
    return mqtt_cache.mqtt_config_known ? mqtt_cache.mqtt_server : nullptr;
}

uint16_t get_port() {
    return mqtt_cache.mqtt_port;
}

const char* get_username() {
    return mqtt_cache.mqtt_username;
}

const char* get_password() {
    return mqtt_cache.mqtt_password;
}

const char* get_client_id() {
    return mqtt_cache.mqtt_client_id;
}

bool is_connected() {
    return mqtt_cache.mqtt_connected;
}

bool is_config_known() {
    return mqtt_cache.mqtt_config_known;
}

uint32_t get_config_version() {
    return mqtt_cache.mqtt_config_version;
}

String get_server_string() {
    if (!mqtt_cache.mqtt_config_known) return "0.0.0.0";

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d",
             mqtt_cache.mqtt_server[0], mqtt_cache.mqtt_server[1], mqtt_cache.mqtt_server[2], mqtt_cache.mqtt_server[3]);
    return String(buffer);
}

bool update_runtime_connection(bool mqtt_connected) {
    bool changed = (mqtt_cache.mqtt_connected != mqtt_connected);
    mqtt_cache.mqtt_connected = mqtt_connected;
    return changed;
}

} // namespace TransmitterMqttCache
