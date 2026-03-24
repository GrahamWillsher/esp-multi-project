#pragma once
#include <freertos/FreeRTOS.h>

namespace task_config {
    // Task stack sizes (bytes)
    constexpr size_t STACK_SIZE_ESPNOW_RX = 4096;      // ESP-NOW message handling
    constexpr size_t STACK_SIZE_DATA_SENDER = 4096;    // Test data generation
    constexpr size_t STACK_SIZE_ANNOUNCEMENT = 4096;   // Periodic announcements (increased for settings notifications)
    constexpr size_t STACK_SIZE_MQTT = 8192;           // MQTT client (increased for large JSON payloads with PSRAM buffers)
    constexpr size_t STACK_SIZE_NETWORK_CONFIG = 4096; // Network configuration processing
    
    // Task priorities (higher number = higher priority)
    constexpr UBaseType_t PRIORITY_CRITICAL = 5;       // Control loop, safety functions
    constexpr UBaseType_t PRIORITY_ESPNOW = 4;         // ESP-NOW RX (must not block control)
    constexpr UBaseType_t PRIORITY_NETWORK_CONFIG = 3; // Network config (medium - heavy operations)
    constexpr UBaseType_t PRIORITY_NORMAL = 2;         // Data transmission
    constexpr UBaseType_t PRIORITY_LOW = 1;            // MQTT, announcements
    
    // ESP-NOW configuration
    constexpr size_t STACK_SIZE_COMP_CFG_SENDER = 3072;    // Component config periodic sender

    constexpr int ESPNOW_QUEUE_SIZE = 10;
    constexpr int ESPNOW_MESSAGE_QUEUE_SIZE = 10;  // Alias for clarity
    constexpr int NETWORK_CONFIG_QUEUE_SIZE = 5;   // Network config message queue
    
} // namespace task_config
