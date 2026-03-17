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

namespace timing {
    // Intervals in milliseconds
    constexpr unsigned long ESPNOW_SEND_INTERVAL_MS = 2000;        // Data transmit rate (2 seconds)
    constexpr unsigned long ANNOUNCEMENT_INTERVAL_MS = 5000;       // Discovery announcements (5 seconds)
    constexpr unsigned long MQTT_PUBLISH_INTERVAL_MS = 10000;      // MQTT telemetry (10 seconds)
    constexpr unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;     // MQTT reconnect attempts (5 seconds)

    // TX-specific connection / heartbeat
    constexpr uint32_t TX_HEARTBEAT_TIMEOUT_MS = 35000;            // Transmitter heartbeat grace (35s vs common 30s)
    constexpr uint32_t DEFERRED_DISCOVERY_POLL_MS = 250;           // Backoff gate re-check interval
    constexpr uint32_t COMP_CFG_SEND_INTERVAL_MS = 5000;           // Component config periodic send interval
    constexpr uint32_t COMP_CFG_POLL_INTERVAL_MS = 500;            // Component config periodic check delay
    constexpr uint32_t ESPNOW_CONNECTING_TIMEOUT_MS = 30000;       // Max time to establish ESP-NOW connection

    // MQTT task intervals
    constexpr uint32_t MQTT_STATS_LOG_INTERVAL_MS = 30000;         // MQTT statistics logging period
    constexpr uint32_t MQTT_CELL_PUBLISH_INTERVAL_MS = 1000;       // Cell data publish rate
    constexpr uint32_t MQTT_EVENT_PUBLISH_INTERVAL_MS = 5000;      // Event log publish rate

    // Main-loop housekeeping intervals
    constexpr uint32_t ETH_STATE_MACHINE_UPDATE_INTERVAL_MS = 1000;  // Ethernet state machine poll rate
    constexpr uint32_t CAN_STATS_LOG_INTERVAL_MS = 10000;            // CAN bus statistics logging
    constexpr uint32_t STATE_VALIDATION_INTERVAL_MS = 30000;         // Discovery state validation period
    constexpr uint32_t METRICS_REPORT_INTERVAL_MS = 300000;          // ESP-NOW metrics report (5 min)
    constexpr uint32_t PEER_AUDIT_INTERVAL_MS = 120000;              // Peer state audit period (2 min)

} // namespace timing
