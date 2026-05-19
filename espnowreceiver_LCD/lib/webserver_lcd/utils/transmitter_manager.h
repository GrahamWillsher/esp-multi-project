#ifndef TRANSMITTER_MANAGER_H
#define TRANSMITTER_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "transmitter_settings_types.h"
#include "transmitter_event_log_types.h"

class TransmitterManager {
public:
    using EventLogEntry = TransmitterEventLogTypes::EventLogEntry;

    struct EventLogSummary {
        bool known = false;
        uint32_t seq = 0;
        uint32_t total_historical = 0;
        uint32_t error_historical = 0;
        uint32_t new_since_last_report_total = 0;
        uint32_t new_since_last_report_error = 0;
        uint32_t uptime_ms = 0;
        uint32_t last_update_ms = 0;
    };

    struct TemperatureReport {
        bool known = false;
        bool valid = false;
        uint32_t seq = 0;
        int16_t temperature_centi_c = 0;
        uint32_t uptime_ms = 0;
        uint32_t last_update_ms = 0;
    };

    static constexpr uint8_t kEventLogsClearAckFailed = 0;
    static constexpr uint8_t kEventLogsClearAckSuccess = 1;

    struct EventLogClearAck {
        bool known = false;
        uint8_t status = kEventLogsClearAckFailed;
        uint32_t summary_seq = 0;
        uint32_t uptime_ms = 0;
        uint32_t last_update_ms = 0;
    };



public:
    // Initialization (load cache from NVS)
    static void init();

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
    static const char* getMqttClientId();
    static bool isMqttConnected();
    static bool isMqttConfigKnown();
    static uint32_t getMqttConfigVersion();  // Phase 4: Get cached MQTT config version
    
    // Phase 4: Runtime status update from version beacons
    static void updateRuntimeStatus(bool mqtt_conn, bool eth_conn);
    static bool isEthernetConnected();
    static void updateSendStatus(bool success);
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
    
    // Battery settings management
    static void storeBatterySettings(const BatterySettings& settings);
    static BatterySettings getBatterySettings();
    static bool hasBatterySettings();

    static void storeBatteryEmulatorSettings(const BatteryEmulatorSettings& settings);
    static BatteryEmulatorSettings getBatteryEmulatorSettings();
    static bool hasBatteryEmulatorSettings();

    static void storePowerSettings(const PowerSettings& settings);
    static PowerSettings getPowerSettings();

    static void storeInverterSettings(const InverterSettings& settings);
    static InverterSettings getInverterSettings();

    static void storeCanSettings(const CanSettings& settings);
    static CanSettings getCanSettings();

    static void storeContactorSettings(const ContactorSettings& settings);
    static ContactorSettings getContactorSettings();
    
    // Time data management
    static uint64_t getUptimeMs();
    static uint64_t getUnixTime();
    static int16_t getUtcOffsetMin();
    static uint8_t getTimeSource();
    static void updateTimeData(uint64_t new_uptime_ms, uint64_t new_unix_time, int16_t new_utc_offset_min, uint8_t new_time_source);
    static void updateHeartbeatFlags(uint8_t heartbeat_flags);
    static bool isGeolocationValid();
    
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
    
    // Phase 3: Cell monitor data queries delegated to CellDataCache
    
    // Event logs (from transmitter via MQTT or HTTP proxy)
    static void storeEventLogs(const JsonObject& logs);
    static void clearEventLogs();
    static bool hasEventLogs();
    static void getEventLogsSnapshot(std::vector<EventLogEntry>& out_logs, uint32_t* out_last_update_ms = nullptr);

    // Event log summary counters (transport-agnostic cache)
    static void storeEventLogSummary(uint32_t seq,
                                     uint32_t total_historical,
                                     uint32_t error_historical,
                                     uint32_t new_since_last_report_total,
                                     uint32_t new_since_last_report_error,
                                     uint32_t uptime_ms);
    static EventLogSummary getEventLogSummary();

    static void storeEventLogClearAck(uint8_t status,
                                      uint32_t summary_seq,
                                      uint32_t uptime_ms);
    static EventLogClearAck getEventLogClearAck();

    // Transmitter temperature (transport-agnostic cache)
    static void storeTemperatureReport(bool valid,
                                       uint32_t seq,
                                       int16_t temperature_centi_c,
                                       uint32_t uptime_ms);
    static TemperatureReport getTemperatureReport();
};

#endif
