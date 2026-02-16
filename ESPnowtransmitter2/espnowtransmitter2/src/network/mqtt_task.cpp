#include "mqtt_task.h"
#include "mqtt_manager.h"
#include "ethernet_manager.h"
#include "../config/task_config.h"
#include "../config/network_config.h"
#include "../config/logging_config.h"
#include "../espnow/message_handler.h"
#include "../espnow/version_beacon_manager.h"
#include <Arduino.h>
#include <espnow_transmitter.h>
#include <ethernet_utilities.h>
#include <mqtt_logger.h>

// MqttTask singleton implementation
MqttTask& MqttTask::instance() {
    static MqttTask instance;
    return instance;
}

bool MqttTask::is_connected() const {
    return MqttManager::instance().is_connected();
}

void task_mqtt_loop(void* parameter) {
    LOG_DEBUG("MQTT", "MQTT task started");
    
    // Wait for Ethernet to be ready
    while (!EthernetManager::instance().is_connected()) {
        LOG_DEBUG("MQTT", "MQTT waiting for Ethernet");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    LOG_INFO("MQTT", "MQTT task active");
    
    unsigned long last_reconnect_attempt = 0;
    unsigned long last_publish = 0;
    bool logger_initialized = false;
    bool was_connected = false;  // Track previous MQTT connection state
    
    while (true) {
        unsigned long now = millis();
        
        // Check if MQTT connection state changed
        bool is_connected_now = MqttManager::instance().is_connected();
        if (is_connected_now != was_connected) {
            // MQTT state changed - notify version beacon manager
            VersionBeaconManager::instance().notify_mqtt_connected(is_connected_now);
            was_connected = is_connected_now;
        }
        
        // Handle MQTT connection
        if (!is_connected_now) {
            if (config::features::MQTT_ENABLED && 
                EthernetManager::instance().is_connected() && 
                (now - last_reconnect_attempt > timing::MQTT_RECONNECT_INTERVAL_MS)) {
                last_reconnect_attempt = now;
                if (MqttManager::instance().connect()) {
                    // Initialize MQTT logger on first successful connection
                    if (!logger_initialized) {
                        uint8_t saved_level = EspnowMessageHandler::instance().load_debug_level();
                        MqttLogger::instance().init(MqttManager::instance().get_client(), "espnow/transmitter");
                        MqttLogger::instance().set_level((MqttLogLevel)saved_level);
                        logger_initialized = true;
                        
                        LOG_INFO("MQTT", "MQTT logger initialized, level: %s", 
                                 MqttLogger::instance().level_to_string((MqttLogLevel)saved_level));
                    }
                    
                    MQTT_LOG_NOTICE("MQTT", "MQTT broker connected successfully");
                    MQTT_LOG_INFO("SYSTEM", "ESP-NOW Transmitter online, uptime: %lu ms", millis());
                    MQTT_LOG_INFO("ETH", "IP: %s, Gateway: %s", 
                                  EthernetManager::instance().get_local_ip().toString().c_str(),
                                  EthernetManager::instance().get_gateway_ip().toString().c_str());
                    
                    // Flush any buffered messages (if reconnecting)
                    MqttLogger::instance().flush_buffer();
                }
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
                
                // Test MQTT logger with periodic message
                MQTT_LOG_INFO("TELEMETRY", "Data published: SOC=%d%%, Power=%ldW", tx_data.soc, tx_data.power);
            }
        }
        
        // Low priority task - run infrequently
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
