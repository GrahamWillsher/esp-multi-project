#pragma once

#include <Arduino.h>

// Minimal ESP-NOW protocol subset for Phase E display data path.
// Keep aligned with esp32common/espnow_transmitter/espnow_common.h.

enum msg_type_min : uint8_t {
    msg_probe_min = 0,
    msg_ack_min,
    msg_data_min,
    msg_request_data_min,
    msg_abort_data_min,
    msg_packet_min,
    msg_reboot_min,
    msg_ota_start_min,
    msg_flash_led_min,
    msg_debug_control_min,
    msg_debug_ack_min,
    msg_version_announce_min,
    msg_version_request_min,
    msg_version_response_min,
    msg_metadata_response_min,
    msg_battery_status_min,
    msg_battery_info_min,
    msg_charger_status_min,
    msg_inverter_status_min,
    msg_system_status_min,
    msg_component_config_min,
    msg_battery_settings_update_min,
    msg_settings_update_ack_min,
    msg_settings_changed_min,
    msg_network_config_request_min,
    msg_network_config_update_min,
    msg_network_config_ack_min,
    msg_mqtt_config_request_min,
    msg_mqtt_config_update_min,
    msg_mqtt_config_ack_min,
    msg_version_beacon_min,
    msg_config_section_request_min,
    msg_time_transitions_snapshot_min,
    msg_heartbeat_min,
    msg_heartbeat_ack_min,
};

enum msg_subtype_min : uint8_t {
    subtype_power_profile_min = 0,
};

typedef struct __attribute__((packed)) {
    uint8_t type;          // msg_data
    uint8_t soc;           // 0-100
    int16_t power;         // W
    uint32_t checksum;
} espnow_payload_min_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;      // msg_probe
    uint32_t seq;
} probe_min_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;      // msg_ack
    uint32_t seq;       // echo of probe seq
    uint8_t  channel;   // receiver Wi-Fi channel
} ack_min_t;

typedef struct __attribute__((packed)) {
    uint8_t type;       // msg_request_data
    uint8_t subtype;
} request_data_min_t;

typedef struct __attribute__((packed)) {
    uint8_t type;                    // msg_battery_status
    uint16_t soc_percent_100;        // 0.01%
    uint32_t voltage_mV;
    int32_t current_mA;
    int16_t temperature_dC;
    int32_t power_W;
    uint16_t max_charge_power_W;
    uint16_t max_discharge_power_W;
    uint8_t bms_status;
    uint32_t checksum;
} battery_status_min_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;           // msg_heartbeat
    uint32_t seq;            // Monotonic sequence number
    uint64_t uptime_ms;      // Sender uptime
    uint64_t unix_time;      // Sender unix time
    int16_t  utc_offset_min; // UTC offset minutes
    uint8_t  time_source;    // Time source enum
    uint8_t  state;          // Sender state enum
    uint8_t  rssi;           // Last RSSI
    uint8_t  flags;          // Status flags
    uint32_t checksum;       // CRC32 (zeroed-trailing-field convention)
} heartbeat_min_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;          // msg_heartbeat_ack
    uint32_t ack_seq;       // Acknowledged heartbeat sequence
    uint64_t uptime_ms;     // Receiver uptime
    uint8_t  state;         // Receiver state
    uint32_t checksum;      // CRC32 (zeroed-trailing-field convention)
} heartbeat_ack_min_t;

typedef struct {
    uint8_t data[250];
    uint8_t mac[6];
    int len;
    uint32_t timestamp;
} espnow_queue_msg_min_t;
