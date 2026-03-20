#ifndef TRANSMITTER_STATUS_CACHE_H
#define TRANSMITTER_STATUS_CACHE_H

#include <Arduino.h>

// =============================================================================
// TransmitterStatusCache
// =============================================================================
// Owns two previously-separate TransmitterManager domain groups:
//
//   RuntimeStatus — live beacon/heartbeat state (ETH connected, send success,
//                   last beacon time, uptime, unix time, time source).
//                   Not persisted to NVS; always rebuilt from incoming packets.
//
//   Metadata      — firmware identity received from the transmitter's version
//                   beacon (env, device, major/minor/patch, build_date).
//                   Persisted to NVS via the caller (saveToNVS in TM).
//
// Thread-safety: all public methods are safe to call from any task; internal
// state is plain primitive/char[] with no heap allocation.
// =============================================================================

namespace TransmitterStatusCache {

// --- Runtime status ---------------------------------------------------------

/// Update from an incoming version beacon's Ethernet state.
void update_runtime_status(bool eth_conn);

bool is_ethernet_connected();
unsigned long get_last_beacon_time();

void update_send_status(bool success);
bool was_last_send_successful();

/// True when the ESP-NOW connection state machine reports connected AND
/// the transmitter MAC has been registered.
bool is_transmitter_connected();

// Time data updated from heartbeat packets
void update_time_data(uint64_t uptime_ms, uint64_t unix_time, uint8_t time_source);
uint64_t get_uptime_ms();
uint64_t get_unix_time();
uint8_t get_time_source();

// For write-back when TransmitterManager saves to NVS
// (RuntimeStatus is not persisted — no NVS keys needed)

// --- Firmware metadata ------------------------------------------------------

void store_metadata(bool valid,
                    const char* env,
                    const char* device,
                    uint8_t major, uint8_t minor, uint8_t patch,
                    const char* build_date);

bool has_metadata();
bool is_metadata_valid();
const char* get_metadata_env();
const char* get_metadata_device();
void get_metadata_version(uint8_t& major, uint8_t& minor, uint8_t& patch);
const char* get_metadata_build_date();
uint32_t get_metadata_version_number();

// NVS persistence helpers (called from TransmitterManager::loadFromNVS /
// persist_to_nvs_now — metadata IS persisted, runtime status is NOT)
void load_metadata_from_prefs(void* prefs_ptr);   // void* avoids pulling Preferences.h into header
void save_metadata_to_prefs(void* prefs_ptr);

} // namespace TransmitterStatusCache

#endif // TRANSMITTER_STATUS_CACHE_H
