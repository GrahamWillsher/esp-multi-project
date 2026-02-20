#include "mqtt_client.h"
#include "../lib/webserver/utils/transmitter_manager.h"
#include "../lib/receiver_config/receiver_config_manager.h"
#include "../common.h"

/**
 * @brief FreeRTOS task for MQTT client
 * Connects to broker and processes incoming messages
 * 
 * Uses receiver's own MQTT configuration from ReceiverNetworkConfig.
 * The receiver subscribes to spec topics published by the transmitter.
 */
void task_mqtt_client(void* parameter) {
    LOG_INFO("MQTT_TASK", "Started");
    
    // Wait a moment for WiFi and config to be loaded
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    while (true) {
        // Check if receiver MQTT is enabled and configured
        if (ReceiverNetworkConfig::isMqttEnabled()) {
            const uint8_t* mqtt_server = ReceiverNetworkConfig::getMqttServer();
            
            // Validate server IP is configured (not 0.0.0.0)
            if (mqtt_server[0] != 0 || mqtt_server[1] != 0 || mqtt_server[2] != 0 || mqtt_server[3] != 0) {
                // Initialize client if not already done
                static bool initialized = false;
                if (!initialized) {
                    LOG_INFO("MQTT_TASK", "Initializing MQTT client");
                    MqttClient::init(
                        mqtt_server,
                        ReceiverNetworkConfig::getMqttPort(),
                        "espnow_receiver"
                    );
                    
                    const char* mqtt_username = ReceiverNetworkConfig::getMqttUsername();
                    if (mqtt_username && mqtt_username[0] != '\0') {
                        MqttClient::setAuth(
                            mqtt_username,
                            ReceiverNetworkConfig::getMqttPassword()
                        );
                    }
                    
                    MqttClient::setEnabled(true);
                    initialized = true;
                }
                
                // Process MQTT messages
                MqttClient::loop();
            } else {
                // Server not configured, disable client
                MqttClient::setEnabled(false);
            }
        } else {
            // MQTT not enabled, disable client
            MqttClient::setEnabled(false);
        }
        
        // Yield for other tasks
        vTaskDelay(pdMS_TO_TICKS(100));  // Process messages 10x per second
    }
}
