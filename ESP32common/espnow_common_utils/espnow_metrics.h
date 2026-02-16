/**
 * @file espnow_metrics.h
 * @brief ESP-NOW connection quality and performance metrics structures
 * 
 * Shared metric structures used by both transmitter and receiver.
 * Each device maintains its own instance of these metrics.
 */

#pragma once

#include <cstdint>
#include <cstring>

/**
 * @brief Connection quality metrics
 */
struct EspNowMetrics {
    // Send statistics
    uint32_t total_sends = 0;
    uint32_t successful_sends = 0;
    uint32_t failed_sends = 0;
    
    // Receive statistics
    uint32_t total_receives = 0;
    uint32_t invalid_receives = 0;
    
    // Connection tracking
    uint32_t total_connects = 0;
    uint32_t total_disconnects = 0;
    uint32_t total_reconnects = 0;
    
    // State transitions
    uint32_t total_state_changes = 0;
    
    // Timing
    uint32_t last_send_timestamp = 0;
    uint32_t last_receive_timestamp = 0;
    uint32_t connection_established_timestamp = 0;
    uint32_t last_state_change_timestamp = 0;
    
    // Quality indicators
    float current_success_rate = 100.0f;
    float connection_quality = 100.0f;  // 0-100%
    
    /**
     * @brief Reset all metrics to zero
     */
    void reset() {
        total_sends = 0;
        successful_sends = 0;
        failed_sends = 0;
        total_receives = 0;
        invalid_receives = 0;
        total_connects = 0;
        total_disconnects = 0;
        total_reconnects = 0;
        total_state_changes = 0;
        last_send_timestamp = 0;
        last_receive_timestamp = 0;
        connection_established_timestamp = 0;
        last_state_change_timestamp = 0;
        current_success_rate = 100.0f;
        connection_quality = 100.0f;
    }
    
    /**
     * @brief Calculate current send success rate
     * @return Success rate percentage (0-100)
     */
    float calculate_success_rate() const {
        if (total_sends == 0) {
            return 100.0f;
        }
        return (static_cast<float>(successful_sends) / total_sends) * 100.0f;
    }
    
    /**
     * @brief Get time since last successful send
     * @param current_time Current timestamp in milliseconds
     * @return Milliseconds since last send
     */
    uint32_t time_since_last_send(uint32_t current_time) const {
        if (last_send_timestamp == 0) {
            return 0;
        }
        return current_time - last_send_timestamp;
    }
    
    /**
     * @brief Get connection uptime
     * @param current_time Current timestamp in milliseconds
     * @return Milliseconds since connection established
     */
    uint32_t get_connection_uptime(uint32_t current_time) const {
        if (connection_established_timestamp == 0) {
            return 0;
        }
        return current_time - connection_established_timestamp;
    }
};

/**
 * @brief State history entry
 */
struct StateHistoryEntry {
    uint8_t state = 0;              // State enum value (device-specific)
    char state_name[32] = {0};      // Human-readable state name
    uint32_t timestamp_ms = 0;      // When state was entered
    uint32_t duration_ms = 0;       // How long in this state
    
    StateHistoryEntry() = default;
    
    StateHistoryEntry(uint8_t s, const char* name, uint32_t ts) 
        : state(s), timestamp_ms(ts), duration_ms(0) {
        strncpy(state_name, name, sizeof(state_name) - 1);
        state_name[sizeof(state_name) - 1] = '\0';
    }
};

/**
 * @brief Reconnection statistics
 */
struct ReconnectionStats {
    uint32_t total_reconnects = 0;
    uint32_t successful_reconnects = 0;
    uint32_t failed_reconnects = 0;
    uint32_t rapid_reconnects = 0;
    uint32_t average_reconnect_time_ms = 0;
    uint32_t fastest_reconnect_ms = 0xFFFFFFFF;
    uint32_t slowest_reconnect_ms = 0;
    uint32_t last_reconnect_timestamp = 0;
    
    /**
     * @brief Reset reconnection statistics
     */
    void reset() {
        total_reconnects = 0;
        successful_reconnects = 0;
        failed_reconnects = 0;
        rapid_reconnects = 0;
        average_reconnect_time_ms = 0;
        fastest_reconnect_ms = 0xFFFFFFFF;
        slowest_reconnect_ms = 0;
        last_reconnect_timestamp = 0;
    }
    
    /**
     * @brief Update statistics with new reconnection
     * @param duration_ms Time taken to reconnect
     * @param current_time Current timestamp
     * @param rapid_window_ms Time window to consider "rapid"
     */
    void record_reconnect(uint32_t duration_ms, uint32_t current_time, uint32_t rapid_window_ms) {
        total_reconnects++;
        successful_reconnects++;
        
        // Check if rapid reconnect
        if (last_reconnect_timestamp > 0 && 
            (current_time - last_reconnect_timestamp) < rapid_window_ms) {
            rapid_reconnects++;
        }
        
        // Update timing stats
        if (duration_ms < fastest_reconnect_ms) {
            fastest_reconnect_ms = duration_ms;
        }
        if (duration_ms > slowest_reconnect_ms) {
            slowest_reconnect_ms = duration_ms;
        }
        
        // Update average
        average_reconnect_time_ms = 
            ((average_reconnect_time_ms * (total_reconnects - 1)) + duration_ms) / total_reconnects;
        
        last_reconnect_timestamp = current_time;
    }
};
