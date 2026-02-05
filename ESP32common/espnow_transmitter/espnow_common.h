#ifndef ESPNOW_COMMON_H
#define ESPNOW_COMMON_H

#include <Arduino.h>

// ESP-NOW broadcast MAC address (used for discovery before peer is known)
const uint8_t ESPNOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESP-NOW channel probing and control message types
enum msg_type : uint8_t { 
    msg_probe,          // Channel probe request
    msg_ack,            // Channel probe acknowledgment
    msg_data,           // Battery data message
    msg_request_data,   // Request to start sending data
    msg_abort_data,     // Request to stop sending data
    msg_packet,         // Fragmented packet (uses espnow_packet_t)
    msg_reboot,         // Reboot command
    msg_ota_start,      // OTA update start command
    msg_flash_led,      // Flash LED indicator on receiver
    msg_debug_control,  // Debug level control (receiver → transmitter)
    msg_debug_ack,      // Debug level acknowledgment (transmitter → receiver)
    
    // Configuration synchronization messages
    msg_config_request_full,    // Request full configuration snapshot
    msg_config_snapshot,        // Full configuration snapshot response
    msg_config_update_delta,    // Delta update (single field change)
    msg_config_ack,             // Configuration ACK with version
    msg_config_request_resync,  // Request resync (delta failed)
    
    // Firmware version exchange messages
    msg_version_announce,       // Periodic version announcement
    msg_version_request,        // Request version from peer
    msg_version_response        // Version response to request
};

// ESP-NOW packet subtypes (for fragmented messages)
enum msg_subtype : uint8_t {
    subtype_none,           // No subtype (used for probe/ack)
    subtype_settings,       // Static configuration (IP address data)
    subtype_systeminfo,     // System information
    subtype_events,         // Real-time event stream (small updates)
    subtype_logs,           // Large multi-KB log dump
    subtype_cell_info,      // Large or medium structured data
    subtype_power_profile   // Power profile data
};


// Message payload structure
typedef struct __attribute__((packed)) {
    uint8_t type;          // msg_data
    uint8_t soc;           // SOC: 0-100%
    int16_t power;         // Power in Watts: -4000 to +4000 (2 bytes, signed)
    uint16_t checksum;     // Simple checksum for data integrity
} espnow_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;      // msg_probe
    uint32_t seq;       // random per attempt
} probe_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;      // msg_ack
    uint32_t seq;       // echo of probe seq
    uint8_t  channel;   // our current Wi-Fi channel
} ack_t;

typedef struct __attribute__((packed)) {
    uint8_t type;       // msg_request_data
    uint8_t subtype;    // msg_subtype (which data stream to start)
} request_data_t;

typedef struct __attribute__((packed)) {
    uint8_t type;       // msg_abort_data
    uint8_t subtype;    // msg_subtype (which data stream to stop)
} abort_data_t;

typedef struct __attribute__((packed)) {
    uint8_t type;       // msg_reboot
} reboot_t;

typedef struct __attribute__((packed)) {
    uint8_t type;       // msg_ota_start
    uint32_t size;      // Firmware size in bytes
} ota_start_t;

typedef struct __attribute__((packed)) {
    uint8_t type;       // msg_flash_led
    uint8_t color;      // LED color code: 0=RED, 1=GREEN, 2=ORANGE
                        // (semantic values, receiver maps to display colors)
} flash_led_t;

typedef struct __attribute__((packed)) {
    uint8_t type;       // msg_debug_control
    uint8_t level;      // Debug level: 0-7 (EMERG to DEBUG)
    uint8_t flags;      // Reserved for future use
    uint8_t checksum;   // Simple checksum validation
} debug_control_t;

typedef struct __attribute__((packed)) {
    uint8_t type;       // msg_debug_ack
    uint8_t applied;    // Level that was applied
    uint8_t previous;   // Previous level before change
    uint8_t status;     // 0=success, 1=invalid level, 2=error
} debug_ack_t;

// Fragmented packet structure for large data transfers
typedef struct __attribute__((packed)) {
    uint8_t   type;          // Packet type (msg_packet)
    uint8_t   subtype;       // Data category (subtype_settings, subtype_events, etc.)
    uint32_t  seq;           // Unique sequence ID for a full request/response cycle
    uint16_t  frag_index;    // Index of this fragment (0-based)
    uint16_t  frag_total;    // Total fragment count
    uint16_t  payload_len;   // Length of actual data in payload[]
    uint16_t  checksum;      // Simple checksum for payload integrity
    uint8_t   payload[230];  // Payload data (230 bytes max to keep total <= 250)
} espnow_packet_t;

// Configuration synchronization message structures
#include "../config_sync/config_structures.h"

// Request full configuration snapshot
typedef struct __attribute__((packed)) {
    uint8_t type;             // msg_config_request_full
    uint32_t request_id;      // For tracking responses
} config_request_full_t;

// Delta update message (sent when a config value changes)
typedef struct __attribute__((packed)) {
    uint8_t type;             // msg_config_update_delta
    uint16_t global_version;  // New global version
    uint16_t section_version; // New section version
    uint8_t section;          // ConfigSection
    uint8_t field_id;         // Specific field changed
    uint8_t value_length;     // Length of value data
    uint8_t value_data[64];   // Actual value (variable length)
    uint32_t timestamp;       // When change occurred
} config_delta_update_t;

// Configuration ACK
typedef struct __attribute__((packed)) {
    uint8_t type;             // msg_config_ack
    uint16_t acked_version;   // Version being acknowledged
    uint8_t section;          // ConfigSection acknowledged
    uint8_t success;          // Success/failure flag (0 or 1)
    uint32_t timestamp;       // When ACK sent
} config_ack_t;

// Request resync (fallback when delta updates fail)
typedef struct __attribute__((packed)) {
    uint8_t type;             // msg_config_request_resync
    uint16_t last_known_version; // Last version receiver had
    char reason[32];          // Why resync needed
} config_request_resync_t;

// Firmware version announcement packet
typedef struct __attribute__((packed)) {
    uint8_t type;                   // msg_version_announce
    uint32_t firmware_version;      // Version number (e.g., 10000 for 1.0.0)
    uint8_t protocol_version;       // ESP-NOW protocol version
    uint32_t min_compatible_version; // Minimum compatible firmware version
    char device_type[16];           // "RECEIVER" or "TRANSMITTER"
    char build_date[12];            // Build date string
    char build_time[9];             // Build time string
    uint32_t uptime_seconds;        // Device uptime in seconds
} version_announce_t;

// Version request packet
typedef struct __attribute__((packed)) {
    uint8_t type;             // msg_version_request
    uint32_t request_id;      // For tracking responses
} version_request_t;

// Version response packet (same structure as announce)
typedef struct __attribute__((packed)) {
    uint8_t type;                   // msg_version_response
    uint32_t request_id;            // Echoed from request
    uint32_t firmware_version;      // Version number
    uint8_t protocol_version;       // ESP-NOW protocol version
    uint32_t min_compatible_version; // Minimum compatible version
    char device_type[16];           // Device type string
    char build_date[12];            // Build date
    char build_time[9];             // Build time
    uint32_t uptime_seconds;        // Device uptime
} version_response_t;

// Structure for queued ESP-NOW messages (holds raw data for processing in worker task)
typedef struct {
    uint8_t data[250];     // Raw message data (ESP-NOW max payload)
    uint8_t mac[6];        // Sender MAC address
    int len;               // Message length
    uint32_t timestamp;    // Reception timestamp
} espnow_queue_msg_t;

#endif // ESPNOW_COMMON_H
