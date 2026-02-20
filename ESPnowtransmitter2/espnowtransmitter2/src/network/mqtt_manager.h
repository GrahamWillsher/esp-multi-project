#pragma once
#include <PubSubClient.h>
#include <WiFiClient.h>

/**
 * @brief Manages MQTT connectivity and publishing for telemetry
 * 
 * Singleton class that handles MQTT broker connection, automatic reconnection,
 * and publishing of battery data and status messages.
 */
class MqttManager {
public:
    static MqttManager& instance();
    
    /**
     * @brief Initialize MQTT client with broker configuration
     */
    void init();
    
    /**
     * @brief Attempt connection to MQTT broker
     * @return true if connected successfully, false otherwise
     */
    bool connect();
    
    /**
     * @brief Check if currently connected to MQTT broker
     * @return true if connected, false otherwise
     */
    bool is_connected() const { return connected_; }
    
    /**
     * @brief Publish battery data as JSON
     * @param soc Battery state of charge (0-100%)
     * @param power Power in watts (positive = charging, negative = discharging)
     * @param timestamp Formatted timestamp string
     * @param eth_connected Ethernet connection status
     * @return true if published successfully, false otherwise
     */
    bool publish_data(int soc, long power, const char* timestamp, bool eth_connected);
    
    /**
     * @brief Publish status message
     * @param message Status message to publish
     * @param retained Whether message should be retained by broker
     * @return true if published successfully, false otherwise
     */
    bool publish_status(const char* message, bool retained = false);
    
    /**
     * @brief Publish static configuration data (BE/spec_data topic)
     * @return true if published successfully, false otherwise
     */
    bool publish_static_specs();
    
    /**
     * @brief Publish battery specifications (BE/spec_data topic)
     * @return true if published successfully, false otherwise
     */
    bool publish_battery_specs();
    
    /**
     * @brief Publish inverter specifications (BE/spec_data_2 topic)
     * @return true if published successfully, false otherwise
     */
    bool publish_inverter_specs();
    
    /**
     * @brief Publish cell voltages and balancing status (BE/cell_data topic)
     * @return true if published successfully, false otherwise
     */
    bool publish_cell_data();
    
    /**
     * @brief Process MQTT messages (must be called regularly from task)
     */
    void loop();
    
    /**
     * @brief Get pointer to MQTT client for logger integration
     * @return Pointer to PubSubClient instance
     */
    PubSubClient* get_client() { return &client_; }
    
private:
    MqttManager();
    ~MqttManager() = default;
    
    // Prevent copying
    MqttManager(const MqttManager&) = delete;
    MqttManager& operator=(const MqttManager&) = delete;
    
    /**
     * @brief MQTT message callback for subscribed topics
     * @param topic Topic on which message was received
     * @param payload Message payload
     * @param length Payload length in bytes
     */
    static void message_callback(char* topic, byte* payload, unsigned int length);
    
    /**
     * @brief Handle OTA update command via MQTT
     * @param url Firmware URL to download from
     */
    void handle_ota_command(const char* url);
    
    WiFiClient eth_client_;
    PubSubClient client_;
    volatile bool connected_{false};
    char payload_buffer_[384];
};
