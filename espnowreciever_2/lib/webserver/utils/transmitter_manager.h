#ifndef TRANSMITTER_MANAGER_H
#define TRANSMITTER_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Battery settings structure (matches transmitter)
struct BatterySettings {
    uint32_t capacity_wh;
    uint32_t max_voltage_mv;
    uint32_t min_voltage_mv;
    float max_charge_current_a;
    float max_discharge_current_a;
    uint8_t soc_high_limit;
    uint8_t soc_low_limit;
    uint8_t cell_count;
    uint8_t chemistry;
    uint32_t version;  // Version tracking for synchronization
};

struct BatteryEmulatorSettings {
    bool double_battery;
    uint16_t pack_max_voltage_dV;
    uint16_t pack_min_voltage_dV;
    uint16_t cell_max_voltage_mV;
    uint16_t cell_min_voltage_mV;
    bool soc_estimated;
};

struct PowerSettings {
    uint16_t charge_w;
    uint16_t discharge_w;
    uint16_t max_precharge_ms;
    uint16_t precharge_duration_ms;
};

struct InverterSettings {
    uint8_t cells;
    uint8_t modules;
    uint8_t cells_per_module;
    uint16_t voltage_level;
    uint16_t capacity_ah;
    uint8_t battery_type;
};

struct CanSettings {
    uint16_t frequency_khz;
    uint16_t fd_frequency_mhz;
    uint16_t sofar_id;
    uint16_t pylon_send_interval_ms;
};

struct ContactorSettings {
    bool control_enabled;
    bool nc_contactor;
    uint16_t pwm_frequency_hz;
};

class TransmitterManager {
private:
    static uint8_t mac[6];
    static bool mac_known;
    
    // Current network configuration (active IP - could be DHCP or Static)
    static uint8_t current_ip[4];
    static uint8_t current_gateway[4];
    static uint8_t current_subnet[4];
    
    // Saved static configuration (from transmitter NVS)
    static uint8_t static_ip[4];
    static uint8_t static_gateway[4];
    static uint8_t static_subnet[4];
    static uint8_t static_dns_primary[4];
    static uint8_t static_dns_secondary[4];
    
    static bool ip_known;
    static bool is_static_ip;  // True if using static IP, false if DHCP
    static uint32_t network_config_version;  // Version from NVS
    
    // MQTT configuration (from transmitter)
    static bool mqtt_enabled;
    static uint8_t mqtt_server[4];
    static uint16_t mqtt_port;
    static char mqtt_username[32];
    static char mqtt_password[32];
    static char mqtt_client_id[32];
    static bool mqtt_connected;
    static uint32_t mqtt_config_version;
    static bool mqtt_config_known;
    
    // Phase 4: Runtime status tracking (from version beacons)
    static bool ethernet_connected;
    static unsigned long last_beacon_time_ms;
    static bool last_espnow_send_success;
    
    // V2: Legacy version tracking removed - only use firmware metadata
    
    // Firmware metadata (from .rodata)
    static bool metadata_received;  // Flag indicating metadata was received
    static bool metadata_valid;
    static char metadata_env[32];
    static char metadata_device[16];
    static uint8_t metadata_major;
    static uint8_t metadata_minor;
    static uint8_t metadata_patch;
    static char metadata_build_date[48];
    static uint32_t metadata_version;
    
    // Battery settings (cached from PACKET/SETTINGS)
    static BatterySettings battery_settings;
    static bool battery_settings_known;

    static BatteryEmulatorSettings battery_emulator_settings;
    static bool battery_emulator_settings_known;

    static PowerSettings power_settings;
    static bool power_settings_known;

    static InverterSettings inverter_settings;
    static bool inverter_settings_known;

    static CanSettings can_settings;
    static bool can_settings_known;

    static ContactorSettings contactor_settings;
    static bool contactor_settings_known;
    
    // Time data (cached from heartbeat)
    static uint64_t uptime_ms;
    static uint64_t unix_time;
    static uint8_t time_source;  // 0=unsynced, 1=NTP, 2=manual, 3=GPS
    
    // Phase 3: Static spec data from battery emulator (via MQTT)
    static String static_specs_json_;
    static String battery_specs_json_;
    static String inverter_specs_json_;
    static String charger_specs_json_;
    static String system_specs_json_;
    static bool static_specs_known_;
    
    // Cell monitor data (from BE/cell_data MQTT topic)
    static uint16_t* cell_voltages_mV_;      // Dynamically allocated array
    static bool* cell_balancing_status_;     // Dynamically allocated array
    static uint16_t cell_count_;
    static uint16_t cell_min_voltage_mV_;
    static uint16_t cell_max_voltage_mV_;
    static bool balancing_active_;
    static bool cell_data_known_;
    
public:
    // Initialization (load cache from NVS)
    static void init();
    static void loadFromNVS();
    static void saveToNVS();

    // MAC management
    static void registerMAC(const uint8_t* transmitter_mac);
    static const uint8_t* getMAC();
    static bool isMACKnown();
    static String getMACString();
    
    // IP management
    static void storeIPData(const uint8_t* transmitter_ip, 
                           const uint8_t* transmitter_gateway,
                           const uint8_t* transmitter_subnet,
                           bool is_static = false,
                           uint32_t config_version = 0);
    
    // New: Store complete network configuration (current + static)
    static void storeNetworkConfig(const uint8_t* curr_ip, 
                                   const uint8_t* curr_gateway,
                                   const uint8_t* curr_subnet,
                                   const uint8_t* stat_ip,
                                   const uint8_t* stat_gateway,
                                   const uint8_t* stat_subnet,
                                   const uint8_t* stat_dns1,
                                   const uint8_t* stat_dns2,
                                   bool is_static,
                                   uint32_t config_version);
    
    // Current network configuration (active - could be DHCP or Static)
    static const uint8_t* getIP();           // Returns current_ip
    static const uint8_t* getGateway();      // Returns current_gateway
    static const uint8_t* getSubnet();       // Returns current_subnet
    
    // Saved static configuration (from transmitter NVS)
    static const uint8_t* getStaticIP();
    static const uint8_t* getStaticGateway();
    static const uint8_t* getStaticSubnet();
    static const uint8_t* getStaticDNSPrimary();
    static const uint8_t* getStaticDNSSecondary();
    
    static bool isIPKnown();
    static bool isStaticIP();
    static uint32_t getNetworkConfigVersion();
    static void updateNetworkMode(bool is_static, uint32_t version);  // Update mode/version without full IP data
    static String getIPString();
    static String getURL();  // Returns http://x.x.x.x
    
    // MQTT management
    static void storeMqttConfig(bool enabled, const uint8_t* server, uint16_t port,
                               const char* username, const char* password,
                               const char* client_id, bool connected, uint32_t version);
    static bool isMqttEnabled();
    static const uint8_t* getMqttServer();
    static uint16_t getMqttPort();
    static const char* getMqttUsername();
    static const char* getMqttPassword();
    static const char* getMqttClientId();
    static bool isMqttConnected();
    static bool isMqttConfigKnown();
    static uint32_t getMqttConfigVersion();  // Phase 4: Get cached MQTT config version
    static String getMqttServerString();
    
    // Phase 4: Runtime status update from version beacons
    static void updateRuntimeStatus(bool mqtt_conn, bool eth_conn);
    static bool isEthernetConnected();
    static unsigned long getLastBeaconTime();
    static void updateSendStatus(bool success);
    static bool wasLastSendSuccessful();
    static bool isTransmitterConnected();
    
    // V2: Legacy version functions removed - only use metadata
    
    // Metadata management
    static void storeMetadata(bool valid, const char* env, const char* device,
                             uint8_t major, uint8_t minor, uint8_t patch,
                             const char* build_date);
    static bool hasMetadata();
    static bool isMetadataValid();
    static const char* getMetadataEnv();
    static const char* getMetadataDevice();
    static void getMetadataVersion(uint8_t& major, uint8_t& minor, uint8_t& patch);
    static const char* getMetadataBuildDate();
    static uint32_t getMetadataVersionNumber();
    
    // Battery settings management
    static void storeBatterySettings(const BatterySettings& settings);
    static BatterySettings getBatterySettings();
    static bool hasBatterySettings();

    static void storeBatteryEmulatorSettings(const BatteryEmulatorSettings& settings);
    static BatteryEmulatorSettings getBatteryEmulatorSettings();
    static bool hasBatteryEmulatorSettings();

    static void storePowerSettings(const PowerSettings& settings);
    static PowerSettings getPowerSettings();
    static bool hasPowerSettings();

    static void storeInverterSettings(const InverterSettings& settings);
    static InverterSettings getInverterSettings();
    static bool hasInverterSettings();

    static void storeCanSettings(const CanSettings& settings);
    static CanSettings getCanSettings();
    static bool hasCanSettings();

    static void storeContactorSettings(const ContactorSettings& settings);
    static ContactorSettings getContactorSettings();
    static bool hasContactorSettings();
    
    // Time data management
    static uint64_t getUptimeMs();
    static uint64_t getUnixTime();
    static uint8_t getTimeSource();
    static void updateTimeData(uint64_t new_uptime_ms, uint64_t new_unix_time, uint8_t new_time_source);
    
    // Phase 3: Static spec data from battery emulator (via MQTT)
    static void storeStaticSpecs(const JsonObject& specs);
    static void storeBatterySpecs(const JsonObject& specs);
    static void storeInverterSpecs(const JsonObject& specs);
    static void storeChargerSpecs(const JsonObject& specs);
    static void storeSystemSpecs(const JsonObject& specs);
    static bool hasStaticSpecs();
    static String getStaticSpecsJson();  // Returns combined JSON for API
    static String getBatterySpecsJson();
    static String getInverterSpecsJson();
    static String getChargerSpecsJson();
    static String getSystemSpecsJson();
    
    // Cell monitor data (from BE/cell_data MQTT topic)
    static void storeCellData(const JsonObject& cell_data);
    static bool hasCellData() { return cell_data_known_; }
    static uint16_t getCellCount() { return cell_count_; }
    static const uint16_t* getCellVoltages() { return cell_voltages_mV_; }
    static const bool* getCellBalancingStatus() { return cell_balancing_status_; }
    static uint16_t getCellMinVoltage() { return cell_min_voltage_mV_; }
    static uint16_t getCellMaxVoltage() { return cell_max_voltage_mV_; }
    static bool isBalancingActive() { return balancing_active_; }
};

#endif
