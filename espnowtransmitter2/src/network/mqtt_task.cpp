#include "mqtt_task.h"
#include "mqtt_manager.h"
#include "ethernet_manager.h"
#include "../config/task_config.h"
#include "../config/network_config.h"
#include "../config/logging_config.h"
#include <Arduino.h>
#include <espnow_transmitter.h>
#include <ethernet_utilities.h>

void task_mqtt_loop(void* parameter) {
    LOG_DEBUG("MQTT task started");
    
    // Wait for Ethernet to be ready
    while (!EthernetManager::instance().is_connected()) {
        LOG_DEBUG("MQTT waiting for Ethernet");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    LOG_INFO("MQTT task active");
    
    unsigned long last_reconnect_attempt = 0;
    unsigned long last_publish = 0;
    
    while (true) {
        unsigned long now = millis();
        
        // Handle MQTT connection
        if (!MqttManager::instance().is_connected()) {
            if (config::features::MQTT_ENABLED && 
                EthernetManager::instance().is_connected() && 
                (now - last_reconnect_attempt > timing::MQTT_RECONNECT_INTERVAL_MS)) {
                last_reconnect_attempt = now;
                MqttManager::instance().connect();
            }
        } else {
            // Process MQTT messages
            MqttManager::instance().loop();
            
            // Publish data periodically
            if (config::features::MQTT_ENABLED && 
                (now - last_publish > timing::MQTT_PUBLISH_INTERVAL_MS)) {
                last_publish = now;
                
                // Get formatted timestamp
                char timestamp_str[64];
                get_formatted_time(timestamp_str, sizeof(timestamp_str));
                
                // Publish current data
                MqttManager::instance().publish_data(
                    tx_data.soc,
                    tx_data.power,
                    timestamp_str,
                    EthernetManager::instance().is_connected()
                );
            }
        }
        
        // Low priority task - run infrequently
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
