#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <Preferences.h>

/**
 * @brief Manages MQTT configuration storage for transmitter command updates.
 *
 * This class stores MQTT broker settings in NVS and exposes the current
 * runtime snapshot used by config ACK handlers.
 *
 * Note: runtime reconfiguration of the active MQTT client is not implemented.
 * Saved values are applied on next transmitter boot when network/MQTT initializes.
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
     * @brief Load MQTT configuration from NVS.
     * @return true if config loaded successfully, false if not found or on error.
     */
    static bool loadConfig();

    /**
     * @brief Save MQTT configuration to NVS.
     * @param enabled MQTT enabled state.
     * @param server MQTT broker IP address.
     * @param port MQTT broker port.
     * @param username Username (empty string for none).
     * @param password Password (empty string for none).
     * @param client_id Client ID.
     * @return true if saved successfully, false on error.
     */
    static bool saveConfig(bool enabled,
                           const IPAddress& server,
                           uint16_t port,
                           const char* username,
                           const char* password,
                           const char* client_id);

    /**
     * @brief Apply configuration update semantics.
     *
     * Current behavior is reboot-required apply: logs effective values and
     * confirms persistence, but does not live-reconfigure an existing MQTT socket.
     */
    static void applyConfig();

    static bool isEnabled() { return enabled_; }
    static IPAddress getServer() { return server_; }
    static uint16_t getPort() { return port_; }
    static const char* getUsername() { return username_; }
    static const char* getPassword() { return password_; }
    static const char* getClientId() { return client_id_; }
    static uint32_t getConfigVersion() { return config_version_; }

    /**
     * @brief Check if MQTT client is currently connected.
     * @return true if connected to broker, false otherwise.
     */
    static bool isConnected();
};
