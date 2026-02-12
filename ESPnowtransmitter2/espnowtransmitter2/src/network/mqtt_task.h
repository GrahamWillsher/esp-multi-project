#pragma once

#include <Arduino.h>

/**
 * @brief FreeRTOS task wrapper for MQTT operations
 * 
 * Manages MQTT connection, reconnection, and periodic publishing
 * in a low-priority background task.
 */
void task_mqtt_loop(void* parameter);

/**
 * @brief MQTT Task wrapper for connection state tracking
 * 
 * Provides a singleton interface to query MQTT connection state.
 * Used by VersionBeaconManager to include runtime MQTT status in beacons.
 */
class MqttTask {
public:
    static MqttTask& instance();
    
    /**
     * @brief Check if MQTT is currently connected
     * @return true if connected to broker, false otherwise
     */
    bool is_connected() const;
    
private:
    MqttTask() = default;
};
