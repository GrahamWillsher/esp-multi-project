#pragma once
#include <freertos/FreeRTOS.h>

namespace task_config {
    // Task stack sizes (bytes)
    constexpr size_t STACK_SIZE_ESPNOW_RX = 4096;      // ESP-NOW message handling
    constexpr size_t STACK_SIZE_DATA_SENDER = 4096;    // Test data generation
    constexpr size_t STACK_SIZE_ANNOUNCEMENT = 4096;   // Periodic announcements (increased for settings notifications)
    constexpr size_t STACK_SIZE_MQTT = 4096;           // MQTT client
    constexpr size_t STACK_SIZE_NETWORK_CONFIG = 4096; // Network configuration processing
    
    // Task priorities (higher number = higher priority)
    constexpr UBaseType_t PRIORITY_CRITICAL = 5;       // Control loop, safety functions
    constexpr UBaseType_t PRIORITY_ESPNOW = 4;         // ESP-NOW RX (must not block control)
    constexpr UBaseType_t PRIORITY_NETWORK_CONFIG = 3; // Network config (medium - heavy operations)
    constexpr UBaseType_t PRIORITY_NORMAL = 2;         // Data transmission
    constexpr UBaseType_t PRIORITY_LOW = 1;            // MQTT, announcements
    
    // ESP-NOW configuration
    constexpr int ESPNOW_QUEUE_SIZE = 10;
    constexpr int ESPNOW_MESSAGE_QUEUE_SIZE = 10;  // Alias for clarity
    constexpr int NETWORK_CONFIG_QUEUE_SIZE = 5;   // Network config message queue
    
} // namespace task_config

namespace timing {
    // Intervals in milliseconds
    constexpr unsigned long ESPNOW_SEND_INTERVAL_MS = 2000;        // Data transmit rate (2 seconds)
    constexpr unsigned long ANNOUNCEMENT_INTERVAL_MS = 5000;       // Discovery announcements (5 seconds)
    constexpr unsigned long MQTT_PUBLISH_INTERVAL_MS = 10000;      // MQTT telemetry (10 seconds)
    constexpr unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;     // MQTT reconnect attempts (5 seconds)
    
} // namespace timing
