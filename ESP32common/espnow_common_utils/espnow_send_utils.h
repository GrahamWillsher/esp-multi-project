/**
 * @file espnow_send_utils.h
 * @brief Unified ESP-NOW send utilities with retry and backoff
 * 
 * Provides consistent failure handling across all ESP-NOW transmissions.
 * Implements consecutive failure tracking with automatic backoff to prevent
 * log spam and allow recovery from transient connection issues.
 */

#pragma once

#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <cstdint>
#include <cstddef>

class EspnowSendUtils {
public:
    /**
     * @brief Send ESP-NOW message with automatic retry and backoff
     * 
     * Tracks consecutive failures and pauses sending after max_failures
     * to prevent log spam. Automatically resumes after backoff period.
     * 
     * @param mac Destination MAC address
     * @param data Message data to send
     * @param len Message length in bytes
     * @param msg_name Human-readable message name for logging
     * @param max_failures Maximum consecutive failures before backoff (default: 10)
     * @param backoff_ms Backoff duration in milliseconds (default: 10000)
     * @return true if sent successfully, false on failure
     */
    static bool send_with_retry(
        const uint8_t* mac,
        const void* data,
        size_t len,
        const char* msg_name,
        uint8_t max_failures = 10,
        uint32_t backoff_ms = 10000
    );
    
    /**
     * @brief Reset failure counter (call after manual reconnection)
     */
    static void reset_failure_counter();
    
    /**
     * @brief Get current consecutive failure count
     */
    static uint8_t get_failure_count();
    
    /**
     * @brief Check if send is currently paused due to backoff
     */
    static bool is_paused();
    
    /**
     * @brief Check and handle deferred logging from timer callback
     * Call this periodically from a task with adequate stack
     */
    static void handle_deferred_logging();
    
private:
    static uint8_t consecutive_failures_;
    static bool send_paused_;
    static TimerHandle_t unpause_timer_;
    static volatile bool needs_unpause_log_;  // Flag for deferred MQTT logging
    
    /**
     * @brief Timer callback to unpause sending after backoff period
     */
    static void unpause_callback(TimerHandle_t xTimer);
};
