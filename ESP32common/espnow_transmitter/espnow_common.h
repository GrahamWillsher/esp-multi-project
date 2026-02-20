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
    
    // Firmware version exchange messages
    msg_version_announce,       // Periodic version announcement
    msg_version_request,        // Request version from peer
    msg_version_response,       // Version response to request
    
    // Firmware metadata exchange messages
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
    
    // Component configuration messages
    msg_component_config,       // Component type selections (BMS, inverter, charger, shunt)
    
    // =========================================================================
    // PHASE 2: Settings Bidirectional Flow
    // =========================================================================
    
    // Settings update messages
    msg_battery_settings_update,    // Update battery settings (receiver → transmitter)
    msg_settings_update_ack,        // Settings update acknowledgment (transmitter → receiver)
    
    // Settings change notification (transmitter → receiver)
    msg_settings_changed,           // Notify receiver that settings version changed
    
    // =========================================================================
    // PHASE 3: Network Configuration
    // =========================================================================
    
    // Network configuration messages
    msg_network_config_request,     // Request current network configuration (receiver → transmitter)
    msg_network_config_update,      // Update network configuration (receiver → transmitter)
    msg_network_config_ack,         // Network config update ACK (transmitter → receiver)
    
    // MQTT configuration messages
    msg_mqtt_config_request,        // Request current MQTT configuration (receiver → transmitter)
    msg_mqtt_config_update,         // Update MQTT configuration (receiver → transmitter)
    msg_mqtt_config_ack,            // MQTT config update ACK (transmitter → receiver)
    
    // =========================================================================
    // PHASE 4: Version-Based Cache Synchronization
    // =========================================================================
    
    // Version beacon messages
    msg_version_beacon,             // Periodic version sync beacon (transmitter → receiver)
    msg_config_section_request,     // Request specific config section (receiver → transmitter)
    
    // =========================================================================
    // SECTION 11: Transmitter-Active Architecture
    // =========================================================================
    
    // Keep-alive messages
    msg_heartbeat,                  // Heartbeat message (10s interval, both directions)
    msg_heartbeat_ack,              // Heartbeat acknowledgment (confirms receipt)
    
    // Bidirectional config sync
    msg_config_changed              // Configuration changed notification (versioned, timestamped)
};

// ESP-NOW packet subtypes (for fragmented messages)
enum msg_subtype : uint8_t {
    subtype_none = 0xFF,            // No subtype (used for pages without ESP-NOW data)
    subtype_power_profile = 0,      // Power profile data stream
    subtype_settings = 1,           // DEPRECATED: Returns IP + battery (use granular subtypes instead)
    subtype_events = 2,             // Real-time event stream
    subtype_logs = 3,               // Large multi-KB log dump
    subtype_cell_info = 4,          // Cell-level structured data
    subtype_systeminfo = 10,        // System information (firmware, uptime, etc.)
    
    // Phase 3: Granular subtypes for separate data requests
    subtype_network_config = 5,     // IP, gateway, subnet only
    subtype_battery_config = 6,     // Battery settings only
    subtype_charger_config = 7,     // Charger settings only
    subtype_inverter_config = 8,    // Inverter settings only
    subtype_system_config = 9       // System settings only
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

// ============================================================================
// SECTION 11: Keep-Alive and Config Sync Messages
// ============================================================================

// Heartbeat message (bidirectional keep-alive with sequence tracking)
typedef struct __attribute__((packed)) {
    uint8_t  type;          // msg_heartbeat
    uint32_t seq;           // Monotonic sequence number (detects loss/resets)
    uint32_t uptime_ms;     // Sender uptime in milliseconds
    uint64_t unix_time;     // Unix timestamp in seconds (NTP-synchronized)
    uint8_t  time_source;   // 0=unsynced, 1=NTP, 2=manual, 3=GPS
    uint8_t  state;         // Connection state enum
    uint8_t  rssi;          // Last RX RSSI (if known, 0 if N/A)
    uint8_t  flags;         // Status flags (e.g., low_batt, degraded)
    uint16_t checksum;      // CRC16 checksum
} heartbeat_t;

// Heartbeat acknowledgment (receiver confirms receipt)
typedef struct __attribute__((packed)) {
    uint8_t  type;          // msg_heartbeat_ack
    uint32_t ack_seq;       // Sequence number being acknowledged
    uint32_t uptime_ms;     // Receiver uptime in milliseconds
    uint8_t  state;         // Receiver connection state
    uint16_t checksum;      // CRC16 checksum
} heartbeat_ack_t;

// Placeholder config types (simplified for now - will expand in Phase 3)
typedef struct __attribute__((packed)) {
    uint32_t ip;
    uint32_t gateway;
    uint32_t subnet;
    uint8_t use_dhcp;
} network_config_t;

typedef struct __attribute__((packed)) {
    char server[64];
    uint16_t port;
    char username[32];
    char password[32];
    uint8_t enabled;
} mqtt_config_t;

typedef struct __attribute__((packed)) {
    uint16_t capacity_kwh;
    uint8_t cell_count;
    uint8_t chemistry;  // 0=LFP, 1=NMC, etc.
} battery_config_t;

// Configuration changed notification (bidirectional)
typedef struct __attribute__((packed)) {
    uint8_t type;           // msg_config_changed
    uint8_t config_type;    // 1=network, 2=mqtt, 3=battery (maps to CacheDataType)
    uint32_t version;       // Configuration version number
    uint32_t timestamp;     // Change timestamp (millis)
    uint8_t data[160];      // Configuration payload (union of network/mqtt/battery)
} config_changed_t;

#ifdef __cplusplus
static_assert(sizeof(mqtt_config_t) <= sizeof(((config_changed_t*)0)->data),
              "mqtt_config_t too large for config_changed_t payload");
#endif

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

// Battery settings full message - All configurable battery settings (for bidirectional sync)
typedef struct __attribute__((packed)) {
    uint8_t type;                        // msg_battery_info (reuses same message type)
    uint32_t capacity_wh;                // Battery capacity in Wh
    uint32_t max_voltage_mv;             // Max pack voltage in mV
    uint32_t min_voltage_mv;             // Min pack voltage in mV
    float max_charge_current_a;          // Max charge current in A
    float max_discharge_current_a;       // Max discharge current in A
    uint8_t soc_high_limit;              // SOC high limit (50-100%)
    uint8_t soc_low_limit;               // SOC low limit (0-50%)
    uint8_t cell_count;                  // Number of cells in series
    uint8_t chemistry;                   // Battery chemistry (0=NCA, 1=NMC, 2=LFP, 3=LTO)
    uint16_t checksum;                   // Message checksum
} battery_settings_full_msg_t;  // Total: 28 bytes

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

// Component configuration message - Active component selections (sent on connect + every 5s)
typedef struct __attribute__((packed)) {
    uint8_t type;                    // msg_component_config
    uint8_t bms_type;                // Primary BMS type (0-45)
    uint8_t secondary_bms_type;      // Secondary BMS type (0=disabled)
    uint8_t battery_type;            // Battery profile type (0-31, 29=PYLON_BATTERY)
    uint8_t inverter_type;           // Inverter type (0-21, 0=disabled)
    uint8_t charger_type;            // Charger type (0-2, 0=disabled)
    uint8_t shunt_type;              // Shunt type (0-2, 0=disabled)
    uint8_t multi_battery_enabled;   // Multi-battery mode (0=off, 1=on)
    uint32_t config_version;         // Configuration version for change tracking
    uint16_t checksum;               // Message checksum
} component_config_msg_t;  // Total: 16 bytes

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
    SETTINGS_NETWORK = 5,
    SETTINGS_POWER = 6,
    SETTINGS_CAN = 7,
    SETTINGS_CONTACTOR = 8
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
    BATTERY_CHEMISTRY = 8,
    BATTERY_DOUBLE_ENABLED = 9,
    BATTERY_PACK_MAX_VOLTAGE_DV = 10,
    BATTERY_PACK_MIN_VOLTAGE_DV = 11,
    BATTERY_CELL_MAX_VOLTAGE_MV = 12,
    BATTERY_CELL_MIN_VOLTAGE_MV = 13,
    BATTERY_SOC_ESTIMATED = 14
};

// Power settings field IDs
enum PowerSettingsField : uint8_t {
    POWER_CHARGE_W = 0,
    POWER_DISCHARGE_W = 1,
    POWER_MAX_PRECHARGE_MS = 2,
    POWER_PRECHARGE_DURATION_MS = 3
};

// Inverter settings field IDs
enum InverterSettingsField : uint8_t {
    INVERTER_CELLS = 0,
    INVERTER_MODULES = 1,
    INVERTER_CELLS_PER_MODULE = 2,
    INVERTER_VOLTAGE_LEVEL = 3,
    INVERTER_CAPACITY_AH = 4,
    INVERTER_BATTERY_TYPE = 5
};

// CAN settings field IDs
enum CanSettingsField : uint8_t {
    CAN_FREQUENCY_KHZ = 0,
    CAN_FD_FREQUENCY_MHZ = 1,
    CAN_SOFAR_ID = 2,
    CAN_PYLON_SEND_INTERVAL_MS = 3
};

// Contactor control field IDs
enum ContactorSettingsField : uint8_t {
    CONTACTOR_CONTROL_ENABLED = 0,
    CONTACTOR_NC_MODE = 1,
    CONTACTOR_PWM_FREQUENCY_HZ = 2
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
    uint32_t new_version;        // New version number after update
    char error_msg[44];          // Error description if failed (reduced to fit new_version)
    uint16_t checksum;           // Message checksum
} settings_update_ack_msg_t;  // Total: 54 bytes

// Settings change notification - Transmitter → Receiver (push-based sync)
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_settings_changed
    uint8_t category;            // Which settings section changed
    uint32_t new_version;        // New version number
    uint16_t checksum;           // Message checksum
} settings_changed_msg_t;  // Total: 8 bytes

// =============================================================================
// PHASE 3: Network Configuration Message Structures
// =============================================================================

// Network configuration request message - Receiver → Transmitter
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_network_config_request
} network_config_request_t;  // Total: 1 byte

// Network configuration update message - Receiver → Transmitter
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_network_config_update
    uint8_t use_static_ip;       // 0 = DHCP, 1 = Static
    uint8_t ip[4];               // Static IP octets (e.g., 192.168.1.100)
    uint8_t gateway[4];          // Gateway octets
    uint8_t subnet[4];           // Subnet mask octets
    uint8_t dns_primary[4];      // Primary DNS server octets
    uint8_t dns_secondary[4];    // Secondary DNS server octets
    uint32_t config_version;     // Version number for tracking
    uint16_t checksum;           // Simple checksum for integrity
} network_config_update_t;  // Total: 32 bytes

// Network configuration ACK - Transmitter → Receiver
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_network_config_ack
    uint8_t success;             // 0 = failed, 1 = success
    uint8_t use_static_ip;       // Current mode (0=DHCP, 1=Static)
    
    // Current network configuration (active IP - DHCP or Static)
    uint8_t current_ip[4];       // Current IP address
    uint8_t current_gateway[4];  // Current gateway
    uint8_t current_subnet[4];   // Current subnet mask
    
    // Saved static configuration (from NVS - used when static mode is enabled)
    uint8_t static_ip[4];        // Static IP address
    uint8_t static_gateway[4];   // Static gateway
    uint8_t static_subnet[4];    // Static subnet mask
    uint8_t static_dns_primary[4];    // Static DNS primary
    uint8_t static_dns_secondary[4];  // Static DNS secondary
    
    uint32_t config_version;     // Configuration version
    char message[32];            // Status message
} network_config_ack_t;  // Total: 85 bytes

// =============================================================================
// PHASE 4: MQTT Configuration Message Structures
// =============================================================================

// MQTT configuration request message - Receiver → Transmitter
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_mqtt_config_request
} mqtt_config_request_t;  // Total: 1 byte

// MQTT configuration update message - Receiver → Transmitter
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_mqtt_config_update
    uint8_t enabled;             // 0 = disabled, 1 = enabled
    uint8_t server[4];           // MQTT broker IP (4 octets: xxx.xxx.xxx.xxx)
    uint16_t port;               // MQTT broker port
    char username[32];           // Username (empty string if none)
    char password[32];           // Password (empty string if none)
    char client_id[32];          // Client ID
    uint32_t config_version;     // Version for tracking
    uint16_t checksum;           // Integrity check
} mqtt_config_update_t;  // Total: 106 bytes

// MQTT configuration ACK - Transmitter → Receiver
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_mqtt_config_ack
    uint8_t success;             // 0 = failed, 1 = success
    uint8_t enabled;             // Current MQTT enabled state
    uint8_t server[4];           // Current MQTT broker IP
    uint16_t port;               // Current MQTT port
    char username[32];           // Current username
    char password[32];           // Current password (masked as "********" in UI)
    char client_id[32];          // Current client ID
    uint8_t connected;           // MQTT connection status (0 = disconnected, 1 = connected)
    uint32_t config_version;     // Configuration version
    char message[64];            // Status message
    uint16_t checksum;           // Integrity check
} mqtt_config_ack_t;  // Total: 179 bytes

// =========================================================================
// PHASE 4: Version-Based Cache Synchronization Structures
// =========================================================================

// Configuration section identifiers for targeted updates
enum config_section_t : uint8_t {
    config_section_mqtt = 0x01,
    config_section_network = 0x02,
    config_section_battery = 0x03,
    config_section_power_profile = 0x04,
    config_section_metadata = 0x05
};

// Lightweight version beacon (transmitter → receiver, every 15s)
typedef struct __attribute__((packed)) {
    uint8_t type;                       // msg_version_beacon
    uint32_t mqtt_config_version;       // MQTT config version number
    uint32_t network_config_version;    // Network config version number
    uint32_t battery_settings_version;  // Battery settings version number
    uint32_t power_profile_version;     // Power profile version number
    uint32_t metadata_config_version;   // Metadata version number
    bool mqtt_connected;                // Runtime MQTT connection status
    bool ethernet_connected;            // Runtime Ethernet link status
    
    // Firmware metadata (no need for separate metadata_response message)
    char env_name[32];                  // Environment name (e.g., "olimex_esp32_poe2")
    uint8_t version_major;              // Major version
    uint8_t version_minor;              // Minor version
    uint8_t version_patch;              // Patch version
    uint8_t reserved[1];                // Future expansion
} version_beacon_t;  // Total: 62 bytes (still well under ESP-NOW's 250 byte limit)

// Config section request (receiver → transmitter when version mismatch detected)
typedef struct __attribute__((packed)) {
    uint8_t type;                       // msg_config_section_request
    config_section_t section;           // Which config section to send
    uint32_t requested_version;         // Version number receiver wants
    uint8_t reserved[10];               // Padding/future expansion
} config_section_request_t;  // Total: 16 bytes

// Structure for queued ESP-NOW messages (holds raw data for processing in worker task)
typedef struct {
    uint8_t data[250];     // Raw message data (ESP-NOW max payload)
    uint8_t mac[6];        // Sender MAC address
    int len;               // Message length
    uint32_t timestamp;    // Reception timestamp
} espnow_queue_msg_t;

#endif // ESPNOW_COMMON_H
