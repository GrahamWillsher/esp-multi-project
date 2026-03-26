#include "transmitter_state.h"

#include <Preferences.h>
#include <esp32common/espnow/connection_manager.h>
#include <string.h>

#include "../logging.h"
#include "sse_notifier.h"
#include "transmitter_identity.h"
#include "transmitter_mqtt_specs.h"

namespace {
constexpr const char* kKeyMetaKnown = "meta_known";
constexpr const char* kKeyMetaValid = "meta_valid";
constexpr const char* kKeyMetaEnv = "meta_env";
constexpr const char* kKeyMetaDevice = "meta_device";
constexpr const char* kKeyMetaMajor = "meta_major";
constexpr const char* kKeyMetaMinor = "meta_minor";
constexpr const char* kKeyMetaPatch = "meta_patch";
constexpr const char* kKeyMetaBuild = "meta_build";
constexpr const char* kKeyMetaVersion = "meta_ver";

struct RuntimeStatus {
    bool ethernet_connected = false;
    unsigned long last_beacon_time_ms = 0;
    bool last_espnow_send_success = true;
    uint64_t uptime_ms = 0;
    uint64_t unix_time = 0;
    uint8_t time_source = 0;
};

struct Metadata {
    bool received = false;
    bool valid = false;
    char env[32] = {0};
    char device[16] = {0};
    uint8_t major = 0;
    uint8_t minor = 0;
    uint8_t patch = 0;
    char build_date[48] = {0};
    uint32_t version = 0;
};

RuntimeStatus runtime_status;
Metadata metadata;
} // namespace

namespace TransmitterState {

void update_runtime_status(bool mqtt_conn, bool eth_conn) {
    bool mqtt_changed = TransmitterMqttSpecs::update_runtime_connection(mqtt_conn);
    bool eth_changed = (runtime_status.ethernet_connected != eth_conn);

    runtime_status.ethernet_connected = eth_conn;
    runtime_status.last_beacon_time_ms = millis();

    if (eth_changed) {
        LOG_INFO("STATUS_CACHE", "Runtime: ETH=%s", eth_conn ? "CONNECTED" : "DISCONNECTED");
    }

    if (mqtt_changed || eth_changed) {
        LOG_INFO("TX_MGR", "Runtime status updated: MQTT=%s, ETH=%s",
                 mqtt_conn ? "CONNECTED" : "DISCONNECTED",
                 eth_conn ? "CONNECTED" : "DISCONNECTED");
    }
}

bool is_ethernet_connected() {
    return runtime_status.ethernet_connected;
}

unsigned long get_last_beacon_time() {
    return runtime_status.last_beacon_time_ms;
}

void update_send_status(bool success) {
    runtime_status.last_espnow_send_success = success;
}

bool was_last_send_successful() {
    return runtime_status.last_espnow_send_success;
}

bool is_transmitter_connected() {
    return EspNowConnectionManager::instance().is_connected() &&
           TransmitterIdentity::get_active_mac() != nullptr;
}

void update_time_data(uint64_t uptime_ms, uint64_t unix_time, uint8_t time_source) {
    runtime_status.uptime_ms = uptime_ms;
    runtime_status.unix_time = unix_time;
    runtime_status.time_source = time_source;
}

uint64_t get_uptime_ms() {
    return runtime_status.uptime_ms;
}

uint64_t get_unix_time() {
    return runtime_status.unix_time;
}

uint8_t get_time_source() {
    return runtime_status.time_source;
}

void store_metadata(bool valid,
                    const char* env,
                    const char* device,
                    uint8_t major,
                    uint8_t minor,
                    uint8_t patch,
                    const char* build_date) {
    metadata.received = true;
    metadata.valid = valid;

    if (env != nullptr) {
        strncpy(metadata.env, env, sizeof(metadata.env) - 1);
        metadata.env[sizeof(metadata.env) - 1] = '\0';
    }

    if (device != nullptr) {
        strncpy(metadata.device, device, sizeof(metadata.device) - 1);
        metadata.device[sizeof(metadata.device) - 1] = '\0';
    }

    metadata.major = major;
    metadata.minor = minor;
    metadata.patch = patch;
    metadata.version = (static_cast<uint32_t>(major) * 10000) +
                       (static_cast<uint32_t>(minor) * 100) +
                       static_cast<uint32_t>(patch);

    if (build_date != nullptr) {
        strncpy(metadata.build_date, build_date, sizeof(metadata.build_date) - 1);
        metadata.build_date[sizeof(metadata.build_date) - 1] = '\0';
    }

    char indicator = valid ? '@' : '*';
    LOG_INFO("STATUS_CACHE", "Metadata: %s %s v%d.%d.%d %c",
             metadata.device, metadata.env, major, minor, patch, indicator);
    if (build_date != nullptr && strlen(build_date) > 0) {
        LOG_INFO("STATUS_CACHE", "  Built: %s", metadata.build_date);
    }

    SSENotifier::notifyDataUpdated();
}

bool has_metadata() {
    return metadata.received;
}

bool is_metadata_valid() {
    return metadata.valid;
}

const char* get_metadata_env() {
    return metadata.env;
}

const char* get_metadata_device() {
    return metadata.device;
}

void get_metadata_version(uint8_t& major, uint8_t& minor, uint8_t& patch) {
    major = metadata.major;
    minor = metadata.minor;
    patch = metadata.patch;
}

const char* get_metadata_build_date() {
    return metadata.build_date;
}

uint32_t get_metadata_version_number() {
    return metadata.version;
}

void load_metadata_from_prefs(void* prefs_ptr) {
    if (prefs_ptr == nullptr) {
        return;
    }

    Preferences& prefs = *static_cast<Preferences*>(prefs_ptr);
    metadata.received = prefs.getBool(kKeyMetaKnown, false);
    metadata.valid = prefs.getBool(kKeyMetaValid, false);

    metadata.env[0] = '\0';
    metadata.device[0] = '\0';
    metadata.build_date[0] = '\0';
    prefs.getString(kKeyMetaEnv, metadata.env, sizeof(metadata.env));
    prefs.getString(kKeyMetaDevice, metadata.device, sizeof(metadata.device));
    prefs.getString(kKeyMetaBuild, metadata.build_date, sizeof(metadata.build_date));

    metadata.env[sizeof(metadata.env) - 1] = '\0';
    metadata.device[sizeof(metadata.device) - 1] = '\0';
    metadata.build_date[sizeof(metadata.build_date) - 1] = '\0';

    metadata.major = prefs.getUChar(kKeyMetaMajor, 0);
    metadata.minor = prefs.getUChar(kKeyMetaMinor, 0);
    metadata.patch = prefs.getUChar(kKeyMetaPatch, 0);
    metadata.version = prefs.getUInt(kKeyMetaVersion, 0);
}

void save_metadata_to_prefs(void* prefs_ptr) {
    if (prefs_ptr == nullptr) {
        return;
    }

    Preferences& prefs = *static_cast<Preferences*>(prefs_ptr);
    prefs.putBool(kKeyMetaKnown, metadata.received);
    prefs.putBool(kKeyMetaValid, metadata.valid);
    prefs.putString(kKeyMetaEnv, metadata.env);
    prefs.putString(kKeyMetaDevice, metadata.device);
    prefs.putUChar(kKeyMetaMajor, metadata.major);
    prefs.putUChar(kKeyMetaMinor, metadata.minor);
    prefs.putUChar(kKeyMetaPatch, metadata.patch);
    prefs.putString(kKeyMetaBuild, metadata.build_date);
    prefs.putUInt(kKeyMetaVersion, metadata.version);
}

} // namespace TransmitterState
