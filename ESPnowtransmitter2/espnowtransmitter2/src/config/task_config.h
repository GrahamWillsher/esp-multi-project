#pragma once
#include <freertos/FreeRTOS.h>

namespace task_config {
    // Task stack sizes (bytes)
    constexpr size_t STACK_SIZE_ESPNOW_RX = 4096;      // ESP-NOW message handling
    constexpr size_t STACK_SIZE_DATA_SENDER = 4096;    // Test data generation
    constexpr size_t STACK_SIZE_ANNOUNCEMENT = 2048;   // Periodic announcements
    constexpr size_t STACK_SIZE_MQTT = 4096;           // MQTT client
    
    // Task priorities (higher number = higher priority)
    constexpr UBaseType_t PRIORITY_CRITICAL = 3;       // ESP-NOW RX
    constexpr UBaseType_t PRIORITY_NORMAL = 2;         // Data transmission
    constexpr UBaseType_t PRIORITY_LOW = 1;            // MQTT, announcements
    
    // ESP-NOW configuration
    constexpr int ESPNOW_QUEUE_SIZE = 10;
    constexpr int ESPNOW_MESSAGE_QUEUE_SIZE = 10;  // Alias for clarity
    
} // namespace task_config

namespace timing {
    // Intervals in milliseconds
    constexpr unsigned long ESPNOW_SEND_INTERVAL_MS = 2000;        // Data transmit rate (2 seconds)
    constexpr unsigned long ANNOUNCEMENT_INTERVAL_MS = 5000;       // Discovery announcements (5 seconds)
    constexpr unsigned long MQTT_PUBLISH_INTERVAL_MS = 10000;      // MQTT telemetry (10 seconds)
    constexpr unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;     // MQTT reconnect attempts (5 seconds)
    
} // namespace timing
