#include "transmitter_manager.h"
#include "sse_notifier.h"
#include <connection_manager.h>
#include <esp_now.h>
#include <string.h>
#include <Preferences.h>

namespace {
    constexpr const char* kTxCacheNamespace = "tx_cache";

    constexpr const char* kKeyMqttEnabled = "mqtt_enabled";
    constexpr const char* kKeyMqttServer = "mqtt_server";
    constexpr const char* kKeyMqttPort = "mqtt_port";
    constexpr const char* kKeyMqttUser = "mqtt_user";
    constexpr const char* kKeyMqttPass = "mqtt_pass";
    constexpr const char* kKeyMqttClient = "mqtt_client";
    constexpr const char* kKeyMqttVersion = "mqtt_ver";
    constexpr const char* kKeyMqttKnown = "mqtt_known";

    constexpr const char* kKeyNetCurrIp = "net_curr_ip";
    constexpr const char* kKeyNetCurrGw = "net_curr_gw";
    constexpr const char* kKeyNetCurrSn = "net_curr_sn";
    constexpr const char* kKeyNetStatIp = "net_stat_ip";
    constexpr const char* kKeyNetStatGw = "net_stat_gw";
    constexpr const char* kKeyNetStatSn = "net_stat_sn";
    constexpr const char* kKeyNetDns1 = "net_dns1";
    constexpr const char* kKeyNetDns2 = "net_dns2";
    constexpr const char* kKeyNetIsStatic = "net_is_static";
    constexpr const char* kKeyNetVersion = "net_ver";
    constexpr const char* kKeyNetKnown = "net_known";

    constexpr const char* kKeyMetaKnown = "meta_known";
    constexpr const char* kKeyMetaValid = "meta_valid";
    constexpr const char* kKeyMetaEnv = "meta_env";
    constexpr const char* kKeyMetaDevice = "meta_device";
    constexpr const char* kKeyMetaMajor = "meta_major";
    constexpr const char* kKeyMetaMinor = "meta_minor";
    constexpr const char* kKeyMetaPatch = "meta_patch";
    constexpr const char* kKeyMetaBuild = "meta_build";
    constexpr const char* kKeyMetaVersion = "meta_ver";

    constexpr const char* kKeyBatteryKnown = "batt_known";
    constexpr const char* kKeyBatterySettings = "batt_settings";

    constexpr const char* kKeyBatteryEmuKnown = "batt_emu_known";
    constexpr const char* kKeyBatteryEmuSettings = "batt_emu_set";

    constexpr const char* kKeyPowerKnown = "power_known";
    constexpr const char* kKeyPowerSettings = "power_settings";

    constexpr const char* kKeyInverterKnown = "inv_known";
    constexpr const char* kKeyInverterSettings = "inv_settings";

    constexpr const char* kKeyCanKnown = "can_known";
    constexpr const char* kKeyCanSettings = "can_settings";

    constexpr const char* kKeyContactorKnown = "contactor_known";
    constexpr const char* kKeyContactorSettings = "contactor_set";
}

// Static member initialization
uint8_t TransmitterManager::mac[6] = {0};
bool TransmitterManager::mac_known = false;

// Current network configuration (active IP - could be DHCP or Static)
uint8_t TransmitterManager::current_ip[4] = {0};
uint8_t TransmitterManager::current_gateway[4] = {0};
uint8_t TransmitterManager::current_subnet[4] = {0};

// Saved static configuration (from transmitter NVS)
uint8_t TransmitterManager::static_ip[4] = {0};
uint8_t TransmitterManager::static_gateway[4] = {0};
uint8_t TransmitterManager::static_subnet[4] = {0};
uint8_t TransmitterManager::static_dns_primary[4] = {8, 8, 8, 8};
uint8_t TransmitterManager::static_dns_secondary[4] = {8, 8, 4, 4};

bool TransmitterManager::ip_known = false;
bool TransmitterManager::is_static_ip = false;
uint32_t TransmitterManager::network_config_version = 0;

// MQTT configuration initialization
bool TransmitterManager::mqtt_enabled = false;
uint8_t TransmitterManager::mqtt_server[4] = {0};
uint16_t TransmitterManager::mqtt_port = 1883;
char TransmitterManager::mqtt_username[32] = {0};
char TransmitterManager::mqtt_password[32] = {0};
char TransmitterManager::mqtt_client_id[32] = {0};
bool TransmitterManager::mqtt_connected = false;
uint32_t TransmitterManager::mqtt_config_version = 0;
bool TransmitterManager::mqtt_config_known = false;

// Phase 4: Runtime status initialization
bool TransmitterManager::ethernet_connected = false;
unsigned long TransmitterManager::last_beacon_time_ms = 0;
bool TransmitterManager::last_espnow_send_success = true;

// Phase 4: Time and uptime data from heartbeats
uint64_t TransmitterManager::uptime_ms = 0;
uint64_t TransmitterManager::unix_time = 0;
uint8_t TransmitterManager::time_source = 0;  // 0=unsynced, 1=NTP, 2=manual, 3=GPS

// V2: Legacy version tracking removed
bool TransmitterManager::metadata_received = false;
bool TransmitterManager::metadata_valid = false;
char TransmitterManager::metadata_env[32] = {0};
char TransmitterManager::metadata_device[16] = {0};
uint8_t TransmitterManager::metadata_major = 0;
uint8_t TransmitterManager::metadata_minor = 0;
uint8_t TransmitterManager::metadata_patch = 0;
char TransmitterManager::metadata_build_date[48] = {0};
uint32_t TransmitterManager::metadata_version = 0;

// Battery settings initialization
BatterySettings TransmitterManager::battery_settings = {
    .capacity_wh = 30000,
    .max_voltage_mv = 58000,
    .min_voltage_mv = 46000,
    .max_charge_current_a = 100.0f,
    .max_discharge_current_a = 100.0f,
    .soc_high_limit = 95,
    .soc_low_limit = 20,
    .cell_count = 16,
    .chemistry = 2
};
bool TransmitterManager::battery_settings_known = false;

BatteryEmulatorSettings TransmitterManager::battery_emulator_settings = {
    .double_battery = false,
    .pack_max_voltage_dV = 580,
    .pack_min_voltage_dV = 460,
    .cell_max_voltage_mV = 4200,
    .cell_min_voltage_mV = 3000,
    .soc_estimated = false
};
bool TransmitterManager::battery_emulator_settings_known = false;

PowerSettings TransmitterManager::power_settings = {
    .charge_w = 3000,
    .discharge_w = 3000,
    .max_precharge_ms = 15000,
    .precharge_duration_ms = 100
};
bool TransmitterManager::power_settings_known = false;

InverterSettings TransmitterManager::inverter_settings = {
    .cells = 0,
    .modules = 0,
    .cells_per_module = 0,
    .voltage_level = 0,
    .capacity_ah = 0,
    .battery_type = 0
};
bool TransmitterManager::inverter_settings_known = false;

CanSettings TransmitterManager::can_settings = {
    .frequency_khz = 8,
    .fd_frequency_mhz = 40,
    .sofar_id = 0,
    .pylon_send_interval_ms = 0
};
bool TransmitterManager::can_settings_known = false;

ContactorSettings TransmitterManager::contactor_settings = {
    .control_enabled = false,
    .nc_contactor = false,
    .pwm_frequency_hz = 20000
};
bool TransmitterManager::contactor_settings_known = false;

// Phase 3: Static spec data (MQTT) initialization
String TransmitterManager::static_specs_json_ = "";
String TransmitterManager::battery_specs_json_ = "";
String TransmitterManager::inverter_specs_json_ = "";
String TransmitterManager::charger_specs_json_ = "";
String TransmitterManager::system_specs_json_ = "";
bool TransmitterManager::static_specs_known_ = false;

// Cell monitor data initialization
uint16_t* TransmitterManager::cell_voltages_mV_ = nullptr;
bool* TransmitterManager::cell_balancing_status_ = nullptr;
uint16_t TransmitterManager::cell_count_ = 0;
uint16_t TransmitterManager::cell_min_voltage_mV_ = 0;
uint16_t TransmitterManager::cell_max_voltage_mV_ = 0;
bool TransmitterManager::balancing_active_ = false;
bool TransmitterManager::cell_data_known_ = false;
char TransmitterManager::cell_data_source_[32] = "unknown";

std::vector<TransmitterManager::EventLogEntry> TransmitterManager::event_logs_ = {};
bool TransmitterManager::event_logs_known_ = false;
uint32_t TransmitterManager::event_logs_last_update_ms_ = 0;

void TransmitterManager::init() {
    loadFromNVS();
}

void TransmitterManager::loadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(kTxCacheNamespace, true)) {
        return;
    }

    mqtt_enabled = prefs.getBool(kKeyMqttEnabled, false);
    prefs.getBytes(kKeyMqttServer, mqtt_server, sizeof(mqtt_server));
    mqtt_port = prefs.getUShort(kKeyMqttPort, 1883);
    String mqtt_user = prefs.getString(kKeyMqttUser, "");
    String mqtt_pass = prefs.getString(kKeyMqttPass, "");
    String mqtt_client = prefs.getString(kKeyMqttClient, "");
    strncpy(mqtt_username, mqtt_user.c_str(), sizeof(mqtt_username) - 1);
    strncpy(mqtt_password, mqtt_pass.c_str(), sizeof(mqtt_password) - 1);
    strncpy(mqtt_client_id, mqtt_client.c_str(), sizeof(mqtt_client_id) - 1);
    mqtt_username[sizeof(mqtt_username) - 1] = '\0';
    mqtt_password[sizeof(mqtt_password) - 1] = '\0';
    mqtt_client_id[sizeof(mqtt_client_id) - 1] = '\0';
    mqtt_config_version = prefs.getUInt(kKeyMqttVersion, 0);
    mqtt_config_known = prefs.getBool(kKeyMqttKnown, false);

    prefs.getBytes(kKeyNetCurrIp, current_ip, sizeof(current_ip));
    prefs.getBytes(kKeyNetCurrGw, current_gateway, sizeof(current_gateway));
    prefs.getBytes(kKeyNetCurrSn, current_subnet, sizeof(current_subnet));
    prefs.getBytes(kKeyNetStatIp, static_ip, sizeof(static_ip));
    prefs.getBytes(kKeyNetStatGw, static_gateway, sizeof(static_gateway));
    prefs.getBytes(kKeyNetStatSn, static_subnet, sizeof(static_subnet));
    prefs.getBytes(kKeyNetDns1, static_dns_primary, sizeof(static_dns_primary));
    prefs.getBytes(kKeyNetDns2, static_dns_secondary, sizeof(static_dns_secondary));
    is_static_ip = prefs.getBool(kKeyNetIsStatic, false);
    network_config_version = prefs.getUInt(kKeyNetVersion, 0);
    ip_known = prefs.getBool(kKeyNetKnown, false);

    metadata_received = prefs.getBool(kKeyMetaKnown, false);
    metadata_valid = prefs.getBool(kKeyMetaValid, false);
    String meta_env = prefs.getString(kKeyMetaEnv, "");
    String meta_device = prefs.getString(kKeyMetaDevice, "");
    String meta_build = prefs.getString(kKeyMetaBuild, "");
    strncpy(metadata_env, meta_env.c_str(), sizeof(metadata_env) - 1);
    strncpy(metadata_device, meta_device.c_str(), sizeof(metadata_device) - 1);
    strncpy(metadata_build_date, meta_build.c_str(), sizeof(metadata_build_date) - 1);
    metadata_env[sizeof(metadata_env) - 1] = '\0';
    metadata_device[sizeof(metadata_device) - 1] = '\0';
    metadata_build_date[sizeof(metadata_build_date) - 1] = '\0';
    metadata_major = prefs.getUChar(kKeyMetaMajor, 0);
    metadata_minor = prefs.getUChar(kKeyMetaMinor, 0);
    metadata_patch = prefs.getUChar(kKeyMetaPatch, 0);
    metadata_version = prefs.getUInt(kKeyMetaVersion, 0);

    size_t batt_len = prefs.getBytes(kKeyBatterySettings, &battery_settings, sizeof(battery_settings));
    battery_settings_known = prefs.getBool(kKeyBatteryKnown, false) && batt_len == sizeof(battery_settings);

    size_t batt_emu_len = prefs.getBytes(kKeyBatteryEmuSettings, &battery_emulator_settings, sizeof(battery_emulator_settings));
    battery_emulator_settings_known = prefs.getBool(kKeyBatteryEmuKnown, false) && batt_emu_len == sizeof(battery_emulator_settings);

    size_t power_len = prefs.getBytes(kKeyPowerSettings, &power_settings, sizeof(power_settings));
    power_settings_known = prefs.getBool(kKeyPowerKnown, false) && power_len == sizeof(power_settings);

    size_t inverter_len = prefs.getBytes(kKeyInverterSettings, &inverter_settings, sizeof(inverter_settings));
    inverter_settings_known = prefs.getBool(kKeyInverterKnown, false) && inverter_len == sizeof(inverter_settings);

    size_t can_len = prefs.getBytes(kKeyCanSettings, &can_settings, sizeof(can_settings));
    can_settings_known = prefs.getBool(kKeyCanKnown, false) && can_len == sizeof(can_settings);

    size_t contactor_len = prefs.getBytes(kKeyContactorSettings, &contactor_settings, sizeof(contactor_settings));
    contactor_settings_known = prefs.getBool(kKeyContactorKnown, false) && contactor_len == sizeof(contactor_settings);

    prefs.end();
}

void TransmitterManager::saveToNVS() {
    Preferences prefs;
    if (!prefs.begin(kTxCacheNamespace, false)) {
        return;
    }

    prefs.putBool(kKeyMqttEnabled, mqtt_enabled);
    prefs.putBytes(kKeyMqttServer, mqtt_server, sizeof(mqtt_server));
    prefs.putUShort(kKeyMqttPort, mqtt_port);
    prefs.putString(kKeyMqttUser, mqtt_username);
    prefs.putString(kKeyMqttPass, mqtt_password);
    prefs.putString(kKeyMqttClient, mqtt_client_id);
    prefs.putUInt(kKeyMqttVersion, mqtt_config_version);
    prefs.putBool(kKeyMqttKnown, mqtt_config_known);

    prefs.putBytes(kKeyNetCurrIp, current_ip, sizeof(current_ip));
    prefs.putBytes(kKeyNetCurrGw, current_gateway, sizeof(current_gateway));
    prefs.putBytes(kKeyNetCurrSn, current_subnet, sizeof(current_subnet));
    prefs.putBytes(kKeyNetStatIp, static_ip, sizeof(static_ip));
    prefs.putBytes(kKeyNetStatGw, static_gateway, sizeof(static_gateway));
    prefs.putBytes(kKeyNetStatSn, static_subnet, sizeof(static_subnet));
    prefs.putBytes(kKeyNetDns1, static_dns_primary, sizeof(static_dns_primary));
    prefs.putBytes(kKeyNetDns2, static_dns_secondary, sizeof(static_dns_secondary));
    prefs.putBool(kKeyNetIsStatic, is_static_ip);
    prefs.putUInt(kKeyNetVersion, network_config_version);
    prefs.putBool(kKeyNetKnown, ip_known);

    prefs.putBool(kKeyMetaKnown, metadata_received);
    prefs.putBool(kKeyMetaValid, metadata_valid);
    prefs.putString(kKeyMetaEnv, metadata_env);
    prefs.putString(kKeyMetaDevice, metadata_device);
    prefs.putUChar(kKeyMetaMajor, metadata_major);
    prefs.putUChar(kKeyMetaMinor, metadata_minor);
    prefs.putUChar(kKeyMetaPatch, metadata_patch);
    prefs.putString(kKeyMetaBuild, metadata_build_date);
    prefs.putUInt(kKeyMetaVersion, metadata_version);

    prefs.putBool(kKeyBatteryKnown, battery_settings_known);
    prefs.putBytes(kKeyBatterySettings, &battery_settings, sizeof(battery_settings));

    prefs.putBool(kKeyBatteryEmuKnown, battery_emulator_settings_known);
    prefs.putBytes(kKeyBatteryEmuSettings, &battery_emulator_settings, sizeof(battery_emulator_settings));

    prefs.putBool(kKeyPowerKnown, power_settings_known);
    prefs.putBytes(kKeyPowerSettings, &power_settings, sizeof(power_settings));

    prefs.putBool(kKeyInverterKnown, inverter_settings_known);
    prefs.putBytes(kKeyInverterSettings, &inverter_settings, sizeof(inverter_settings));

    prefs.putBool(kKeyCanKnown, can_settings_known);
    prefs.putBytes(kKeyCanSettings, &can_settings, sizeof(can_settings));

    prefs.putBool(kKeyContactorKnown, contactor_settings_known);
    prefs.putBytes(kKeyContactorSettings, &contactor_settings, sizeof(contactor_settings));

    prefs.end();
}

void TransmitterManager::registerMAC(const uint8_t* transmitter_mac) {
    if (transmitter_mac == nullptr) return;
    
    memcpy(mac, transmitter_mac, 6);
    mac_known = true;
    
    Serial.printf("[TX_MGR] MAC registered: %s\n", getMACString().c_str());
    
    // Notify dashboard of cache update
    SSENotifier::notifyDataUpdated();
    
    // Add as ESP-NOW peer
    if (!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = 0;
        peer.encrypt = false;
        peer.ifidx = WIFI_IF_STA;
        
        if (esp_now_add_peer(&peer) == ESP_OK) {
            Serial.println("[TX_MGR] Added as ESP-NOW peer");
        } else {
            Serial.println("[TX_MGR] ERROR: Failed to add as ESP-NOW peer");
        }
    }
}

const uint8_t* TransmitterManager::getMAC() {
    return mac_known ? mac : nullptr;
}

bool TransmitterManager::isMACKnown() {
    return mac_known;
}

String TransmitterManager::getMACString() {
    if (!mac_known) return "Unknown";
    char str[18];
    snprintf(str, sizeof(str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(str);
}

void TransmitterManager::storeIPData(const uint8_t* transmitter_ip,
                                     const uint8_t* transmitter_gateway,
                                     const uint8_t* transmitter_subnet,
                                     bool is_static,
                                     uint32_t config_version) {
    if (!transmitter_ip || !transmitter_gateway || !transmitter_subnet) return;
    
    // Check if IP is all zeros (indicates Ethernet not connected on transmitter)
    bool is_zero_ip = (transmitter_ip[0] == 0 && transmitter_ip[1] == 0 && 
                       transmitter_ip[2] == 0 && transmitter_ip[3] == 0);
    
    if (is_zero_ip) {
        ip_known = false;
        Serial.println("[TX_MGR] Received empty IP data - transmitter Ethernet not connected yet");
        return;
    }
    
    // Store as current IP (legacy method - for backward compatibility)
    memcpy(current_ip, transmitter_ip, 4);
    memcpy(current_gateway, transmitter_gateway, 4);
    memcpy(current_subnet, transmitter_subnet, 4);
    ip_known = true;
    is_static_ip = is_static;
    network_config_version = config_version;
    
    Serial.printf("[TX_MGR] IP data: %s (%s), Gateway: %d.%d.%d.%d, Subnet: %d.%d.%d.%d, Version: %u\n",
                 getIPString().c_str(),
                 is_static_ip ? "Static" : "DHCP",
                 current_gateway[0], current_gateway[1], current_gateway[2], current_gateway[3],
                 current_subnet[0], current_subnet[1], current_subnet[2], current_subnet[3],
                 network_config_version);
    
    // Notify dashboard of cache update
    SSENotifier::notifyDataUpdated();

    // Persist to NVS (write-through)
    saveToNVS();
}

// Store complete network configuration (current + static)
void TransmitterManager::storeNetworkConfig(const uint8_t* curr_ip, 
                                           const uint8_t* curr_gateway,
                                           const uint8_t* curr_subnet,
                                           const uint8_t* stat_ip,
                                           const uint8_t* stat_gateway,
                                           const uint8_t* stat_subnet,
                                           const uint8_t* stat_dns1,
                                           const uint8_t* stat_dns2,
                                           bool is_static,
                                           uint32_t config_version) {
    if (!curr_ip || !curr_gateway || !curr_subnet) return;
    
    // Check if current IP is all zeros
    bool is_zero_ip = (curr_ip[0] == 0 && curr_ip[1] == 0 && 
                       curr_ip[2] == 0 && curr_ip[3] == 0);
    
    if (is_zero_ip) {
        ip_known = false;
        Serial.println("[TX_MGR] Received empty current IP - transmitter Ethernet not connected yet");
        return;
    }
    
    // Store current configuration
    memcpy(current_ip, curr_ip, 4);
    memcpy(current_gateway, curr_gateway, 4);
    memcpy(current_subnet, curr_subnet, 4);
    
    // Store static configuration
    if (stat_ip) memcpy(static_ip, stat_ip, 4);
    if (stat_gateway) memcpy(static_gateway, stat_gateway, 4);
    if (stat_subnet) memcpy(static_subnet, stat_subnet, 4);
    if (stat_dns1) memcpy(static_dns_primary, stat_dns1, 4);
    if (stat_dns2) memcpy(static_dns_secondary, stat_dns2, 4);
    
    ip_known = true;
    is_static_ip = is_static;
    network_config_version = config_version;
    
    Serial.printf("[TX_MGR] Network config stored:\n");
    Serial.printf("  Current: %d.%d.%d.%d (%s)\n",
                 current_ip[0], current_ip[1], current_ip[2], current_ip[3],
                 is_static_ip ? "Static" : "DHCP");
    Serial.printf("  Static saved: %d.%d.%d.%d / %d.%d.%d.%d / %d.%d.%d.%d\n",
                 static_ip[0], static_ip[1], static_ip[2], static_ip[3],
                 static_gateway[0], static_gateway[1], static_gateway[2], static_gateway[3],
                 static_subnet[0], static_subnet[1], static_subnet[2], static_subnet[3]);
    Serial.printf("  DNS: %d.%d.%d.%d / %d.%d.%d.%d, Version: %u\n",
                 static_dns_primary[0], static_dns_primary[1], static_dns_primary[2], static_dns_primary[3],
                 static_dns_secondary[0], static_dns_secondary[1], static_dns_secondary[2], static_dns_secondary[3],
                 network_config_version);
    
    // Notify dashboard of cache update
    SSENotifier::notifyDataUpdated();

    // Persist to NVS (write-through)
    saveToNVS();
}

// Current network configuration (active - could be DHCP or Static)
const uint8_t* TransmitterManager::getIP() {
    return ip_known ? current_ip : nullptr;
}

const uint8_t* TransmitterManager::getGateway() {
    return ip_known ? current_gateway : nullptr;
}

const uint8_t* TransmitterManager::getSubnet() {
    return ip_known ? current_subnet : nullptr;
}

// Saved static configuration (from transmitter NVS)
const uint8_t* TransmitterManager::getStaticIP() {
    return static_ip;
}

const uint8_t* TransmitterManager::getStaticGateway() {
    return static_gateway;
}

const uint8_t* TransmitterManager::getStaticSubnet() {
    return static_subnet;
}

const uint8_t* TransmitterManager::getStaticDNSPrimary() {
    return static_dns_primary;
}

const uint8_t* TransmitterManager::getStaticDNSSecondary() {
    return static_dns_secondary;
}

bool TransmitterManager::isIPKnown() {
    return ip_known;
}

bool TransmitterManager::isStaticIP() {
    return is_static_ip;
}

uint32_t TransmitterManager::getNetworkConfigVersion() {
    return network_config_version;
}

void TransmitterManager::updateNetworkMode(bool is_static, uint32_t version) {
    is_static_ip = is_static;
    network_config_version = version;
    Serial.printf("[TX_MGR] Network mode updated: %s (version %u)\n", 
                 is_static ? "Static" : "DHCP", version);
}

String TransmitterManager::getIPString() {
    if (!ip_known) return "0.0.0.0";
    char str[16];
    snprintf(str, sizeof(str), "%d.%d.%d.%d", current_ip[0], current_ip[1], current_ip[2], current_ip[3]);
    return String(str);
}

String TransmitterManager::getURL() {
    if (!ip_known) return "";
    return "http://" + getIPString();
}

// V2: Legacy version tracking functions removed

// Metadata management
void TransmitterManager::storeMetadata(bool valid, const char* env, const char* device,
                                       uint8_t major, uint8_t minor, uint8_t patch,
                                       const char* build_date_str) {
    metadata_received = true;
    metadata_valid = valid;
    
    if (env != nullptr) {
        strncpy(metadata_env, env, sizeof(metadata_env) - 1);
        metadata_env[sizeof(metadata_env) - 1] = '\0';
    }
    
    if (device != nullptr) {
        strncpy(metadata_device, device, sizeof(metadata_device) - 1);
        metadata_device[sizeof(metadata_device) - 1] = '\0';
    }
    
    metadata_major = major;
    metadata_minor = minor;
    metadata_patch = patch;
    metadata_version = (static_cast<uint32_t>(major) * 10000) +
                       (static_cast<uint32_t>(minor) * 100) +
                       static_cast<uint32_t>(patch);
    
    if (build_date_str != nullptr) {
        strncpy(metadata_build_date, build_date_str, sizeof(metadata_build_date) - 1);
        metadata_build_date[sizeof(metadata_build_date) - 1] = '\0';
    }
    
    char indicator = valid ? '@' : '*';
    Serial.printf("[TX_MGR] Metadata: %s %s v%d.%d.%d %c\n", 
                 metadata_device, metadata_env, major, minor, patch, indicator);
    if (build_date_str != nullptr && strlen(build_date_str) > 0) {
        Serial.printf("[TX_MGR]   Built: %s\n", metadata_build_date);
    }
    
    // Notify dashboard of cache update
    SSENotifier::notifyDataUpdated();

    // Persist to NVS (write-through)
    saveToNVS();
}

bool TransmitterManager::hasMetadata() {
    return metadata_received;
}

bool TransmitterManager::isMetadataValid() {
    return metadata_valid;
}

const char* TransmitterManager::getMetadataEnv() {
    return metadata_env;
}

const char* TransmitterManager::getMetadataDevice() {
    return metadata_device;
}

void TransmitterManager::getMetadataVersion(uint8_t& major, uint8_t& minor, uint8_t& patch) {
    major = metadata_major;
    minor = metadata_minor;
    patch = metadata_patch;
}

uint32_t TransmitterManager::getMetadataVersionNumber() {
    return metadata_version;
}

const char* TransmitterManager::getMetadataBuildDate() {
    return metadata_build_date;
}

// ═══════════════════════════════════════════════════════════════════════
// BATTERY SETTINGS MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════

void TransmitterManager::storeBatterySettings(const BatterySettings& settings) {
    battery_settings = settings;
    battery_settings_known = true;
    
    Serial.printf("[TX_MGR] Battery settings stored: %uWh, %uS, %umV-%umV\n",
                  settings.capacity_wh, settings.cell_count,
                  settings.min_voltage_mv, settings.max_voltage_mv);

    // Persist to NVS (write-through)
    saveToNVS();
}

BatterySettings TransmitterManager::getBatterySettings() {
    return battery_settings;
}

bool TransmitterManager::hasBatterySettings() {
    return battery_settings_known;
}

void TransmitterManager::storeBatteryEmulatorSettings(const BatteryEmulatorSettings& settings) {
    battery_emulator_settings = settings;
    battery_emulator_settings_known = true;
    saveToNVS();
}

BatteryEmulatorSettings TransmitterManager::getBatteryEmulatorSettings() {
    return battery_emulator_settings;
}

bool TransmitterManager::hasBatteryEmulatorSettings() {
    return battery_emulator_settings_known;
}

void TransmitterManager::storePowerSettings(const PowerSettings& settings) {
    power_settings = settings;
    power_settings_known = true;
    saveToNVS();
}

PowerSettings TransmitterManager::getPowerSettings() {
    return power_settings;
}

bool TransmitterManager::hasPowerSettings() {
    return power_settings_known;
}

void TransmitterManager::storeInverterSettings(const InverterSettings& settings) {
    inverter_settings = settings;
    inverter_settings_known = true;
    saveToNVS();
}

InverterSettings TransmitterManager::getInverterSettings() {
    return inverter_settings;
}

bool TransmitterManager::hasInverterSettings() {
    return inverter_settings_known;
}

void TransmitterManager::storeCanSettings(const CanSettings& settings) {
    can_settings = settings;
    can_settings_known = true;
    saveToNVS();
}

CanSettings TransmitterManager::getCanSettings() {
    return can_settings;
}

bool TransmitterManager::hasCanSettings() {
    return can_settings_known;
}

void TransmitterManager::storeContactorSettings(const ContactorSettings& settings) {
    contactor_settings = settings;
    contactor_settings_known = true;
    saveToNVS();
}

ContactorSettings TransmitterManager::getContactorSettings() {
    return contactor_settings;
}

bool TransmitterManager::hasContactorSettings() {
    return contactor_settings_known;
}

// ═══════════════════════════════════════════════════════════════════════
// MQTT CONFIGURATION MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════

void TransmitterManager::storeMqttConfig(bool enabled, const uint8_t* server, uint16_t port,
                                        const char* username, const char* password,
                                        const char* client_id, bool connected, uint32_t version) {
    mqtt_enabled = enabled;
    
    if (server) {
        memcpy(mqtt_server, server, 4);
    }
    
    mqtt_port = port;
    
    if (username) {
        strncpy(mqtt_username, username, sizeof(mqtt_username) - 1);
        mqtt_username[sizeof(mqtt_username) - 1] = '\0';
    }
    
    if (password) {
        strncpy(mqtt_password, password, sizeof(mqtt_password) - 1);
        mqtt_password[sizeof(mqtt_password) - 1] = '\0';
    }
    
    if (client_id) {
        strncpy(mqtt_client_id, client_id, sizeof(mqtt_client_id) - 1);
        mqtt_client_id[sizeof(mqtt_client_id) - 1] = '\0';
    }
    
    // NOTE: Do NOT update mqtt_connected here - it's runtime status managed by updateRuntimeStatus()
    // The 'connected' parameter in the config message is stale (from when config was saved)
    // Only version beacons have real-time connection status
    
    mqtt_config_version = version;
    mqtt_config_known = true;
    
    Serial.printf("[TX_MGR] MQTT config stored: %s, %d.%d.%d.%d:%d, v%u\n",
                  enabled ? "ENABLED" : "DISABLED",
                  server[0], server[1], server[2], server[3], port,
                  version);

    // Persist to NVS (write-through)
    saveToNVS();
}

bool TransmitterManager::isMqttEnabled() {
    return mqtt_enabled;
}

const uint8_t* TransmitterManager::getMqttServer() {
    return mqtt_config_known ? mqtt_server : nullptr;
}

uint16_t TransmitterManager::getMqttPort() {
    return mqtt_port;
}

const char* TransmitterManager::getMqttUsername() {
    return mqtt_username;
}

const char* TransmitterManager::getMqttPassword() {
    return mqtt_password;
}

const char* TransmitterManager::getMqttClientId() {
    return mqtt_client_id;
}

bool TransmitterManager::isMqttConnected() {
    return mqtt_connected;
}

bool TransmitterManager::isMqttConfigKnown() {
    return mqtt_config_known;
}

String TransmitterManager::getMqttServerString() {
    if (!mqtt_config_known) return "0.0.0.0";
    
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d",
             mqtt_server[0], mqtt_server[1], mqtt_server[2], mqtt_server[3]);
    return String(buffer);
}

// Phase 4: Version and runtime status tracking
uint32_t TransmitterManager::getMqttConfigVersion() {
    return mqtt_config_version;
}

void TransmitterManager::updateRuntimeStatus(bool mqtt_conn, bool eth_conn) {
    bool mqtt_changed = (mqtt_connected != mqtt_conn);
    bool eth_changed = (ethernet_connected != eth_conn);
    
    mqtt_connected = mqtt_conn;
    ethernet_connected = eth_conn;
    last_beacon_time_ms = millis();
    
    if (mqtt_changed || eth_changed) {
        Serial.printf("[TX_MGR] Runtime status updated: MQTT=%s, ETH=%s\n",
                     mqtt_conn ? "CONNECTED" : "DISCONNECTED",
                     eth_conn ? "CONNECTED" : "DISCONNECTED");
    }
}

bool TransmitterManager::isEthernetConnected() {
    return ethernet_connected;
}

unsigned long TransmitterManager::getLastBeaconTime() {
    return last_beacon_time_ms;
}

// Phase 4: Get transmitter time and uptime data
uint64_t TransmitterManager::getUptimeMs() {
    return uptime_ms;
}

uint64_t TransmitterManager::getUnixTime() {
    return unix_time;
}

uint8_t TransmitterManager::getTimeSource() {
    return time_source;
}

// Phase 4: Update time/uptime data from heartbeat
void TransmitterManager::updateTimeData(uint64_t new_uptime_ms, uint64_t new_unix_time, uint8_t new_time_source) {
    uptime_ms = new_uptime_ms;
    unix_time = new_unix_time;
    time_source = new_time_source;
}

void TransmitterManager::updateSendStatus(bool success) {
    last_espnow_send_success = success;
}

bool TransmitterManager::wasLastSendSuccessful() {
    return last_espnow_send_success;
}

bool TransmitterManager::isTransmitterConnected() {
    // Check ESP-NOW connection state machine for accurate status
    return EspNowConnectionManager::instance().is_connected() && mac_known;
}

// Phase 3: Static spec data storage (battery emulator specs via MQTT)
void TransmitterManager::storeStaticSpecs(const JsonObject& specs) {
    // Store combined specs from BE/spec_data topic
    DynamicJsonDocument doc(2048);
    doc.set(specs);
    
    static_specs_json_ = "";
    serializeJson(doc, static_specs_json_);
    
    // Extract individual spec sections if present
    if (specs.containsKey("battery")) {
        DynamicJsonDocument batteryDoc(512);
        batteryDoc.set(specs["battery"]);
        battery_specs_json_ = "";
        serializeJson(batteryDoc, battery_specs_json_);
    }
    
    if (specs.containsKey("inverter")) {
        DynamicJsonDocument inverterDoc(512);
        inverterDoc.set(specs["inverter"]);
        inverter_specs_json_ = "";
        serializeJson(inverterDoc, inverter_specs_json_);
    }
    
    if (specs.containsKey("charger")) {
        DynamicJsonDocument chargerDoc(512);
        chargerDoc.set(specs["charger"]);
        charger_specs_json_ = "";
        serializeJson(chargerDoc, charger_specs_json_);
    }
    
    if (specs.containsKey("system")) {
        DynamicJsonDocument systemDoc(512);
        systemDoc.set(specs["system"]);
        system_specs_json_ = "";
        serializeJson(systemDoc, system_specs_json_);
    }
    
    static_specs_known_ = true;
    Serial.println("[TX_MGR] Stored static specs from MQTT");
}

void TransmitterManager::storeBatterySpecs(const JsonObject& specs) {
    DynamicJsonDocument doc(512);
    doc.set(specs);
    battery_specs_json_ = "";
    serializeJson(doc, battery_specs_json_);
    
    // Parse and update battery_settings.cell_count from MQTT data
    if (specs.containsKey("number_of_cells")) {
        uint16_t new_cell_count = specs["number_of_cells"];
        if (new_cell_count > 0) {
            battery_settings.cell_count = new_cell_count;
            Serial.printf("[TX_MGR] Updated battery_settings.cell_count from MQTT: %u\n", new_cell_count);
        }
    }
    
    Serial.println("[TX_MGR] Stored battery specs from MQTT");
}

void TransmitterManager::storeInverterSpecs(const JsonObject& specs) {
    DynamicJsonDocument doc(512);
    doc.set(specs);
    inverter_specs_json_ = "";
    serializeJson(doc, inverter_specs_json_);
    Serial.println("[TX_MGR] Stored inverter specs from MQTT");
}

void TransmitterManager::storeChargerSpecs(const JsonObject& specs) {
    DynamicJsonDocument doc(512);
    doc.set(specs);
    charger_specs_json_ = "";
    serializeJson(doc, charger_specs_json_);
    Serial.println("[TX_MGR] Stored charger specs from MQTT");
}

void TransmitterManager::storeSystemSpecs(const JsonObject& specs) {
    DynamicJsonDocument doc(512);
    doc.set(specs);
    system_specs_json_ = "";
    serializeJson(doc, system_specs_json_);
    Serial.println("[TX_MGR] Stored system specs from MQTT");
}

bool TransmitterManager::hasStaticSpecs() {
    return static_specs_known_;
}

String TransmitterManager::getStaticSpecsJson() {
    return static_specs_json_;
}

String TransmitterManager::getBatterySpecsJson() {
    return battery_specs_json_;
}

String TransmitterManager::getInverterSpecsJson() {
    return inverter_specs_json_;
}

String TransmitterManager::getChargerSpecsJson() {
    return charger_specs_json_;
}

String TransmitterManager::getSystemSpecsJson() {
    return system_specs_json_;
}

void TransmitterManager::storeCellData(const JsonObject& cell_data) {
    if (!cell_data.containsKey("number_of_cells")) {
        Serial.println("[TX_MGR] Invalid cell data: missing number_of_cells");
        return;
    }
    
    uint16_t new_cell_count = cell_data["number_of_cells"];
    
    // Reallocate arrays if cell count changed
    if (new_cell_count != cell_count_ || cell_voltages_mV_ == nullptr) {
        if (cell_voltages_mV_) {
            free(cell_voltages_mV_);
            cell_voltages_mV_ = nullptr;
        }
        if (cell_balancing_status_) {
            free(cell_balancing_status_);
            cell_balancing_status_ = nullptr;
        }
        
        if (new_cell_count > 0) {
            cell_voltages_mV_ = (uint16_t*)malloc(new_cell_count * sizeof(uint16_t));
            cell_balancing_status_ = (bool*)malloc(new_cell_count * sizeof(bool));
            
            if (!cell_voltages_mV_ || !cell_balancing_status_) {
                Serial.println("[TX_MGR] Failed to allocate cell data arrays");
                if (cell_voltages_mV_) free(cell_voltages_mV_);
                if (cell_balancing_status_) free(cell_balancing_status_);
                cell_voltages_mV_ = nullptr;
                cell_balancing_status_ = nullptr;
                cell_data_known_ = false;
                return;
            }
        }
        
        cell_count_ = new_cell_count;
    }
    
    // Parse cell voltages
    if (cell_data.containsKey("cell_voltages_mV")) {
        JsonArray voltages = cell_data["cell_voltages_mV"];
        uint16_t count = min((uint16_t)voltages.size(), cell_count_);
        for (uint16_t i = 0; i < count; i++) {
            cell_voltages_mV_[i] = voltages[i];
        }
    }
    
    // Parse balancing status
    if (cell_data.containsKey("cell_balancing_status")) {
        JsonArray balancing = cell_data["cell_balancing_status"];
        uint16_t count = min((uint16_t)balancing.size(), cell_count_);
        for (uint16_t i = 0; i < count; i++) {
            cell_balancing_status_[i] = balancing[i];
        }
    }
    
    // Parse statistics
    if (cell_data.containsKey("cell_min_voltage_mV")) {
        cell_min_voltage_mV_ = cell_data["cell_min_voltage_mV"];
    }
    if (cell_data.containsKey("cell_max_voltage_mV")) {
        cell_max_voltage_mV_ = cell_data["cell_max_voltage_mV"];
    }
    if (cell_data.containsKey("balancing_active")) {
        balancing_active_ = cell_data["balancing_active"];
    }
    
    // Parse data_source field (dummy/live/live_simulated)
    // NOTE: Must extract immediately since cell_data (JsonObject) reference is temporary
    if (cell_data.containsKey("data_source")) {
        const char* source = cell_data["data_source"].as<const char*>();
        if (source) {
            strncpy(cell_data_source_, source, sizeof(cell_data_source_) - 1);
            cell_data_source_[sizeof(cell_data_source_) - 1] = '\0';
        } else {
            strncpy(cell_data_source_, "unknown", sizeof(cell_data_source_) - 1);
        }
    } else {
        // Default to unknown if not present
        strncpy(cell_data_source_, "unknown", sizeof(cell_data_source_) - 1);
    }
    
    cell_data_known_ = true;
    Serial.printf("[TX_MGR] Stored cell data: %d cells, min=%dmV, max=%dmV, source=%s\\n",
                  cell_count_, cell_min_voltage_mV_, cell_max_voltage_mV_, cell_data_source_);
}

void TransmitterManager::storeEventLogs(const JsonObject& logs) {
    event_logs_.clear();

    if (!logs.containsKey("events") || !logs["events"].is<JsonArray>()) {
        event_logs_known_ = false;
        Serial.println("[TX_MGR] Event logs missing 'events' array");
        return;
    }

    JsonArray events = logs["events"].as<JsonArray>();
    const size_t max_events = 200;

    for (JsonObject evt : events) {
        if (event_logs_.size() >= max_events) {
            break;
        }

        EventLogEntry entry = {};
        entry.timestamp = evt["timestamp"] | 0;
        entry.level = evt["level"] | 0;
        entry.data = evt["data"] | 0;

        const char* msg = evt["message"] | "";
        strncpy(entry.message, msg, sizeof(entry.message) - 1);
        entry.message[sizeof(entry.message) - 1] = '\0';

        event_logs_.push_back(entry);
    }

    event_logs_known_ = true;
    event_logs_last_update_ms_ = millis();
    Serial.printf("[TX_MGR] Stored %u event logs\n", (unsigned)event_logs_.size());
}

bool TransmitterManager::hasEventLogs() {
    return event_logs_known_ && !event_logs_.empty();
}

const std::vector<TransmitterManager::EventLogEntry>& TransmitterManager::getEventLogs() {
    return event_logs_;
}

uint32_t TransmitterManager::getEventLogCount() {
    return static_cast<uint32_t>(event_logs_.size());
}

uint32_t TransmitterManager::getEventLogsLastUpdateMs() {
    return event_logs_last_update_ms_;
}
