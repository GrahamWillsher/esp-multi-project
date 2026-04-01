#ifndef TRANSMITTER_STATE_H
#define TRANSMITTER_STATE_H

#include <Arduino.h>
#include <cstdint>
#include <esp32common/espnow/common.h>

namespace TransmitterState {

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

} // namespace TransmitterState

#endif // TRANSMITTER_STATE_H
