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
    msg_version_response,       // Version response to request
    
    // Firmware metadata exchange messages
    msg_metadata_request,       // Request firmware metadata from peer
    msg_metadata_response,      // Firmware metadata response
    
    // =========================================================================
    // PHASE 1: Battery Emulator Data Layer Messages
    // =========================================================================
    
    // Battery data messages
    msg_battery_status,         // Real-time battery status (SOC, V, I, temp, power)
    msg_battery_info,           // Static battery info (capacity, chemistry, cell count)
    
    // Charger data messages  
    msg_charger_status,         // Real-time charger status (HV/LV voltage, current, power)
    
    // Inverter data messages
    msg_inverter_status,        // Real-time inverter status (AC voltage, freq, power)
    
    // System data messages
    msg_system_status,          // System status (contactors, BMS state, errors)
    
    // =========================================================================
    // PHASE 2: Settings Bidirectional Flow
    // =========================================================================
    
    // Settings update messages
    msg_battery_settings_update,    // Update battery settings (receiver → transmitter)
    msg_settings_update_ack         // Settings update acknowledgment (transmitter → receiver)
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

// Firmware metadata request packet
typedef struct __attribute__((packed)) {
    uint8_t type;             // msg_metadata_request
    uint32_t request_id;      // For tracking responses
} metadata_request_t;

// Firmware metadata response packet
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_metadata_response
    uint32_t request_id;         // Echoed from request
    bool valid;                  // Metadata is valid (from .rodata)
    char env_name[32];           // Environment name
    char device_type[16];        // Device type
    uint8_t version_major;       // Major version
    uint8_t version_minor;       // Minor version
    uint8_t version_patch;       // Patch version
    char build_date[48];         // Build timestamp (DD-MM-YYYY HH:MM:SS)
} metadata_response_t;

// =============================================================================
// PHASE 1: Battery Emulator Data Layer Message Structures
// =============================================================================

// BMS status enum
enum bms_status_t : uint8_t {
    BMS_OK = 0,
    BMS_WARNING = 1,
    BMS_FAULT = 2,
    BMS_OFFLINE = 3
};

// Battery status message - Real-time data (200ms updates)
typedef struct __attribute__((packed)) {
    uint8_t type;                    // msg_battery_status
    uint16_t soc_percent_100;        // SOC in 0.01% (e.g., 8050 = 80.50%)
    uint32_t voltage_mV;             // Voltage in mV
    int32_t current_mA;              // Current in mA (signed: + = charging, - = discharging)
    int16_t temperature_dC;          // Temperature in 0.1°C
    int32_t power_W;                 // Power in W (signed)
    uint16_t max_charge_power_W;     // Maximum charge power limit
    uint16_t max_discharge_power_W;  // Maximum discharge power limit
    uint8_t bms_status;              // BMS status (bms_status_t enum)
    uint16_t checksum;               // Message checksum
} battery_status_msg_t;  // Total: 27 bytes

// Battery info message - Static data (sent once on connection)
typedef struct __attribute__((packed)) {
    uint8_t type;                        // msg_battery_info
    uint32_t total_capacity_Wh;          // Total energy capacity in Wh
    uint32_t reported_capacity_Wh;       // Capacity reported to inverter
    uint16_t max_design_voltage_dV;      // Maximum pack voltage in dV (0.1V)
    uint16_t min_design_voltage_dV;      // Minimum pack voltage in dV
    uint16_t max_cell_voltage_mV;        // Max cell voltage limit in mV
    uint16_t min_cell_voltage_mV;        // Min cell voltage limit in mV
    uint16_t max_cell_deviation_mV;      // Max allowed cell deviation
    uint8_t number_of_cells;             // Total cells in pack
    uint8_t chemistry;                   // Battery chemistry (0=NCA, 1=NMC, 2=LFP, 3=LTO)
    uint16_t checksum;                   // Message checksum
} battery_info_msg_t;  // Total: 26 bytes

// Charger status message - Real-time data (200ms updates)
typedef struct __attribute__((packed)) {
    uint8_t type;                    // msg_charger_status
    uint16_t hv_voltage_dV;          // HV voltage in dV (0.1V)
    int16_t hv_current_dA;           // HV current in dA (0.1A, signed)
    uint16_t lv_voltage_dV;          // LV voltage in dV
    int16_t lv_current_dA;           // LV current in dA (signed)
    uint16_t ac_voltage_V;           // AC input voltage in V
    int16_t ac_current_dA;           // AC current in dA (signed)
    uint16_t power_W;                // Charger power in W
    uint8_t charger_status;          // Charger state (0=off, 1=charging, 2=fault)
    uint16_t checksum;               // Message checksum
} charger_status_msg_t;  // Total: 20 bytes

// Inverter status message - Real-time data (200ms updates)  
typedef struct __attribute__((packed)) {
    uint8_t type;                    // msg_inverter_status
    uint16_t ac_voltage_V;           // AC output voltage in V
    uint16_t ac_frequency_dHz;       // AC frequency in dHz (0.1Hz)
    int16_t ac_current_dA;           // AC current in dA (signed)
    int32_t power_W;                 // Inverter power in W (signed)
    uint8_t inverter_status;         // Inverter state (0=off, 1=on, 2=fault)
    uint16_t checksum;               // Message checksum
} inverter_status_msg_t;  // Total: 14 bytes

// System status message - Real-time data (200ms updates)
typedef struct __attribute__((packed)) {
    uint8_t type;                    // msg_system_status
    uint8_t contactor_state;         // Bit flags: 0=positive, 1=negative, 2=precharge
    uint8_t error_flags;             // Error flags (bit mask)
    uint8_t warning_flags;           // Warning flags (bit mask)
    uint32_t uptime_seconds;         // System uptime in seconds
    uint16_t checksum;               // Message checksum
} system_status_msg_t;  // Total: 10 bytes

// =============================================================================
// PHASE 2: Settings Bidirectional Flow Message Structures
// =============================================================================

// Settings category and field identifiers
enum SettingsCategory : uint8_t {
    SETTINGS_BATTERY = 0,
    SETTINGS_CHARGER = 1,
    SETTINGS_INVERTER = 2,
    SETTINGS_SYSTEM = 3,
    SETTINGS_MQTT = 4,
    SETTINGS_NETWORK = 5
};

// Battery settings field IDs
enum BatterySettingsField : uint8_t {
    BATTERY_CAPACITY_WH = 0,
    BATTERY_MAX_VOLTAGE_MV = 1,
    BATTERY_MIN_VOLTAGE_MV = 2,
    BATTERY_MAX_CHARGE_CURRENT_A = 3,
    BATTERY_MAX_DISCHARGE_CURRENT_A = 4,
    BATTERY_SOC_HIGH_LIMIT = 5,
    BATTERY_SOC_LOW_LIMIT = 6,
    BATTERY_CELL_COUNT = 7,
    BATTERY_CHEMISTRY = 8
};

// Settings update message - Receiver → Transmitter
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_battery_settings_update
    uint8_t category;            // Settings category (SettingsCategory)
    uint8_t field_id;            // Field within category
    uint32_t value_uint32;       // Integer value
    float value_float;           // Float value (use appropriate field based on setting type)
    char value_string[32];       // String value (for text settings like MQTT server)
    uint16_t checksum;           // Message checksum
} settings_update_msg_t;  // Total: 44 bytes

// Settings update acknowledgment - Transmitter → Receiver
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_settings_update_ack
    uint8_t category;            // Echo: category that was updated
    uint8_t field_id;            // Echo: field that was updated
    bool success;                // True if setting was saved successfully
    char error_msg[48];          // Error description if failed
    uint16_t checksum;           // Message checksum
} settings_update_ack_msg_t;  // Total: 54 bytes

// Structure for queued ESP-NOW messages (holds raw data for processing in worker task)
typedef struct {
    uint8_t data[250];     // Raw message data (ESP-NOW max payload)
    uint8_t mac[6];        // Sender MAC address
    int len;               // Message length
    uint32_t timestamp;    // Reception timestamp
} espnow_queue_msg_t;

#endif // ESPNOW_COMMON_H
