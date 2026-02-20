#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

/**
 * @brief MQTT client for subscribing to battery emulator static specs
 * 
 * This module subscribes to topics published by the transmitter's battery emulator:
 * - BE/spec_data: Combined battery/inverter/charger/system specs
 * - BE/spec_data_2: Inverter-specific specs (alternative topic)
 * - BE/battery_specs: Battery-only specs
 * 
 * Received data is stored in TransmitterManager for web UI access.
 */
class MqttClient {
public:
    /**
     * @brief Initialize MQTT client with broker configuration
     * @param mqtt_server MQTT broker IP address
     * @param mqtt_port MQTT broker port (default 1883)
     * @param client_id MQTT client identifier (default "espnow_receiver")
     */
    static void init(const uint8_t* mqtt_server, uint16_t mqtt_port = 1883, 
                    const char* client_id = "espnow_receiver");
    
    /**
     * @brief Set authentication credentials
     * @param username MQTT username (nullptr for no auth)
     * @param password MQTT password (nullptr for no auth)
     */
    static void setAuth(const char* username, const char* password);
    
    /**
     * @brief Connect to MQTT broker and subscribe to topics
     * @return true if connection successful
     */
    static bool connect();
    
    /**
     * @brief Disconnect from MQTT broker
     */
    static void disconnect();
    
    /**
     * @brief Check if connected to MQTT broker
     * @return true if connected
     */
    static bool isConnected();
    
    /**
     * @brief Process incoming MQTT messages (call in loop)
     */
    static void loop();
    
    /**
     * @brief Enable/disable MQTT client
     * @param enabled true to enable, false to disable
     */
    static void setEnabled(bool enabled);
    
    /**
     * @brief Check if MQTT client is enabled
     * @return true if enabled
     */
    static bool isEnabled();

private:
    static WiFiClient wifi_client_;
    static PubSubClient mqtt_client_;
    static char client_id_[32];
    static char username_[32];
    static char password_[32];
    static uint8_t broker_ip_[4];
    static uint16_t broker_port_;
    static bool enabled_;
    static unsigned long last_connect_attempt_;
    static const unsigned long RECONNECT_INTERVAL_MS = 5000;
    
    /**
     * @brief MQTT message callback
     */
    static void messageCallback(char* topic, uint8_t* payload, unsigned int length);
    
    /**
     * @brief Subscribe to all required topics
     */
    static void subscribeToTopics();
    
    /**
     * @brief Handle incoming spec_data message
     */
    static void handleSpecData(const char* json_payload, size_t length);
    
    /**
     * @brief Handle incoming spec_data_2 message
     */
    static void handleSpecData2(const char* json_payload, size_t length);
    
    /**
     * @brief Handle incoming battery_specs message
     */
    static void handleBatterySpecs(const char* json_payload, size_t length);
    
    /**
     * @brief Handle incoming cell_data message
     */
    static void handleCellData(const char* json_payload, size_t length);
};

#endif // MQTT_CLIENT_H
