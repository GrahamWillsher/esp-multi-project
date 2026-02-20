#include "receiver_config_manager.h"
#include <Arduino.h>

// Initialize static members
char ReceiverNetworkConfig::hostname_[32] = {0};
char ReceiverNetworkConfig::ssid_[32] = {0};
char ReceiverNetworkConfig::password_[64] = {0};
bool ReceiverNetworkConfig::use_static_ip_ = false;
uint8_t ReceiverNetworkConfig::static_ip_[4] = {0};
uint8_t ReceiverNetworkConfig::gateway_[4] = {0};
uint8_t ReceiverNetworkConfig::subnet_[4] = {0};
uint8_t ReceiverNetworkConfig::dns_primary_[4] = {0};
uint8_t ReceiverNetworkConfig::dns_secondary_[4] = {0};
bool ReceiverNetworkConfig::mqtt_enabled_ = false;
uint8_t ReceiverNetworkConfig::mqtt_server_[4] = {0};
uint16_t ReceiverNetworkConfig::mqtt_port_ = 1883;
char ReceiverNetworkConfig::mqtt_username_[32] = {0};
char ReceiverNetworkConfig::mqtt_password_[64] = {0};
uint8_t ReceiverNetworkConfig::battery_type_ = 29;      // Default to PYLON_BATTERY
uint8_t ReceiverNetworkConfig::inverter_type_ = 0;      // Default to NONE
bool ReceiverNetworkConfig::simulation_mode_ = true;    // Default to simulated data

bool ReceiverNetworkConfig::loadConfig() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {  // true = read-only
        Serial.println("[ReceiverConfig] Failed to open NVS namespace");
        return false;
    }
    
    // Load SSID first - if not present, no valid config exists
    size_t ssid_len = prefs.getString(NVS_KEY_SSID, ssid_, sizeof(ssid_));
    if (ssid_len == 0 || ssid_[0] == '\0') {
        Serial.println("[ReceiverConfig] No SSID found - AP mode required");
        prefs.end();
        return false;
    }
    
    // Load hostname (default to "esp32-receiver" if not set)
    size_t hostname_len = prefs.getString(NVS_KEY_HOSTNAME, hostname_, sizeof(hostname_));
    if (hostname_len == 0 || hostname_[0] == '\0') {
        strcpy(hostname_, "esp32-receiver");
    }
    
    // Load password
    prefs.getString(NVS_KEY_PASSWORD, password_, sizeof(password_));
    
    // Load network mode
    use_static_ip_ = prefs.getBool(NVS_KEY_USE_STATIC, false);
    
    // Always load static IP configuration (even if DHCP is enabled)
    // This allows users to switch between modes without re-entering IP addresses
    size_t ip_size = prefs.getBytes(NVS_KEY_IP, static_ip_, 4);
    size_t gw_size = prefs.getBytes(NVS_KEY_GATEWAY, gateway_, 4);
    size_t sn_size = prefs.getBytes(NVS_KEY_SUBNET, subnet_, 4);
    
    // Validate static IP configuration if it's enabled
    if (use_static_ip_ && (ip_size != 4 || gw_size != 4 || sn_size != 4)) {
        Serial.println("[ReceiverConfig] Incomplete static IP config - falling back to DHCP");
        use_static_ip_ = false;
    }
    
    // Load DNS servers (optional)
    prefs.getBytes(NVS_KEY_DNS_PRIMARY, dns_primary_, 4);
    prefs.getBytes(NVS_KEY_DNS_SECONDARY, dns_secondary_, 4);
    
    // Load MQTT configuration (optional)
    mqtt_enabled_ = prefs.getBool(NVS_KEY_MQTT_ENABLED, false);
    prefs.getBytes(NVS_KEY_MQTT_SERVER, mqtt_server_, 4);
    mqtt_port_ = prefs.getUShort(NVS_KEY_MQTT_PORT, 1883);
    prefs.getString(NVS_KEY_MQTT_USERNAME, mqtt_username_, sizeof(mqtt_username_));
    prefs.getString(NVS_KEY_MQTT_PASSWORD, mqtt_password_, sizeof(mqtt_password_));
    
    // Load Battery and Inverter type selection (with defaults)
    battery_type_ = prefs.getUChar(NVS_KEY_BATTERY_TYPE, 29);    // 29 = PYLON_BATTERY
    inverter_type_ = prefs.getUChar(NVS_KEY_INVERTER_TYPE, 0);   // 0 = NONE

    // Load simulation mode (default ON)
    simulation_mode_ = prefs.getBool(NVS_KEY_SIMULATION_MODE, true);
    
    prefs.end();
    
    Serial.println("[ReceiverConfig] Configuration loaded successfully from NVS");
    Serial.printf("  Hostname: %s\n", hostname_);
    Serial.printf("  SSID: %s\n", ssid_);
    Serial.printf("  Password length: %d\n", strlen(password_));
    Serial.printf("  Mode: %s\n", use_static_ip_ ? "Static IP" : "DHCP");
    if (use_static_ip_) {
        Serial.printf("  IP: %d.%d.%d.%d\n", static_ip_[0], static_ip_[1], static_ip_[2], static_ip_[3]);
        Serial.printf("  Gateway: %d.%d.%d.%d\n", gateway_[0], gateway_[1], gateway_[2], gateway_[3]);
        Serial.printf("  Subnet: %d.%d.%d.%d\n", subnet_[0], subnet_[1], subnet_[2], subnet_[3]);
    }
    Serial.println("[ReceiverConfig] NVS read complete");
    
    return true;
}

bool ReceiverNetworkConfig::saveConfig(
    const char* hostname,
    const char* ssid,
    const char* password,
    bool use_static_ip,
    const uint8_t static_ip[4],
    const uint8_t gateway[4],
    const uint8_t subnet[4],
    const uint8_t dns_primary[4],
    const uint8_t dns_secondary[4],
    bool mqtt_enabled,
    const uint8_t mqtt_server[4],
    uint16_t mqtt_port,
    const char* mqtt_username,
    const char* mqtt_password
) {
    // Validation
    if (!ssid || ssid[0] == '\0') {
        Serial.println("[ReceiverConfig] SSID is required");
        return false;
    }
    
    if (strlen(ssid) >= sizeof(ssid_)) {
        Serial.println("[ReceiverConfig] SSID too long");
        return false;
    }
    
    if (password && strlen(password) > 0 && strlen(password) < 8) {
        Serial.println("[ReceiverConfig] Password must be at least 8 characters for WPA2");
        return false;
    }
    
    if (password && strlen(password) >= sizeof(password_)) {
        Serial.println("[ReceiverConfig] Password too long");
        return false;
    }
    
    if (use_static_ip && (!static_ip || !gateway || !subnet)) {
        Serial.println("[ReceiverConfig] Static IP mode requires IP, gateway, and subnet");
        return false;
    }
    
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {  // false = read-write
        Serial.println("[ReceiverConfig] Failed to open NVS namespace for writing");
        return false;
    }
    
    // Save hostname
    if (hostname && hostname[0] != '\0') {
        prefs.putString(NVS_KEY_HOSTNAME, hostname);
        strcpy(hostname_, hostname);
    } else {
        prefs.putString(NVS_KEY_HOSTNAME, "esp32-receiver");
        strcpy(hostname_, "esp32-receiver");
    }
    
    // Save SSID
    prefs.putString(NVS_KEY_SSID, ssid);
    strcpy(ssid_, ssid);
    
    // Save password ONLY if a new password is provided
    // If password is empty, keep the existing password in NVS (don't overwrite it)
    if (password && password[0] != '\0') {
        Serial.printf("[ReceiverConfig] Updating password (length: %d)\n", strlen(password));
        prefs.putString(NVS_KEY_PASSWORD, password);
        strcpy(password_, password);
    } else {
        Serial.println("[ReceiverConfig] No password provided - keeping existing password");
        // Load existing password from NVS into memory
        prefs.getString(NVS_KEY_PASSWORD, password_, sizeof(password_));
    }
    
    // Save network mode
    prefs.putBool(NVS_KEY_USE_STATIC, use_static_ip);
    use_static_ip_ = use_static_ip;
    
    // Always save static IP configuration (even if DHCP is currently enabled)
    // This allows users to switch between modes without re-entering IP addresses
    if (static_ip && gateway && subnet) {
        prefs.putBytes(NVS_KEY_IP, static_ip, 4);
        prefs.putBytes(NVS_KEY_GATEWAY, gateway, 4);
        prefs.putBytes(NVS_KEY_SUBNET, subnet, 4);
        memcpy(static_ip_, static_ip, 4);
        memcpy(gateway_, gateway, 4);
        memcpy(subnet_, subnet, 4);
    }
    
    // Save DNS servers (optional)
    if (dns_primary) {
        prefs.putBytes(NVS_KEY_DNS_PRIMARY, dns_primary, 4);
        memcpy(dns_primary_, dns_primary, 4);
    }
    if (dns_secondary) {
        prefs.putBytes(NVS_KEY_DNS_SECONDARY, dns_secondary, 4);
        memcpy(dns_secondary_, dns_secondary, 4);
    }
    
    // Save MQTT configuration
    prefs.putBool(NVS_KEY_MQTT_ENABLED, mqtt_enabled);
    mqtt_enabled_ = mqtt_enabled;
    
    if (mqtt_server) {
        prefs.putBytes(NVS_KEY_MQTT_SERVER, mqtt_server, 4);
        memcpy(mqtt_server_, mqtt_server, 4);
    }
    
    prefs.putUShort(NVS_KEY_MQTT_PORT, mqtt_port);
    mqtt_port_ = mqtt_port;
    
    if (mqtt_username && mqtt_username[0] != '\0') {
        prefs.putString(NVS_KEY_MQTT_USERNAME, mqtt_username);
        strncpy(mqtt_username_, mqtt_username, sizeof(mqtt_username_) - 1);
        mqtt_username_[sizeof(mqtt_username_) - 1] = '\0';
    }
    
    if (mqtt_password && mqtt_password[0] != '\0' && strcmp(mqtt_password, "********") != 0) {
        // Only save if password is provided and not the placeholder
        prefs.putString(NVS_KEY_MQTT_PASSWORD, mqtt_password);
        strncpy(mqtt_password_, mqtt_password, sizeof(mqtt_password_) - 1);
        mqtt_password_[sizeof(mqtt_password_) - 1] = '\0';
    } else if (strcmp(mqtt_password, "********") == 0) {
        // Keep existing password
        prefs.getString(NVS_KEY_MQTT_PASSWORD, mqtt_password_, sizeof(mqtt_password_));
    }
    
    prefs.end();
    
    Serial.println("[ReceiverConfig] Configuration saved successfully to NVS");
    Serial.printf("  Hostname: %s\n", hostname_);
    Serial.printf("  SSID: %s\n", ssid_);
    Serial.printf("  Password: %s\n", password_[0] ? "(set)" : "(empty)");
    Serial.printf("  Mode: %s\n", use_static_ip_ ? "Static IP" : "DHCP");
    if (use_static_ip_) {
        Serial.printf("  IP: %d.%d.%d.%d\n", static_ip_[0], static_ip_[1], static_ip_[2], static_ip_[3]);
        Serial.printf("  Gateway: %d.%d.%d.%d\n", gateway_[0], gateway_[1], gateway_[2], gateway_[3]);
        Serial.printf("  Subnet: %d.%d.%d.%d\n", subnet_[0], subnet_[1], subnet_[2], subnet_[3]);
    }
    Serial.println("[ReceiverConfig] NVS write complete");
    
    return true;
}

bool ReceiverNetworkConfig::hasValidConfig() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        return false;
    }
    
    char temp_ssid[32];
    size_t ssid_len = prefs.getString(NVS_KEY_SSID, temp_ssid, sizeof(temp_ssid));
    prefs.end();
    
    return (ssid_len > 0 && temp_ssid[0] != '\0');
}

void ReceiverNetworkConfig::clearConfig() {
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.clear();
        prefs.end();
        Serial.println("[ReceiverConfig] Configuration cleared - factory reset complete");
    }
    
    // Clear in-memory state
    hostname_[0] = '\0';
    ssid_[0] = '\0';
    password_[0] = '\0';
    use_static_ip_ = false;
    memset(static_ip_, 0, 4);
    memset(gateway_, 0, 4);
    memset(subnet_, 0, 4);
    memset(dns_primary_, 0, 4);
    memset(dns_secondary_, 0, 4);
}

void ReceiverNetworkConfig::setBatteryType(uint8_t type) {
    battery_type_ = type;
    
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putUChar(NVS_KEY_BATTERY_TYPE, type);
        prefs.end();
        Serial.printf("[ReceiverConfig] Battery type saved: %d\n", type);
    } else {
        Serial.println("[ReceiverConfig] Failed to save battery type to NVS");
    }
}

void ReceiverNetworkConfig::setInverterType(uint8_t type) {
    inverter_type_ = type;
    
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putUChar(NVS_KEY_INVERTER_TYPE, type);
        prefs.end();
        Serial.printf("[ReceiverConfig] Inverter type saved: %d\n", type);
    } else {
        Serial.println("[ReceiverConfig] Failed to save inverter type to NVS");
    }
}

void ReceiverNetworkConfig::setSimulationMode(bool enabled) {
    simulation_mode_ = enabled;

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putBool(NVS_KEY_SIMULATION_MODE, enabled);
        prefs.end();
        Serial.printf("[ReceiverConfig] Simulation mode saved: %s\n", enabled ? "ON" : "OFF");
    } else {
        Serial.println("[ReceiverConfig] Failed to save simulation mode to NVS");
    }
}
