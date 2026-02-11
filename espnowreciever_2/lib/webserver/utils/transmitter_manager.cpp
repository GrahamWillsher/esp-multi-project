#include "transmitter_manager.h"
#include "sse_notifier.h"
#include <esp_now.h>
#include <string.h>

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

// V2: Legacy version tracking removed
bool TransmitterManager::metadata_received = false;
bool TransmitterManager::metadata_valid = false;
char TransmitterManager::metadata_env[32] = {0};
char TransmitterManager::metadata_device[16] = {0};
uint8_t TransmitterManager::metadata_major = 0;
uint8_t TransmitterManager::metadata_minor = 0;
uint8_t TransmitterManager::metadata_patch = 0;
char TransmitterManager::metadata_build_date[48] = {0};

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
}

BatterySettings TransmitterManager::getBatterySettings() {
    return battery_settings;
}

bool TransmitterManager::hasBatterySettings() {
    return battery_settings_known;
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
