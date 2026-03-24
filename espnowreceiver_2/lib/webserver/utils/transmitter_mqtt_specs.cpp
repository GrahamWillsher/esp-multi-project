#include "transmitter_mqtt_specs.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#include "../logging.h"
#include "transmitter_settings_cache.h"
#include "transmitter_nvs_persistence.h"

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

struct ScopedMutex {
    explicit ScopedMutex(SemaphoreHandle_t mutex)
        : mutex_(mutex), locked_(false) {
        if (mutex_ != nullptr) {
            locked_ = (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE);
        }
    }

    ~ScopedMutex() {
        if (locked_) {
            xSemaphoreGive(mutex_);
        }
    }

    bool locked() const { return locked_; }

private:
    SemaphoreHandle_t mutex_;
    bool locked_;
};

struct SpecCache {
    String static_specs_json;
    String battery_specs_json;
    String inverter_specs_json;
    String charger_specs_json;
    String system_specs_json;
    bool static_specs_known = false;
};

MqttCache mqtt_cache;

SemaphoreHandle_t spec_cache_mutex = nullptr;
SpecCache spec_cache;

void ensure_spec_mutex() {
    if (spec_cache_mutex == nullptr) {
        spec_cache_mutex = xSemaphoreCreateMutex();
    }
}

void serialize_json_to_string(const JsonVariantConst& json, String& out) {
    DynamicJsonDocument doc(2048);
    doc.set(json);
    out = "";
    serializeJson(doc, out);
}

void persist_if_requested(bool persist) {
    if (persist) {
        TransmitterNvsPersistence::persist();
    }
}
} // namespace

namespace TransmitterMqttSpecs {

void load_from_prefs(void* prefs_ptr) {
    if (prefs_ptr == nullptr) {
        return;
    }

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
    if (prefs_ptr == nullptr) {
        return;
    }

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

void store_mqtt_config(bool enabled,
                       const uint8_t* server,
                       uint16_t port,
                       const char* username,
                       const char* password,
                       const char* client_id,
                       bool connected,
                       uint32_t version,
                       bool persist) {
    (void)connected;  // runtime MQTT connection is managed by version beacons

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

    const uint8_t* server_ip = get_server();
    const uint8_t fallback[4] = {0, 0, 0, 0};
    if (server_ip == nullptr) {
        server_ip = fallback;
    }

    LOG_INFO("[TX_MGR] MQTT config stored: %s, %d.%d.%d.%d:%d, v%u",
             enabled ? "ENABLED" : "DISABLED",
             server_ip[0], server_ip[1], server_ip[2], server_ip[3], port,
             version);

    persist_if_requested(persist);
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
    if (!mqtt_cache.mqtt_config_known) {
        return "0.0.0.0";
    }

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

void store_static_specs(const JsonObject& specs) {
    ensure_spec_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        LOG_WARN("[SPEC_CACHE] Failed to lock spec cache mutex");
        return;
    }

    serialize_json_to_string(specs, spec_cache.static_specs_json);

    if (specs.containsKey("battery")) {
        serialize_json_to_string(specs["battery"], spec_cache.battery_specs_json);
    }

    if (specs.containsKey("inverter")) {
        serialize_json_to_string(specs["inverter"], spec_cache.inverter_specs_json);
    }

    if (specs.containsKey("charger")) {
        serialize_json_to_string(specs["charger"], spec_cache.charger_specs_json);
    }

    if (specs.containsKey("system")) {
        serialize_json_to_string(specs["system"], spec_cache.system_specs_json);
    }

    spec_cache.static_specs_known = true;
    LOG_INFO("[SPEC_CACHE] Stored static specs from MQTT");
}

void store_battery_specs(const JsonObject& specs) {
    ensure_spec_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        LOG_WARN("[SPEC_CACHE] Failed to lock spec cache mutex");
        return;
    }

    serialize_json_to_string(specs, spec_cache.battery_specs_json);

    if (specs.containsKey("number_of_cells")) {
        uint16_t new_cell_count = specs["number_of_cells"];
        if (new_cell_count > 0) {
            TransmitterSettingsCache::update_battery_cell_count(new_cell_count);
            LOG_INFO("[TX_MGR] Updated battery_settings.cell_count from MQTT: %u", new_cell_count);
        }
    }

    LOG_INFO("[SPEC_CACHE] Stored battery specs from MQTT");
}

void store_inverter_specs(const JsonObject& specs) {
    ensure_spec_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        LOG_WARN("[SPEC_CACHE] Failed to lock spec cache mutex");
        return;
    }

    serialize_json_to_string(specs, spec_cache.inverter_specs_json);
    LOG_INFO("[SPEC_CACHE] Stored inverter specs from MQTT");
}

void store_charger_specs(const JsonObject& specs) {
    ensure_spec_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        LOG_WARN("[SPEC_CACHE] Failed to lock spec cache mutex");
        return;
    }

    serialize_json_to_string(specs, spec_cache.charger_specs_json);
    LOG_INFO("[SPEC_CACHE] Stored charger specs from MQTT");
}

void store_system_specs(const JsonObject& specs) {
    ensure_spec_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        LOG_WARN("[SPEC_CACHE] Failed to lock spec cache mutex");
        return;
    }

    serialize_json_to_string(specs, spec_cache.system_specs_json);
    LOG_INFO("[SPEC_CACHE] Stored system specs from MQTT");
}

bool has_static_specs() {
    ensure_spec_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        return false;
    }

    return spec_cache.static_specs_known;
}

String get_static_specs_json() {
    ensure_spec_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        return String();
    }

    return spec_cache.static_specs_json;
}

String get_battery_specs_json() {
    ensure_spec_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        return String();
    }

    return spec_cache.battery_specs_json;
}

String get_inverter_specs_json() {
    ensure_spec_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        return String();
    }

    return spec_cache.inverter_specs_json;
}

String get_charger_specs_json() {
    ensure_spec_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        return String();
    }

    return spec_cache.charger_specs_json;
}

String get_system_specs_json() {
    ensure_spec_mutex();

    ScopedMutex guard(spec_cache_mutex);
    if (!guard.locked()) {
        return String();
    }

    return spec_cache.system_specs_json;
}

} // namespace TransmitterMqttSpecs
