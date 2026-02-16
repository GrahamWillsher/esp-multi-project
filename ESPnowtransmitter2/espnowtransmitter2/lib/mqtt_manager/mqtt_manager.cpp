#include "mqtt_manager.h"
#include "../../src/config/logging_config.h"

// Forward declaration for connection status query from src/network/mqtt_manager.cpp
// Uses C linkage to avoid name mangling issues
extern "C" bool mqtt_manager_is_connected();

// Initialize static members
bool MqttConfigManager::enabled_ = false;
IPAddress MqttConfigManager::server_(0, 0, 0, 0);
uint16_t MqttConfigManager::port_ = 1883;
char MqttConfigManager::username_[32] = "";
char MqttConfigManager::password_[32] = "";
char MqttConfigManager::client_id_[32] = "";
uint32_t MqttConfigManager::config_version_ = 0;

bool MqttConfigManager::loadConfig() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {  // Read-only
        LOG_ERROR("MQTT_MGR", "Failed to open NVS namespace");
        return false;
    }
    
    // Check if config exists
    if (!prefs.isKey(NVS_KEY_ENABLED)) {
        LOG_INFO("MQTT_MGR", "No config found in NVS");
        prefs.end();
        return false;
    }
    
    // Load configuration
    enabled_ = prefs.getBool(NVS_KEY_ENABLED, false);
    
    // Load server IP (stored as 4 bytes)
    size_t len = prefs.getBytes(NVS_KEY_SERVER, nullptr, 0);
    if (len == 4) {
        uint8_t ip[4];
        prefs.getBytes(NVS_KEY_SERVER, ip, 4);
        server_ = IPAddress(ip[0], ip[1], ip[2], ip[3]);
    } else {
        server_ = IPAddress(192, 168, 1, 221);  // Default
    }
    
    port_ = prefs.getUShort(NVS_KEY_PORT, 1883);
    
    prefs.getString(NVS_KEY_USERNAME, username_, sizeof(username_));
    prefs.getString(NVS_KEY_PASSWORD, password_, sizeof(password_));
    prefs.getString(NVS_KEY_CLIENT_ID, client_id_, sizeof(client_id_));
    
    config_version_ = prefs.getUInt(NVS_KEY_VERSION, 0);
    
    prefs.end();
    
    LOG_INFO("MQTT_MGR", "╔════════════════════════════════════════╗");
    LOG_INFO("MQTT_MGR", "║   MQTT Configuration Loaded from NVS  ║");
    LOG_INFO("MQTT_MGR", "╠════════════════════════════════════════╣");
    LOG_INFO("MQTT_MGR", "║ Enabled:    %-26s ║", enabled_ ? "YES" : "NO");
    LOG_INFO("MQTT_MGR", "║ Server:     %-26s ║", server_.toString().c_str());
    LOG_INFO("MQTT_MGR", "║ Port:       %-26d ║", port_);
    LOG_INFO("MQTT_MGR", "║ Username:   %-26s ║", strlen(username_) > 0 ? username_ : "(none)");
    LOG_INFO("MQTT_MGR", "║ Client ID:  %-26s ║", client_id_);
    LOG_INFO("MQTT_MGR", "║ Version:    %-26d ║", config_version_);
    LOG_INFO("MQTT_MGR", "╚════════════════════════════════════════╝");
    
    return true;
}

bool MqttConfigManager::saveConfig(bool enabled, const IPAddress& server, uint16_t port,
                             const char* username, const char* password, 
                             const char* client_id) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {  // Read-write
        LOG_ERROR("MQTT_MGR", "Failed to open NVS namespace for writing");
        return false;
    }
    
    // Increment version
    config_version_++;
    
    // Save configuration
    prefs.putBool(NVS_KEY_ENABLED, enabled);
    
    // Save server IP as 4 bytes
    uint8_t ip[4] = {server[0], server[1], server[2], server[3]};
    prefs.putBytes(NVS_KEY_SERVER, ip, 4);
    
    prefs.putUShort(NVS_KEY_PORT, port);
    prefs.putString(NVS_KEY_USERNAME, username);
    prefs.putString(NVS_KEY_PASSWORD, password);
    prefs.putString(NVS_KEY_CLIENT_ID, client_id);
    prefs.putUInt(NVS_KEY_VERSION, config_version_);
    
    prefs.end();
    
    // Update runtime configuration
    enabled_ = enabled;
    server_ = server;
    port_ = port;
    strncpy(username_, username, sizeof(username_) - 1);
    username_[sizeof(username_) - 1] = '\0';
    strncpy(password_, password, sizeof(password_) - 1);
    password_[sizeof(password_) - 1] = '\0';
    strncpy(client_id_, client_id, sizeof(client_id_) - 1);
    client_id_[sizeof(client_id_) - 1] = '\0';
    
    LOG_INFO("MQTT_MGR", "╔════════════════════════════════════════╗");
    LOG_INFO("MQTT_MGR", "║   MQTT Configuration Saved to NVS     ║");
    LOG_INFO("MQTT_MGR", "╠════════════════════════════════════════╣");
    LOG_INFO("MQTT_MGR", "║ Enabled:    %-26s ║", enabled_ ? "YES" : "NO");
    LOG_INFO("MQTT_MGR", "║ Server:     %-26s ║", server_.toString().c_str());
    LOG_INFO("MQTT_MGR", "║ Port:       %-26d ║", port_);
    LOG_INFO("MQTT_MGR", "║ Username:   %-26s ║", strlen(username_) > 0 ? username_ : "(none)");
    LOG_INFO("MQTT_MGR", "║ Client ID:  %-26s ║", client_id_);
    LOG_INFO("MQTT_MGR", "║ Version:    %-26d ║", config_version_);
    LOG_INFO("MQTT_MGR", "╚════════════════════════════════════════╝");
    
    return true;
}

void MqttConfigManager::applyConfig() {
    LOG_INFO("MQTT_CFG", "Configuration updated in NVS");
    LOG_INFO("MQTT_CFG", "  Enabled: %s", enabled_ ? "YES" : "NO");
    LOG_INFO("MQTT_CFG", "  Server: %s:%d", server_.toString().c_str(), port_);
    LOG_INFO("MQTT_CFG", "  Client ID: %s", client_id_);
    LOG_INFO("MQTT_CFG", "  Username: %s", strlen(username_) > 0 ? username_ : "(none)");
    
    // NOTE: Full hot-reload requires updating src/network/mqtt_manager.cpp to read from NVS
    // For now, configuration is saved to NVS and will be used on next device reboot
    // To implement true hot-reload:
    //   1. Update MqttManager::init() to read from MqttConfigManager instead of network_config.h
    //   2. Add MqttManager::reconfigure() method to disconnect and reconnect with new settings
    //   3. Call MqttManager::instance().reconfigure() here
    
    LOG_INFO("MQTT_CFG", "Configuration saved - reboot transmitter to apply");
}

bool MqttConfigManager::isConnected() {
    // Query the actual MQTT connection status from src/network/mqtt_manager.cpp
    // Uses extern C function declared at top of file
    return mqtt_manager_is_connected();
}
