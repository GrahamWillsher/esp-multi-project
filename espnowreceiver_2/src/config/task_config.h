#pragma once

#include <cstdint>

/**
 * @file task_config.h
 * @brief FreeRTOS Task Configuration Constants
 * 
 * Centralized configuration for task priorities, stack sizes, and timing intervals.
 * All task-related constants are defined here for easy tuning and optimization.
 */

namespace TaskConfig {

    // ========================================
    // Task Stack Sizes (in bytes)
    // ========================================
    
    /**
     * @brief ESPNow worker task stack size
     * 
     * Responsible for handling ESP-NOW message processing, routing,
     * and command forwarding to transmitter.
     * 
     * Current: 4096 bytes
     * Minimum: 2048 bytes
     * Increase if: Task runs out of stack during message processing
     */
    constexpr uint32_t ESPNOW_WORKER_STACK = 4096;
    
    /**
     * @brief MQTT client task stack size
     * 
     * Handles MQTT connections, pub/sub operations, and network I/O.
     * May need larger stack for JSON serialization and MQTT operations.
     * 
    * Current: 10240 bytes
     * Minimum: 2048 bytes
     * Increase if: MQTT operations fail or task crashes
     */
    constexpr uint32_t MQTT_CLIENT_STACK = 10240;

    /**
     * @brief Display renderer task stack size
     *
     * Consumes display snapshots and performs the actual TFT/LVGL rendering.
     * Keeps rendering work out of the ESP-NOW worker hot path.
     */
    constexpr uint32_t DISPLAY_RENDERER_STACK = 4096;
    
    /**
     * @brief Periodic announcement (discovery) task stack size
     * 
     * Handles periodic discovery announcements to the network.
     * May use MqttLogger which requires additional stack.
     * 
     * Current: 4096 bytes
     * Minimum: 2048 bytes
     * Increased for MqttLogger usage
     */
    constexpr uint32_t ANNOUNCEMENT_TASK_STACK = 4096;
    
    // ========================================
    // Task Priorities (FreeRTOS scale)
    // ========================================
    
    /**
     * @brief ESPNow worker task priority
     * 
     * Higher priority ensures messages are processed promptly.
     * Scale: 0 (lowest) to 25 (highest on ESP32)
     */
    constexpr uint8_t ESPNOW_WORKER_PRIORITY = 2;
    
    /**
     * @brief MQTT client task priority
     * 
     * Low priority - network I/O can be deferred.
     */
    constexpr uint8_t MQTT_CLIENT_PRIORITY = 0;

    /**
     * @brief Display renderer task priority
     *
     * Medium priority so rendering stays responsive without blocking ESP-NOW ingest.
     */
    constexpr uint8_t DISPLAY_RENDERER_PRIORITY = 1;
    
    /**
     * @brief Periodic announcement task priority
     * 
     * Very low priority - announcement is not time-critical.
     */
    constexpr uint8_t ANNOUNCEMENT_PRIORITY = 1;
    
    // ========================================
    // Task Core Affinity
    // ========================================
    
    /**
     * @brief CPU core for worker tasks (1 = Core 1, usually app core)
     * 
     * Core 0 is reserved for WiFi/BLE stack.
     * Core 1 is typically used for application code.
     */
    constexpr uint8_t WORKER_CORE = 1;
    
    // ========================================
    // Task Timing Constants (in milliseconds)
    // ========================================
    
    /**
     * @brief Periodic announcement interval
     * 
     * How often to broadcast receiver presence to the network.
     * Helps transmitter rediscover receiver after disconnection.
     * 
     * Current: 5000 ms (5 seconds)
     * Decrease to: Faster rediscovery (higher network traffic)
     * Increase to: Lower network traffic (slower rediscovery)
     */
    constexpr uint32_t ANNOUNCEMENT_INTERVAL_MS = 5000;
    
} // namespace TaskConfig
