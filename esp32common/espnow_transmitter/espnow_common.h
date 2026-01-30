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
    msg_flash_led       // Flash LED indicator on receiver
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

// Structure for queued ESP-NOW messages (holds raw data for processing in worker task)
typedef struct {
    uint8_t data[250];     // Raw message data (ESP-NOW max payload)
    uint8_t mac[6];        // Sender MAC address
    int len;               // Message length
    uint32_t timestamp;    // Reception timestamp
} espnow_queue_msg_t;

#endif // ESPNOW_COMMON_H
