#ifndef MQTT_CONFIG_MANAGER_H
#define MQTT_CONFIG_MANAGER_H

#include <Arduino.h>
#include <IPAddress.h>
#include <Preferences.h>

/**
 * @brief Manages MQTT configuration storage and application
 * 
 * This class handles MQTT broker settings including:
 * - Enable/disable state
 * - Broker IP address (4-quad format)
 * - Port number
 * - Authentication credentials
 * - Client ID
 * 
 * Configuration is persisted in NVS and can be updated at runtime
 * with hot-reload capability (no device reboot required).
 */
class MqttConfigManager {
private:
    static bool enabled_;
    static IPAddress server_;
    static uint16_t port_;
    static char username_[32];
    static char password_[32];
    static char client_id_[32];
    static uint32_t config_version_;
    
    static constexpr const char* NVS_NAMESPACE = "mqtt_cfg";
    static constexpr const char* NVS_KEY_ENABLED = "enabled";
    static constexpr const char* NVS_KEY_SERVER = "server";
    static constexpr const char* NVS_KEY_PORT = "port";
    static constexpr const char* NVS_KEY_USERNAME = "username";
    static constexpr const char* NVS_KEY_PASSWORD = "password";
    static constexpr const char* NVS_KEY_CLIENT_ID = "client_id";
    static constexpr const char* NVS_KEY_VERSION = "version";
    
public:
    /**
     * @brief Load MQTT configuration from NVS
     * @return true if config loaded successfully, false if not found or error
     */
    static bool loadConfig();
    
    /**
     * @brief Save MQTT configuration to NVS
     * @param enabled MQTT enabled state
     * @param server MQTT broker IP address
     * @param port MQTT broker port
     * @param username Username (empty string for none)
     * @param password Password (empty string for none)
     * @param client_id Client ID
     * @return true if saved successfully, false on error
     */
    static bool saveConfig(bool enabled, const IPAddress& server, uint16_t port,
                          const char* username, const char* password, 
                          const char* client_id);
    
    /**
     * @brief Apply configuration to MQTT client with hot-reload
     * 
     * Disconnects existing MQTT client, applies new configuration,
     * and reconnects if enabled. No device reboot required.
     */
    static void applyConfig();
    
    /**
     * @brief Get MQTT enabled state
     * @return true if MQTT is enabled, false otherwise
     */
    static bool isEnabled() { return enabled_; }
    
    /**
     * @brief Get MQTT broker server address
     * @return IPAddress of MQTT broker
     */
    static IPAddress getServer() { return server_; }
    
    /**
     * @brief Get MQTT broker port
     * @return Port number
     */
    static uint16_t getPort() { return port_; }
    
    /**
     * @brief Get MQTT username
     * @return Username string (may be empty)
     */
    static const char* getUsername() { return username_; }
    
    /**
     * @brief Get MQTT password
     * @return Password string (may be empty)
     */
    static const char* getPassword() { return password_; }
    
    /**
     * @brief Get MQTT client ID
     * @return Client ID string
     */
    static const char* getClientId() { return client_id_; }
    
    /**
     * @brief Get configuration version number
     * @return Version number (increments on each save)
     */
    static uint32_t getConfigVersion() { return config_version_; }
    
    /**
     * @brief Check if MQTT client is currently connected
     * @return true if connected to broker, false otherwise
     */
    static bool isConnected();
};

#endif // MQTT_CONFIG_MANAGER_H
