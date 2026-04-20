#ifndef TRANSMITTER_STATE_H
#define TRANSMITTER_STATE_H

#include <Arduino.h>
#include <cstdint>
#include <esp32common/espnow/common.h>

namespace TransmitterState {

struct RuntimeSnapshot {
    bool ethernet_connected = false;
    unsigned long last_beacon_time_ms = 0;
    bool last_espnow_send_success = true;
    uint64_t uptime_ms = 0;
    uint64_t unix_time = 0;
    int16_t utc_offset_min = 0;
    uint8_t time_source = 0;
    uint8_t heartbeat_flags = 0;
};

struct MetadataSnapshot {
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

struct EventLogSummarySnapshot {
    bool known = false;
    uint32_t seq = 0;
    uint32_t total_historical = 0;
    uint32_t error_historical = 0;
    uint32_t new_since_last_report_total = 0;
    uint32_t new_since_last_report_error = 0;
    uint32_t uptime_ms = 0;
    uint32_t last_update_ms = 0;
};

struct EventLogClearAckSnapshot {
    bool known = false;
    uint8_t status = EVENT_LOGS_CLEAR_ACK_FAILED;
    uint32_t summary_seq = 0;
    uint32_t uptime_ms = 0;
    uint32_t last_update_ms = 0;
};

struct TemperatureReportSnapshot {
    bool known = false;
    bool valid = false;
    uint32_t seq = 0;
    int16_t temperature_centi_c = 0;
    uint32_t uptime_ms = 0;
    uint32_t last_update_ms = 0;
};

struct StateSnapshot {
    RuntimeSnapshot runtime;
    MetadataSnapshot metadata;
    EventLogSummarySnapshot event_log_summary;
    EventLogClearAckSnapshot event_log_clear_ack;
    TemperatureReportSnapshot temperature_report;
};

void update_runtime_status(bool mqtt_conn, bool eth_conn);

bool is_ethernet_connected();
unsigned long get_last_beacon_time();
void update_send_status(bool success);
bool was_last_send_successful();
bool is_transmitter_connected();

void update_time_data(uint64_t uptime_ms, uint64_t unix_time, int16_t utc_offset_min, uint8_t time_source);
uint64_t get_uptime_ms();
uint64_t get_unix_time();
int16_t get_utc_offset_min();
uint8_t get_time_source();
void update_heartbeat_flags(uint8_t flags);
uint8_t get_heartbeat_flags();
bool is_geolocation_valid();

void store_metadata(bool valid,
                    const char* env,
                    const char* device,
                    uint8_t major,
                    uint8_t minor,
                    uint8_t patch,
                    const char* build_date);

bool has_metadata();
bool is_metadata_valid();
const char* get_metadata_env();
const char* get_metadata_device();
void get_metadata_version(uint8_t& major, uint8_t& minor, uint8_t& patch);
const char* get_metadata_build_date();
uint32_t get_metadata_version_number();

void load_metadata_from_prefs(void* prefs_ptr);
void save_metadata_to_prefs(void* prefs_ptr);

void store_event_log_summary(const event_log_summary_t& summary);
EventLogSummarySnapshot get_event_log_summary();

void store_event_log_clear_ack(const event_logs_clear_ack_t& ack);
EventLogClearAckSnapshot get_event_log_clear_ack();

void store_temperature_report(const temperature_report_t& report);
TemperatureReportSnapshot get_temperature_report();

StateSnapshot get_state_snapshot();

} // namespace TransmitterState

#endif // TRANSMITTER_STATE_H
