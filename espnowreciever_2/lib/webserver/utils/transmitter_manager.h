#ifndef TRANSMITTER_MANAGER_H
#define TRANSMITTER_MANAGER_H

#include <Arduino.h>

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
    
    // Battery settings (cached from PACKET/SETTINGS)
    static BatterySettings battery_settings;
    static bool battery_settings_known;
    
public:
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
    
    // Battery settings management
    static void storeBatterySettings(const BatterySettings& settings);
    static BatterySettings getBatterySettings();
    static bool hasBatterySettings();
};

#endif
