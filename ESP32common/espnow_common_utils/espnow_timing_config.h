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
#include <esp32common/config/timing_config.h>

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

struct ChannelLockTiming {
    uint32_t channel_transition_delay_ms;
    uint32_t peer_registration_delay_ms;
    uint32_t channel_stabilizing_delay_ms;
    uint32_t total_channel_lock_time_ms;
};

struct DiscoveryPolicyTiming {
    uint32_t probe_broadcast_interval_ms;
    uint32_t ack_wait_timeout_ms;
    uint32_t total_timeout_ms;
    uint32_t retry_delay_ms;
    uint32_t receiver_wait_for_lock_ms;
};

struct HeartbeatPolicyTiming {
    uint32_t interval_ms;
    uint32_t degraded_timeout_ms;
    uint32_t critical_timeout_ms;
};

struct RetryTiming {
    uint32_t initial_delay_ms;
    uint32_t max_delay_ms;
    uint32_t max_send_retries;
    float backoff_multiplier;
};

struct ConnectionQualityTiming {
    uint32_t assessment_interval_ms;
    uint32_t success_rate_window_ms;
    uint32_t min_sends_for_quality;
};

struct StateMachineTiming {
    uint32_t state_timeout_max_ms;
    uint32_t health_check_interval_ms;
};

struct ReconnectionTiming {
    uint32_t initial_delay_ms;
    uint32_t max_delay_ms;
    uint32_t max_rapid_reconnects;
    uint32_t rapid_reconnect_window_ms;
};

struct QueueTiming {
    uint32_t operation_timeout_ms;
    uint32_t flush_interval_ms;
    uint32_t max_queue_size;
};

struct WatchdogTiming {
    uint32_t check_interval_ms;
    uint32_t timeout_ms;
};

struct DiagnosticTiming {
    uint32_t report_interval_ms;
    uint32_t max_state_history_entries;
};

struct SafetyLimits {
    uint8_t max_wifi_channel;
    uint8_t min_wifi_channel;
    uint8_t mac_address_length;
    uint16_t max_espnow_payload;
};

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

constexpr ChannelLockTiming CHANNEL_LOCK{
    50,
    TimingConfig::DISCOVERY.peer_registration_delay_ms,
    TimingConfig::DISCOVERY.channel_switching_delay_ms,
    50 + TimingConfig::DISCOVERY.peer_registration_delay_ms + TimingConfig::DISCOVERY.channel_switching_delay_ms,
};

// Time to allow ESP-NOW hardware to transition to new channel
constexpr uint32_t CHANNEL_TRANSITION_DELAY_MS = CHANNEL_LOCK.channel_transition_delay_ms;

// Time for peer registration to complete after channel change
constexpr uint32_t PEER_REGISTRATION_DELAY_MS = CHANNEL_LOCK.peer_registration_delay_ms;

// Time for channel to stabilize after registration
constexpr uint32_t CHANNEL_STABILIZING_DELAY_MS = CHANNEL_LOCK.channel_stabilizing_delay_ms;

// Total time for complete channel lock sequence
constexpr uint32_t TOTAL_CHANNEL_LOCK_TIME_MS = CHANNEL_LOCK.total_channel_lock_time_ms;

// ============================================================================
// DISCOVERY TIMING
// ============================================================================

constexpr DiscoveryPolicyTiming DISCOVERY{
    1000,
    2000,
    30000,
    TimingConfig::DISCOVERY.retry_interval_ms,
    CHANNEL_LOCK.total_channel_lock_time_ms + 100,
};

// Interval between PROBE broadcasts (transmitter)
constexpr uint32_t PROBE_BROADCAST_INTERVAL_MS = DISCOVERY.probe_broadcast_interval_ms;

// Timeout waiting for ACK response (transmitter)
constexpr uint32_t ACK_WAIT_TIMEOUT_MS = DISCOVERY.ack_wait_timeout_ms;

// Total discovery attempt timeout
constexpr uint32_t DISCOVERY_TOTAL_TIMEOUT_MS = DISCOVERY.total_timeout_ms;

// Delay before retrying discovery after failure
constexpr uint32_t DISCOVERY_RETRY_DELAY_MS = DISCOVERY.retry_delay_ms;

// Time receiver waits for transmitter channel lock
constexpr uint32_t RECEIVER_WAIT_FOR_LOCK_MS = DISCOVERY.receiver_wait_for_lock_ms;

// ============================================================================
// HEARTBEAT TIMING
// ============================================================================

constexpr HeartbeatPolicyTiming HEARTBEAT{
    TimingConfig::HEARTBEAT.interval_ms,
    15000,
    25000,
};

// Interval between HEARTBEAT messages
constexpr uint32_t HEARTBEAT_INTERVAL_MS = HEARTBEAT.interval_ms;

// Timeout before considering connection degraded
constexpr uint32_t HEARTBEAT_DEGRADED_TIMEOUT_MS = HEARTBEAT.degraded_timeout_ms;

// Timeout before considering connection lost (critical)
constexpr uint32_t HEARTBEAT_CRITICAL_TIMEOUT_MS = HEARTBEAT.critical_timeout_ms;

// ============================================================================
// RETRY & BACKOFF TIMING
// ============================================================================

constexpr RetryTiming RETRY{
    50,
    1000,
    3,
    2.0f,
};

// Initial retry delay for send operations
constexpr uint32_t RETRY_INITIAL_DELAY_MS = RETRY.initial_delay_ms;

// Maximum retry delay (with exponential backoff)
constexpr uint32_t RETRY_MAX_DELAY_MS = RETRY.max_delay_ms;

// Maximum number of send retries
constexpr uint32_t MAX_SEND_RETRIES = RETRY.max_send_retries;

// Backoff multiplier for exponential backoff
constexpr float RETRY_BACKOFF_MULTIPLIER = RETRY.backoff_multiplier;

// ============================================================================
// CONNECTION QUALITY TIMING
// ============================================================================

constexpr ConnectionQualityTiming CONNECTION_QUALITY{
    5000,
    60000,
    10,
};

// Interval for connection quality assessment
constexpr uint32_t QUALITY_ASSESSMENT_INTERVAL_MS = CONNECTION_QUALITY.assessment_interval_ms;

// Time window for success rate calculation
constexpr uint32_t SUCCESS_RATE_WINDOW_MS = CONNECTION_QUALITY.success_rate_window_ms;

// Minimum sends in window for valid quality metric
constexpr uint32_t MIN_SENDS_FOR_QUALITY = CONNECTION_QUALITY.min_sends_for_quality;

// ============================================================================
// STATE MACHINE TIMING
// ============================================================================

constexpr StateMachineTiming STATE_MACHINE{
    60000,
    5000,
};

// Maximum time in any single state (safety timeout)
constexpr uint32_t STATE_TIMEOUT_MAX_MS = STATE_MACHINE.state_timeout_max_ms;

// Interval for state machine health checks
constexpr uint32_t STATE_HEALTH_CHECK_INTERVAL_MS = STATE_MACHINE.health_check_interval_ms;

// ============================================================================
// RECONNECTION TIMING
// ============================================================================

constexpr ReconnectionTiming RECONNECTION{
    2000,
    30000,
    5,
    60000,
};

// Delay before attempting reconnection after disconnect
constexpr uint32_t RECONNECT_INITIAL_DELAY_MS = RECONNECTION.initial_delay_ms;

// Maximum reconnection delay (with backoff)
constexpr uint32_t RECONNECT_MAX_DELAY_MS = RECONNECTION.max_delay_ms;

// Maximum number of rapid reconnection attempts
constexpr uint32_t MAX_RAPID_RECONNECTS = RECONNECTION.max_rapid_reconnects;

// Time window to consider reconnections "rapid"
constexpr uint32_t RAPID_RECONNECT_WINDOW_MS = RECONNECTION.rapid_reconnect_window_ms;

// ============================================================================
// MESSAGE QUEUE TIMING
// ============================================================================

constexpr QueueTiming QUEUE{
    1000,
    100,
    50,
};

// Timeout for queue operations
constexpr uint32_t QUEUE_OPERATION_TIMEOUT_MS = QUEUE.operation_timeout_ms;

// Interval for queue flush attempts
constexpr uint32_t QUEUE_FLUSH_INTERVAL_MS = QUEUE.flush_interval_ms;

// Maximum queue size
constexpr uint32_t MAX_QUEUE_SIZE = QUEUE.max_queue_size;

// ============================================================================
// WATCHDOG TIMING
// ============================================================================

constexpr WatchdogTiming WATCHDOG{
    1000,
    30000,
};

// Watchdog check interval
constexpr uint32_t WATCHDOG_CHECK_INTERVAL_MS = WATCHDOG.check_interval_ms;

// Watchdog timeout threshold
constexpr uint32_t WATCHDOG_TIMEOUT_MS = WATCHDOG.timeout_ms;

// ============================================================================
// DIAGNOSTIC TIMING
// ============================================================================

constexpr DiagnosticTiming DIAGNOSTICS{
    10000,
    50,
};

// Interval for diagnostic report generation
constexpr uint32_t DIAGNOSTIC_REPORT_INTERVAL_MS = DIAGNOSTICS.report_interval_ms;

// State history max entries
constexpr uint32_t MAX_STATE_HISTORY_ENTRIES = DIAGNOSTICS.max_state_history_entries;

// ============================================================================
// SAFETY LIMITS
// ============================================================================

constexpr SafetyLimits LIMITS{
    13,
    1,
    6,
    250,
};

// Maximum channel number (ESP32 WiFi)
constexpr uint8_t MAX_WIFI_CHANNEL = LIMITS.max_wifi_channel;

// Minimum channel number
constexpr uint8_t MIN_WIFI_CHANNEL = LIMITS.min_wifi_channel;

// Maximum peer MAC address length
constexpr uint8_t MAC_ADDRESS_LENGTH = LIMITS.mac_address_length;

// Maximum ESP-NOW payload size
constexpr uint16_t MAX_ESPNOW_PAYLOAD = LIMITS.max_espnow_payload;

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
