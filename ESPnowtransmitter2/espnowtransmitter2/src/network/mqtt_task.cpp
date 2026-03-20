#include "mqtt_task.h"
#include "mqtt_manager.h"
#include "transmission_selector.h"
#include "ethernet_manager.h"
#include "../config/task_config.h"
#include "../config/network_config.h"
#include "../config/logging_config.h"
#include "../espnow/message_handler.h"
#include "../espnow/version_beacon_manager.h"
#include "../datalayer/static_data.h"
#include "../system_settings.h"
#include <Arduino.h>
#include <espnow_transmitter.h>
#include <ethernet_utilities.h>
#include <mqtt_logger.h>
#include <esp32common/config/timing_config.h>

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
    
    // Initialize transmission selector with SMART mode (intelligent routing)
    TransmissionSelector::init(TransmissionSelector::TransmissionMode::SMART);
    
    // Wait for Ethernet to be ready
    while (!EthernetManager::instance().is_connected()) {
        LOG_DEBUG("MQTT", "MQTT waiting for Ethernet");
        vTaskDelay(pdMS_TO_TICKS(timing::MQTT_RECONNECT_INTERVAL_MS));
    }
    
    LOG_INFO("MQTT", "MQTT task active");
    
    unsigned long last_publish = 0;
    unsigned long last_cell_publish = 0;
    unsigned long last_event_publish = 0;
    unsigned long last_stats_log = 0;
    bool logger_initialized = false;
    bool was_connected = false;  // Track previous MQTT connection state
    
    while (true) {
        unsigned long now = millis();
        
        // Update MQTT state machine (handles connection/reconnection)
        MqttManager::instance().update();
        
        // Check if MQTT connection state changed
        bool is_connected_now = MqttManager::instance().is_connected();
        if (is_connected_now != was_connected) {
            // MQTT state changed - notify version beacon manager
            VersionBeaconManager::instance().notify_mqtt_connected(is_connected_now);
            MqttLogger::instance().set_mqtt_available(is_connected_now);
            
            if (is_connected_now) {
                // Just connected - initialize logger if needed
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
                
                // Update battery specs from datalayer (refresh number_of_cells after battery setup)
                LOG_INFO("MQTT", "Refreshing battery specs from datalayer...");
                StaticData::update_battery_specs(SystemSettings::instance().get_battery_profile_type());
                
                // Publish static configuration data (once on connect)
                LOG_INFO("MQTT", "Publishing static configuration...");
                if (MqttManager::instance().publish_static_specs()) {
                    LOG_INFO("MQTT", "✓ Static specs published to transmitter/BE/spec_data (SMART routing)");
                }
                if (MqttManager::instance().publish_inverter_specs()) {
                    LOG_INFO("MQTT", "✓ Inverter specs published to transmitter/BE/spec_data_2 (SMART routing)");
                }
                if (MqttManager::instance().publish_battery_specs()) {
                    LOG_INFO("MQTT", "✓ Battery specs published to transmitter/BE/battery_specs (SMART routing)");
                }
                if (MqttManager::instance().publish_battery_type_catalog()) {
                    LOG_INFO("MQTT", "✓ Battery type catalog published to transmitter/BE/battery_type_catalog");
                }
                if (MqttManager::instance().publish_inverter_type_catalog()) {
                    LOG_INFO("MQTT", "✓ Inverter type catalog published to transmitter/BE/inverter_type_catalog");
                }
            }
            
            was_connected = is_connected_now;
        }
        
        // Log statistics periodically
        if (now - last_stats_log > timing::MQTT_STATS_LOG_INTERVAL_MS) {
            auto stats = MqttManager::instance().get_statistics();
            LOG_INFO("MQTT_STATS", "Connections: %lu, Failed: %lu, Published: %lu, Uptime: %lu s, State: %d",
                    stats.total_connections,
                    stats.failed_connections,
                    stats.total_messages_published,
                    stats.uptime_ms / 1000,
                    (int)stats.current_state);
            last_stats_log = now;
        }
        
        // Publish data periodically when connected
        if (is_connected_now && (now - last_publish > timing::MQTT_PUBLISH_INTERVAL_MS)) {
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
        
        // Publish cell data periodically (less frequent - every 1 second) when connected
        if (is_connected_now && (now - last_cell_publish > timing::MQTT_CELL_PUBLISH_INTERVAL_MS)) {
            last_cell_publish = now;
            
            // Publish cell voltages and balancing status via MQTT
            if (MqttManager::instance().publish_cell_data()) {
                LOG_DEBUG("MQTT", "✓ Cell data published to transmitter/BE/cell_data");
            } else {
                LOG_DEBUG("MQTT", "Cell data publish skipped/failed");
            }
        }

        // Publish event logs periodically (every 5 seconds, only when subscribed) when connected
        if (is_connected_now && 
            MqttManager::instance().get_event_log_subscribers() > 0 &&
            (now - last_event_publish > timing::MQTT_EVENT_PUBLISH_INTERVAL_MS)) {
            last_event_publish = now;
            MqttManager::instance().publish_event_logs();
        }
        
        // Update task every second (state machine runs inside update())
        vTaskDelay(pdMS_TO_TICKS(TimingConfig::MQTT_LOOP_DELAY_MS));
    }
}
