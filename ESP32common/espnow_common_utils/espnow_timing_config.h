/**
 * @file espnow_timing_config.h
 * @brief Centralized timing configuration for ESP-NOW state machine
 * 
 * This file contains all timing constants, debug flags, and logging macros
 * used by both transmitter and receiver ESP-NOW state machines.
 * 
 * CRITICAL: This is SHARED CODE - used by both devices
 * Each device uses these same timing values but maintains separate state instances.
 */

#pragma once

#include <cstdint>
#include <cstdio>
#include <esp_log.h>

// ============================================================================
// LOGGING MACROS
// ============================================================================
// Prefer project-specific logging if available.
#if !defined(LOG_DEBUG) && !defined(LOG_INFO) && !defined(LOG_WARN) && !defined(LOG_ERROR)
    #if defined(__has_include)
        #if __has_include("config/logging_config.h")
            #include "config/logging_config.h"
        #endif
    #endif
#endif

// Fallback to ESP-IDF logging if project logging is not provided.
#if !defined(LOG_DEBUG) && !defined(LOG_INFO) && !defined(LOG_WARN) && !defined(LOG_ERROR)
#define LOG_DEBUG(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#define LOG_INFO(tag, format, ...)  ESP_LOGI(tag, format, ##__VA_ARGS__)
#define LOG_WARN(tag, format, ...)  ESP_LOGW(tag, format, ##__VA_ARGS__)
#define LOG_ERROR(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#endif

namespace EspNowTiming {

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================

// Enable detailed state transition logging
constexpr bool DEBUG_STATE_TRANSITIONS = true;

// Enable channel hopping event logging
constexpr bool DEBUG_CHANNEL_HOPPING = true;

// Enable discovery phase logging
constexpr bool DEBUG_DISCOVERY = true;

// Enable send operation logging
constexpr bool DEBUG_SEND_OPERATIONS = false;

// Enable timing validation checks
constexpr bool DEBUG_TIMING_VALIDATION = true;

// Enable metrics tracking
constexpr bool ENABLE_METRICS = true;

// ============================================================================
// CHANNEL LOCKING TIMING (CRITICAL - Fixes Race Condition)
// ============================================================================

// Time to allow ESP-NOW hardware to transition to new channel
constexpr uint32_t CHANNEL_TRANSITION_DELAY_MS = 50;

// Time for peer registration to complete after channel change
constexpr uint32_t PEER_REGISTRATION_DELAY_MS = 100;

// Time for channel to stabilize after registration
constexpr uint32_t CHANNEL_STABILIZING_DELAY_MS = 300;

// Total time for complete channel lock sequence
constexpr uint32_t TOTAL_CHANNEL_LOCK_TIME_MS = 
    CHANNEL_TRANSITION_DELAY_MS + 
    PEER_REGISTRATION_DELAY_MS + 
    CHANNEL_STABILIZING_DELAY_MS;  // = 450ms

// ============================================================================
// DISCOVERY TIMING
// ============================================================================

// Interval between PROBE broadcasts (transmitter)
constexpr uint32_t PROBE_BROADCAST_INTERVAL_MS = 1000;

// Timeout waiting for ACK response (transmitter)
constexpr uint32_t ACK_WAIT_TIMEOUT_MS = 2000;

// Total discovery attempt timeout
constexpr uint32_t DISCOVERY_TOTAL_TIMEOUT_MS = 30000;  // 30 seconds

// Delay before retrying discovery after failure
constexpr uint32_t DISCOVERY_RETRY_DELAY_MS = 5000;

// Time receiver waits for transmitter channel lock
constexpr uint32_t RECEIVER_WAIT_FOR_LOCK_MS = TOTAL_CHANNEL_LOCK_TIME_MS + 100;  // 550ms

// ============================================================================
// HEARTBEAT TIMING
// ============================================================================

// Interval between HEARTBEAT messages
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 10000;  // 10 seconds

// Timeout before considering connection degraded
constexpr uint32_t HEARTBEAT_DEGRADED_TIMEOUT_MS = 15000;  // 15 seconds

// Timeout before considering connection lost (critical)
constexpr uint32_t HEARTBEAT_CRITICAL_TIMEOUT_MS = 25000;  // 25 seconds

// ============================================================================
// RETRY & BACKOFF TIMING
// ============================================================================

// Initial retry delay for send operations
constexpr uint32_t RETRY_INITIAL_DELAY_MS = 50;

// Maximum retry delay (with exponential backoff)
constexpr uint32_t RETRY_MAX_DELAY_MS = 1000;

// Maximum number of send retries
constexpr uint32_t MAX_SEND_RETRIES = 3;

// Backoff multiplier for exponential backoff
constexpr float RETRY_BACKOFF_MULTIPLIER = 2.0f;

// ============================================================================
// CONNECTION QUALITY TIMING
// ============================================================================

// Interval for connection quality assessment
constexpr uint32_t QUALITY_ASSESSMENT_INTERVAL_MS = 5000;

// Time window for success rate calculation
constexpr uint32_t SUCCESS_RATE_WINDOW_MS = 60000;  // 1 minute

// Minimum sends in window for valid quality metric
constexpr uint32_t MIN_SENDS_FOR_QUALITY = 10;

// ============================================================================
// STATE MACHINE TIMING
// ============================================================================

// Maximum time in any single state (safety timeout)
constexpr uint32_t STATE_TIMEOUT_MAX_MS = 60000;  // 1 minute

// Interval for state machine health checks
constexpr uint32_t STATE_HEALTH_CHECK_INTERVAL_MS = 5000;

// ============================================================================
// RECONNECTION TIMING
// ============================================================================

// Delay before attempting reconnection after disconnect
constexpr uint32_t RECONNECT_INITIAL_DELAY_MS = 2000;

// Maximum reconnection delay (with backoff)
constexpr uint32_t RECONNECT_MAX_DELAY_MS = 30000;  // 30 seconds

// Maximum number of rapid reconnection attempts
constexpr uint32_t MAX_RAPID_RECONNECTS = 5;

// Time window to consider reconnections "rapid"
constexpr uint32_t RAPID_RECONNECT_WINDOW_MS = 60000;  // 1 minute

// ============================================================================
// MESSAGE QUEUE TIMING
// ============================================================================

// Timeout for queue operations
constexpr uint32_t QUEUE_OPERATION_TIMEOUT_MS = 1000;

// Interval for queue flush attempts
constexpr uint32_t QUEUE_FLUSH_INTERVAL_MS = 100;

// Maximum queue size
constexpr uint32_t MAX_QUEUE_SIZE = 50;

// ============================================================================
// WATCHDOG TIMING
// ============================================================================

// Watchdog check interval
constexpr uint32_t WATCHDOG_CHECK_INTERVAL_MS = 1000;

// Watchdog timeout threshold
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 30000;  // 30 seconds

// ============================================================================
// DIAGNOSTIC TIMING
// ============================================================================

// Interval for diagnostic report generation
constexpr uint32_t DIAGNOSTIC_REPORT_INTERVAL_MS = 10000;

// State history max entries
constexpr uint32_t MAX_STATE_HISTORY_ENTRIES = 50;

// ============================================================================
// SAFETY LIMITS
// ============================================================================

// Maximum channel number (ESP32 WiFi)
constexpr uint8_t MAX_WIFI_CHANNEL = 13;

// Minimum channel number
constexpr uint8_t MIN_WIFI_CHANNEL = 1;

// Maximum peer MAC address length
constexpr uint8_t MAC_ADDRESS_LENGTH = 6;

// Maximum ESP-NOW payload size
constexpr uint16_t MAX_ESPNOW_PAYLOAD = 250;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Calculate exponential backoff delay
 * @param attempt Retry attempt number (0-based)
 * @param initial_delay_ms Initial delay in milliseconds
 * @param max_delay_ms Maximum delay cap
 * @return Calculated delay in milliseconds
 */
inline uint32_t calculate_backoff_delay(uint32_t attempt, 
                                       uint32_t initial_delay_ms = RETRY_INITIAL_DELAY_MS,
                                       uint32_t max_delay_ms = RETRY_MAX_DELAY_MS) {
    uint32_t delay = initial_delay_ms;
    for (uint32_t i = 0; i < attempt; i++) {
        delay = static_cast<uint32_t>(delay * RETRY_BACKOFF_MULTIPLIER);
        if (delay > max_delay_ms) {
            return max_delay_ms;
        }
    }
    return delay;
}

/**
 * @brief Validate channel number
 * @param channel Channel to validate
 * @return true if valid, false otherwise
 */
inline bool is_valid_channel(uint8_t channel) {
    return (channel >= MIN_WIFI_CHANNEL && channel <= MAX_WIFI_CHANNEL);
}

/**
 * @brief Get timing configuration as string (for diagnostics)
 * @return String description of key timing values
 */
inline const char* get_timing_summary() {
    static char buffer[512];
    snprintf(buffer, sizeof(buffer),
        "ESP-NOW Timing Configuration:\n"
        "  Channel Lock: %u ms (trans=%u reg=%u stab=%u)\n"
        "  Discovery: probe=%u ack_wait=%u total=%u ms\n"
        "  Heartbeat: interval=%u degraded=%u critical=%u ms\n"
        "  Retry: initial=%u max=%u attempts=%u\n"
        "  Reconnect: initial=%u max=%u rapid_limit=%u",
        TOTAL_CHANNEL_LOCK_TIME_MS, CHANNEL_TRANSITION_DELAY_MS, 
        PEER_REGISTRATION_DELAY_MS, CHANNEL_STABILIZING_DELAY_MS,
        PROBE_BROADCAST_INTERVAL_MS, ACK_WAIT_TIMEOUT_MS, DISCOVERY_TOTAL_TIMEOUT_MS,
        HEARTBEAT_INTERVAL_MS, HEARTBEAT_DEGRADED_TIMEOUT_MS, HEARTBEAT_CRITICAL_TIMEOUT_MS,
        RETRY_INITIAL_DELAY_MS, RETRY_MAX_DELAY_MS, MAX_SEND_RETRIES,
        RECONNECT_INITIAL_DELAY_MS, RECONNECT_MAX_DELAY_MS, MAX_RAPID_RECONNECTS
    );
    return buffer;
}

}  // namespace EspNowTiming
