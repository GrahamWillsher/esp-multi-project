#pragma once

#include <stdint.h>
#include <string.h>

// Configuration section identifiers
enum ConfigSection : uint8_t {
    CONFIG_MQTT = 0x01,           // MQTT broker settings
    CONFIG_NETWORK = 0x02,        // Network/Ethernet settings
    CONFIG_BATTERY = 0x03,        // Battery configuration
    CONFIG_POWER = 0x04,          // Power settings
    CONFIG_INVERTER = 0x05,       // Inverter configuration
    CONFIG_CAN = 0x06,            // CAN bus settings
    CONFIG_CONTACTOR = 0x07,      // Contactor control
    CONFIG_SYSTEM = 0x08          // System-level settings
};

// Field identifiers for each section

// MQTT fields
enum MqttField : uint8_t {
    MQTT_SERVER = 0x01,
    MQTT_PORT = 0x02,
    MQTT_USERNAME = 0x03,
    MQTT_PASSWORD = 0x04,
    MQTT_CLIENT_ID = 0x05,
    MQTT_TOPIC_PREFIX = 0x06,
    MQTT_ENABLED = 0x07,
    MQTT_TIMEOUT = 0x08
};

// Network fields
enum NetworkField : uint8_t {
    NET_USE_STATIC = 0x01,
    NET_IP_ADDRESS = 0x02,
    NET_GATEWAY = 0x03,
    NET_SUBNET = 0x04,
    NET_DNS = 0x05,
    NET_HOSTNAME = 0x06
};

// Battery fields
enum BatteryField : uint8_t {
    BATT_PACK_V_MAX = 0x01,
    BATT_PACK_V_MIN = 0x02,
    BATT_CELL_V_MAX = 0x03,
    BATT_CELL_V_MIN = 0x04,
    BATT_DOUBLE = 0x05,
    BATT_USE_EST_SOC = 0x06,
    BATT_CHEMISTRY = 0x07
};

// Power fields
enum PowerField : uint8_t {
    POWER_CHARGE_W = 0x01,
    POWER_DISCHARGE_W = 0x02,
    POWER_MAX_PRECHARGE_MS = 0x03,
    POWER_PRECHARGE_DUR_MS = 0x04
};

// Inverter fields
enum InverterField : uint8_t {
    INV_TOTAL_CELLS = 0x01,
    INV_MODULES = 0x02,
    INV_CELLS_PER_MODULE = 0x03,
    INV_VOLTAGE_LEVEL = 0x04,
    INV_CAPACITY_AH = 0x05,
    INV_BATTERY_TYPE = 0x06
};

// CAN fields
enum CanField : uint8_t {
    CAN_FREQUENCY_KHZ = 0x01,
    CAN_FD_FREQ_MHZ = 0x02,
    CAN_SOFAR_ID = 0x03,
    CAN_PYLON_INTERVAL = 0x04
};

// Contactor fields
enum ContactorField : uint8_t {
    CONT_CONTROL_EN = 0x01,
    CONT_NC_MODE = 0x02,
    CONT_PWM_FREQ = 0x03
};

// System fields
enum SystemField : uint8_t {
    SYS_LED_MODE = 0x01,
    SYS_WEB_ENABLED = 0x02,
    SYS_LOG_LEVEL = 0x03
};

// Version tracking structure
struct ConfigVersion {
    uint16_t global_version;      // Incremented on any config change
    uint16_t section_versions[8]; // Per-section version tracking (indexed by ConfigSection - 1)
    
    ConfigVersion() : global_version(1) {
        memset(section_versions, 0, sizeof(section_versions));
    }
} __attribute__((packed));

// MQTT Configuration
struct MqttConfig {
    char server[64];              // MQTT broker IP/hostname
    uint16_t port;                // MQTT port (default 1883)
    char username[32];            // MQTT username
    char password[32];            // MQTT password
    char client_id[32];           // MQTT client identifier
    char topic_prefix[32];        // Base topic for publishing
    bool enabled;                 // MQTT enable/disable
    uint16_t timeout_ms;          // Connection timeout
    
    MqttConfig() : port(1883), enabled(false), timeout_ms(5000) {
        memset(server, 0, sizeof(server));
        memset(username, 0, sizeof(username));
        memset(password, 0, sizeof(password));
        memset(client_id, 0, sizeof(client_id));
        memset(topic_prefix, 0, sizeof(topic_prefix));
    }
} __attribute__((packed));

// Network Configuration
struct NetworkConfig {
    bool use_static_ip;           // Static vs DHCP
    uint8_t ip[4];                // Static IP address
    uint8_t gateway[4];           // Gateway address
    uint8_t subnet[4];            // Subnet mask
    uint8_t dns[4];               // DNS server
    char hostname[32];            // Device hostname
    
    NetworkConfig() : use_static_ip(false) {
        memset(ip, 0, sizeof(ip));
        memset(gateway, 0, sizeof(gateway));
        memset(subnet, 0, sizeof(subnet));
        memset(dns, 0, sizeof(dns));
        memset(hostname, 0, sizeof(hostname));
    }
} __attribute__((packed));

// Battery Configuration
struct BatteryConfig {
    uint16_t pack_voltage_max;    // Max pack voltage (mV)
    uint16_t pack_voltage_min;    // Min pack voltage (mV)
    uint16_t cell_voltage_max;    // Max cell voltage (mV)
    uint16_t cell_voltage_min;    // Min cell voltage (mV)
    bool double_battery;          // Dual battery mode
    bool use_estimated_soc;       // Use SOC estimation
    uint8_t chemistry;            // Battery chemistry type
    
    BatteryConfig() : pack_voltage_max(0), pack_voltage_min(0), 
                     cell_voltage_max(0), cell_voltage_min(0),
                     double_battery(false), use_estimated_soc(false),
                     chemistry(0) {}
} __attribute__((packed));

// Power Settings
struct PowerConfig {
    uint16_t charge_power_w;      // Max charge power (W)
    uint16_t discharge_power_w;   // Max discharge power (W)
    uint16_t max_precharge_ms;    // Max precharge time
    uint16_t precharge_duration_ms; // Precharge duration
    
    PowerConfig() : charge_power_w(0), discharge_power_w(0),
                   max_precharge_ms(15000), precharge_duration_ms(100) {}
} __attribute__((packed));

// Inverter Configuration
struct InverterConfig {
    uint8_t total_cells;          // Total cell count
    uint8_t modules;              // Number of modules
    uint8_t cells_per_module;     // Cells per module
    uint16_t voltage_level;       // Nominal voltage
    uint16_t capacity_ah;         // Capacity in Ah
    uint8_t battery_type;         // Battery type enum
    
    InverterConfig() : total_cells(0), modules(0), cells_per_module(0),
                      voltage_level(0), capacity_ah(0), battery_type(0) {}
} __attribute__((packed));

// CAN Configuration
struct CanConfig {
    uint16_t frequency_khz;       // CAN bus frequency
    uint16_t fd_frequency_mhz;    // CAN-FD frequency
    uint16_t sofar_id;            // Sofar inverter ID
    uint16_t pylon_send_interval; // Pylon protocol interval
    
    CanConfig() : frequency_khz(8), fd_frequency_mhz(40),
                 sofar_id(0), pylon_send_interval(1000) {}
} __attribute__((packed));

// Contactor Control
struct ContactorConfig {
    bool control_enabled;         // Enable contactor control
    bool nc_contactor;            // Normally closed mode
    uint16_t pwm_frequency;       // PWM frequency (Hz)
    
    ContactorConfig() : control_enabled(false), nc_contactor(false),
                       pwm_frequency(1000) {}
} __attribute__((packed));

// System Configuration
struct SystemConfig {
    uint8_t led_mode;             // LED mode
    bool web_enabled;             // Web server enabled
    uint16_t log_level;           // Logging verbosity
    
    SystemConfig() : led_mode(0), web_enabled(true), log_level(3) {}
} __attribute__((packed));

// Full configuration snapshot
struct FullConfigSnapshot {
    ConfigVersion version;
    MqttConfig mqtt;
    NetworkConfig network;
    BatteryConfig battery;
    PowerConfig power;
    InverterConfig inverter;
    CanConfig can;
    ContactorConfig contactor;
    SystemConfig system;
    uint32_t checksum;            // CRC32 for integrity
    
    FullConfigSnapshot() : checksum(0) {}
} __attribute__((packed));

// Helper function to calculate CRC32
inline uint32_t calculateCRC32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}
