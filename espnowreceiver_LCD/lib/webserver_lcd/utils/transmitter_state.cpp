#include "transmitter_state.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
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

struct StateStore {
    TransmitterState::RuntimeSnapshot runtime;
    TransmitterState::MetadataSnapshot metadata;
    TransmitterState::EventLogSummarySnapshot event_log_summary;
    TransmitterState::EventLogClearAckSnapshot event_log_clear_ack;
    TransmitterState::TemperatureReportSnapshot temperature_report;
};

StateStore g_state_store;
portMUX_TYPE g_state_lock = portMUX_INITIALIZER_UNLOCKED;

template <typename Func>
void with_state_write_lock(Func&& func) {
    portENTER_CRITICAL(&g_state_lock);
    func();
    portEXIT_CRITICAL(&g_state_lock);
}

template <typename Func>
void with_state_read_lock(Func&& func) {
    portENTER_CRITICAL(&g_state_lock);
    func();
    portEXIT_CRITICAL(&g_state_lock);
}

void reduce_runtime_status(bool eth_conn) {
    g_state_store.runtime.ethernet_connected = eth_conn;
    g_state_store.runtime.last_beacon_time_ms = millis();
}

void reduce_send_status(bool success) {
    g_state_store.runtime.last_espnow_send_success = success;
}

void reduce_time_data(uint64_t uptime_ms, uint64_t unix_time, int16_t utc_offset_min, uint8_t time_source) {
    g_state_store.runtime.uptime_ms = uptime_ms;
    g_state_store.runtime.unix_time = unix_time;
    g_state_store.runtime.utc_offset_min = utc_offset_min;
    g_state_store.runtime.time_source = time_source;
}

void reduce_heartbeat_flags(uint8_t flags) {
    g_state_store.runtime.heartbeat_flags = flags;
}

void reduce_metadata(bool valid,
                     const char* env,
                     const char* device,
                     uint8_t major,
                     uint8_t minor,
                     uint8_t patch,
                     const char* build_date) {
    g_state_store.metadata.received = true;
    g_state_store.metadata.valid = valid;

    if (env != nullptr) {
        strncpy(g_state_store.metadata.env, env, sizeof(g_state_store.metadata.env) - 1);
        g_state_store.metadata.env[sizeof(g_state_store.metadata.env) - 1] = '\0';
    }

    if (device != nullptr) {
        strncpy(g_state_store.metadata.device, device, sizeof(g_state_store.metadata.device) - 1);
        g_state_store.metadata.device[sizeof(g_state_store.metadata.device) - 1] = '\0';
    }

    g_state_store.metadata.major = major;
    g_state_store.metadata.minor = minor;
    g_state_store.metadata.patch = patch;
    g_state_store.metadata.version = (static_cast<uint32_t>(major) * 10000) +
                                     (static_cast<uint32_t>(minor) * 100) +
                                     static_cast<uint32_t>(patch);

    if (build_date != nullptr) {
        strncpy(g_state_store.metadata.build_date, build_date, sizeof(g_state_store.metadata.build_date) - 1);
        g_state_store.metadata.build_date[sizeof(g_state_store.metadata.build_date) - 1] = '\0';
    }
}

void reduce_event_log_summary(uint32_t seq,
                              uint32_t total_historical,
                              uint32_t error_historical,
                              uint32_t new_since_last_report_total,
                              uint32_t new_since_last_report_error,
                              uint32_t uptime_ms) {
    g_state_store.event_log_summary.known = true;
    g_state_store.event_log_summary.seq = seq;
    g_state_store.event_log_summary.total_historical = total_historical;
    g_state_store.event_log_summary.error_historical = error_historical;
    g_state_store.event_log_summary.new_since_last_report_total = new_since_last_report_total;
    g_state_store.event_log_summary.new_since_last_report_error = new_since_last_report_error;
    g_state_store.event_log_summary.uptime_ms = uptime_ms;
    g_state_store.event_log_summary.last_update_ms = millis();
}

void reduce_event_log_clear_ack(uint8_t status,
                                uint32_t summary_seq,
                                uint32_t uptime_ms) {
    g_state_store.event_log_clear_ack.known = true;
    g_state_store.event_log_clear_ack.status = status;
    g_state_store.event_log_clear_ack.summary_seq = summary_seq;
    g_state_store.event_log_clear_ack.uptime_ms = uptime_ms;
    g_state_store.event_log_clear_ack.last_update_ms = millis();
}

void reduce_temperature_report(bool valid,
                               uint32_t seq,
                               int16_t temperature_centi_c,
                               uint32_t uptime_ms) {
    g_state_store.temperature_report.known = true;
    g_state_store.temperature_report.valid = valid;
    g_state_store.temperature_report.seq = seq;
    g_state_store.temperature_report.temperature_centi_c = temperature_centi_c;
    g_state_store.temperature_report.uptime_ms = uptime_ms;
    g_state_store.temperature_report.last_update_ms = millis();
}
} // namespace

namespace TransmitterState {

void update_runtime_status(bool mqtt_conn, bool eth_conn) {
    bool mqtt_changed = TransmitterMqttSpecs::update_runtime_connection(mqtt_conn);
    bool eth_changed = false;
    with_state_write_lock([&]() {
        eth_changed = (g_state_store.runtime.ethernet_connected != eth_conn);
        reduce_runtime_status(eth_conn);
    });

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
    bool ethernet_connected = false;
    with_state_read_lock([&]() {
        ethernet_connected = g_state_store.runtime.ethernet_connected;
    });
    return ethernet_connected;
}

unsigned long get_last_beacon_time() {
    unsigned long last_beacon_time_ms = 0;
    with_state_read_lock([&]() {
        last_beacon_time_ms = g_state_store.runtime.last_beacon_time_ms;
    });
    return last_beacon_time_ms;
}

void update_send_status(bool success) {
    with_state_write_lock([&]() {
        reduce_send_status(success);
    });
}

bool was_last_send_successful() {
    bool send_success = false;
    with_state_read_lock([&]() {
        send_success = g_state_store.runtime.last_espnow_send_success;
    });
    return send_success;
}

bool is_transmitter_connected() {
    return TransmitterMqttSpecs::is_connected() &&
           TransmitterIdentity::has_registered_mac();
}

void update_time_data(uint64_t uptime_ms, uint64_t unix_time, int16_t utc_offset_min, uint8_t time_source) {
    with_state_write_lock([&]() {
        reduce_time_data(uptime_ms, unix_time, utc_offset_min, time_source);
    });
}

uint64_t get_uptime_ms() {
    uint64_t uptime_ms = 0;
    with_state_read_lock([&]() {
        uptime_ms = g_state_store.runtime.uptime_ms;
    });
    return uptime_ms;
}

uint64_t get_unix_time() {
    uint64_t unix_time = 0;
    with_state_read_lock([&]() {
        unix_time = g_state_store.runtime.unix_time;
    });
    return unix_time;
}

int16_t get_utc_offset_min() {
    int16_t utc_offset_min = 0;
    with_state_read_lock([&]() {
        utc_offset_min = g_state_store.runtime.utc_offset_min;
    });
    return utc_offset_min;
}

uint8_t get_time_source() {
    uint8_t time_source = 0;
    with_state_read_lock([&]() {
        time_source = g_state_store.runtime.time_source;
    });
    return time_source;
}

void update_heartbeat_flags(uint8_t flags) {
    with_state_write_lock([&]() {
        reduce_heartbeat_flags(flags);
    });
}

uint8_t get_heartbeat_flags() {
    uint8_t heartbeat_flags = 0;
    with_state_read_lock([&]() {
        heartbeat_flags = g_state_store.runtime.heartbeat_flags;
    });
    return heartbeat_flags;
}

bool is_geolocation_valid() {
    uint8_t heartbeat_flags = 0;
    with_state_read_lock([&]() {
        heartbeat_flags = g_state_store.runtime.heartbeat_flags;
    });
    return (heartbeat_flags & kHeartbeatFlagGeolocationValid) != 0;
}

void store_metadata(bool valid,
                    const char* env,
                    const char* device,
                    uint8_t major,
                    uint8_t minor,
                    uint8_t patch,
                    const char* build_date) {
    MetadataSnapshot metadata_snapshot;
    with_state_write_lock([&]() {
        reduce_metadata(valid, env, device, major, minor, patch, build_date);
        metadata_snapshot = g_state_store.metadata;
    });

    char indicator = valid ? '@' : '*';
    LOG_INFO("STATUS_CACHE", "Metadata: %s %s v%d.%d.%d %c",
             metadata_snapshot.device, metadata_snapshot.env, major, minor, patch, indicator);
    if (build_date != nullptr && strlen(build_date) > 0) {
        LOG_INFO("STATUS_CACHE", "  Built: %s", metadata_snapshot.build_date);
    }

    SSENotifier::notifyDataUpdated();
}

bool has_metadata() {
    bool received = false;
    with_state_read_lock([&]() {
        received = g_state_store.metadata.received;
    });
    return received;
}

bool is_metadata_valid() {
    bool valid = false;
    with_state_read_lock([&]() {
        valid = g_state_store.metadata.valid;
    });
    return valid;
}

const char* get_metadata_env() {
    return g_state_store.metadata.env;
}

const char* get_metadata_device() {
    return g_state_store.metadata.device;
}

void get_metadata_version(uint8_t& major, uint8_t& minor, uint8_t& patch) {
    with_state_read_lock([&]() {
        major = g_state_store.metadata.major;
        minor = g_state_store.metadata.minor;
        patch = g_state_store.metadata.patch;
    });
}

const char* get_metadata_build_date() {
    return g_state_store.metadata.build_date;
}

uint32_t get_metadata_version_number() {
    uint32_t version = 0;
    with_state_read_lock([&]() {
        version = g_state_store.metadata.version;
    });
    return version;
}

void load_metadata_from_prefs(void* prefs_ptr) {
    if (prefs_ptr == nullptr) {
        return;
    }

    Preferences& prefs = *static_cast<Preferences*>(prefs_ptr);
    // IMPORTANT: Never perform NVS/flash operations while holding a critical
    // section lock. NVS reads can invoke flash IPC/cache operations that may
    // block, and doing so under portENTER_CRITICAL can trigger interrupt WDT.
    MetadataSnapshot loaded{};
    loaded.received = prefs.getBool(kKeyMetaKnown, false);
    loaded.valid = prefs.getBool(kKeyMetaValid, false);

    loaded.env[0] = '\0';
    loaded.device[0] = '\0';
    loaded.build_date[0] = '\0';
    prefs.getString(kKeyMetaEnv, loaded.env, sizeof(loaded.env));
    prefs.getString(kKeyMetaDevice, loaded.device, sizeof(loaded.device));
    prefs.getString(kKeyMetaBuild, loaded.build_date, sizeof(loaded.build_date));

    loaded.env[sizeof(loaded.env) - 1] = '\0';
    loaded.device[sizeof(loaded.device) - 1] = '\0';
    loaded.build_date[sizeof(loaded.build_date) - 1] = '\0';

    loaded.major = prefs.getUChar(kKeyMetaMajor, 0);
    loaded.minor = prefs.getUChar(kKeyMetaMinor, 0);
    loaded.patch = prefs.getUChar(kKeyMetaPatch, 0);
    loaded.version = prefs.getUInt(kKeyMetaVersion, 0);

    with_state_write_lock([&]() {
        g_state_store.metadata = loaded;
    });
}

void save_metadata_to_prefs(void* prefs_ptr) {
    if (prefs_ptr == nullptr) {
        return;
    }

    Preferences& prefs = *static_cast<Preferences*>(prefs_ptr);
    MetadataSnapshot metadata_snapshot;
    with_state_read_lock([&]() {
        metadata_snapshot = g_state_store.metadata;
    });

    prefs.putBool(kKeyMetaKnown, metadata_snapshot.received);
    prefs.putBool(kKeyMetaValid, metadata_snapshot.valid);
    prefs.putString(kKeyMetaEnv, metadata_snapshot.env);
    prefs.putString(kKeyMetaDevice, metadata_snapshot.device);
    prefs.putUChar(kKeyMetaMajor, metadata_snapshot.major);
    prefs.putUChar(kKeyMetaMinor, metadata_snapshot.minor);
    prefs.putUChar(kKeyMetaPatch, metadata_snapshot.patch);
    prefs.putString(kKeyMetaBuild, metadata_snapshot.build_date);
    prefs.putUInt(kKeyMetaVersion, metadata_snapshot.version);
}

void store_event_log_summary(uint32_t seq,
                             uint32_t total_historical,
                             uint32_t error_historical,
                             uint32_t new_since_last_report_total,
                             uint32_t new_since_last_report_error,
                             uint32_t uptime_ms) {
    EventLogSummarySnapshot snapshot;
    with_state_write_lock([&]() {
        reduce_event_log_summary(seq,
                                 total_historical,
                                 error_historical,
                                 new_since_last_report_total,
                                 new_since_last_report_error,
                                 uptime_ms);
        snapshot = g_state_store.event_log_summary;
    });

    LOG_DEBUG("TX_MGR", "Stored event summary seq=%lu total=%lu error=%lu new=%lu new_error=%lu",
              static_cast<unsigned long>(snapshot.seq),
              static_cast<unsigned long>(snapshot.total_historical),
              static_cast<unsigned long>(snapshot.error_historical),
              static_cast<unsigned long>(snapshot.new_since_last_report_total),
              static_cast<unsigned long>(snapshot.new_since_last_report_error));
}

EventLogSummarySnapshot get_event_log_summary() {
    EventLogSummarySnapshot snapshot;
    with_state_read_lock([&]() {
        snapshot = g_state_store.event_log_summary;
    });
    return snapshot;
}

void store_event_log_clear_ack(uint8_t status,
                               uint32_t summary_seq,
                               uint32_t uptime_ms) {
    EventLogClearAckSnapshot snapshot;
    with_state_write_lock([&]() {
        reduce_event_log_clear_ack(status, summary_seq, uptime_ms);
        snapshot = g_state_store.event_log_clear_ack;
    });

    LOG_INFO("TX_MGR", "Stored event clear ack status=%u summary_seq=%lu",
             static_cast<unsigned>(snapshot.status),
             static_cast<unsigned long>(snapshot.summary_seq));
}

EventLogClearAckSnapshot get_event_log_clear_ack() {
    EventLogClearAckSnapshot snapshot;
    with_state_read_lock([&]() {
        snapshot = g_state_store.event_log_clear_ack;
    });
    return snapshot;
}

void store_temperature_report(bool valid,
                              uint32_t seq,
                              int16_t temperature_centi_c,
                              uint32_t uptime_ms) {
    TemperatureReportSnapshot snapshot;
    with_state_write_lock([&]() {
        reduce_temperature_report(valid, seq, temperature_centi_c, uptime_ms);
        snapshot = g_state_store.temperature_report;
    });

    if (snapshot.valid) {
        LOG_INFO("TX_MGR", "Stored TX temperature seq=%lu value=%.2fC",
                 static_cast<unsigned long>(snapshot.seq),
                 static_cast<double>(snapshot.temperature_centi_c) / 100.0);
    } else {
        LOG_WARN("TX_MGR", "Stored TX temperature seq=%lu as invalid",
                 static_cast<unsigned long>(snapshot.seq));
    }
}

TemperatureReportSnapshot get_temperature_report() {
    TemperatureReportSnapshot snapshot;
    with_state_read_lock([&]() {
        snapshot = g_state_store.temperature_report;
    });
    return snapshot;
}

StateSnapshot get_state_snapshot() {
    StateSnapshot snapshot;
    with_state_read_lock([&]() {
        snapshot.runtime = g_state_store.runtime;
        snapshot.metadata = g_state_store.metadata;
        snapshot.event_log_summary = g_state_store.event_log_summary;
        snapshot.event_log_clear_ack = g_state_store.event_log_clear_ack;
        snapshot.temperature_report = g_state_store.temperature_report;
    });
    return snapshot;
}

} // namespace TransmitterState
