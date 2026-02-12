#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdint>

/**
 * @brief Section 11: Keep-Alive Manager
 * 
 * Maintains connection health via heartbeat exchange.
 * Detects disconnection and triggers recovery (channel hopping restart).
 * 
 * State Machine:
 * - CONNECTED: Normal operation, heartbeats regular
 * - DEGRADED: 30s since last heartbeat (warning)
 * - FAILURE: 60s since last heartbeat (grace period)
 * - DISCONNECTED: 90s since last heartbeat (trigger recovery)
 * 
 * Timing:
 * - Heartbeat interval: 10s (both TX and RX)
 * - Degraded threshold: 30s
 * - Failure threshold: 60s
 * - Disconnect threshold: 90s
 */
class KeepAliveManager {
public:
    enum class ConnectionState : uint8_t {
        DISCONNECTED,   // No connection or timeout exceeded
        CONNECTED,      // Normal operation
        DEGRADED,       // Missed heartbeats (30s)
        FAILURE         // Extended timeout (60s)
    };
    
    static KeepAliveManager& instance();
    
    /**
     * @brief Start keep-alive manager task
     * @param priority Task priority (default: Priority 2 - LOW)
     * @param core Core affinity (default: Core 1)
     */
    void start(uint8_t priority = 2, uint8_t core = 1);
    
    /**
     * @brief Stop keep-alive manager
     */
    void stop();
    
    /**
     * @brief Update last received heartbeat timestamp
     * Call this when HEARTBEAT message received from receiver
     */
    void record_heartbeat_received();
    
    /**
     * @brief Get current connection state
     */
    ConnectionState get_state() const { return state_; }
    
    /**
     * @brief Get time since last heartbeat (milliseconds)
     */
    uint32_t get_time_since_heartbeat() const;
    
    /**
     * @brief Check if task is running
     */
    bool is_running() const { return task_handle_ != nullptr; }
    
private:
    KeepAliveManager() = default;
    ~KeepAliveManager() = default;
    
    // Prevent copying
    KeepAliveManager(const KeepAliveManager&) = delete;
    KeepAliveManager& operator=(const KeepAliveManager&) = delete;
    
    // Task implementation
    static void task_impl(void* parameter);
    
    // Helper methods
    void send_heartbeat();
    void update_connection_state();
    void transition_to(ConnectionState new_state);
    const char* state_to_string(ConnectionState state) const;
    
    // State
    TaskHandle_t task_handle_{nullptr};
    ConnectionState state_{ConnectionState::DISCONNECTED};
    uint32_t last_heartbeat_received_{0};
    uint32_t last_heartbeat_sent_{0};
    uint32_t state_entry_time_{0};
    
    // Timing constants (milliseconds)
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 10000;   // 10s
    static constexpr uint32_t DEGRADED_THRESHOLD_MS = 30000;   // 30s
    static constexpr uint32_t FAILURE_THRESHOLD_MS = 60000;    // 60s
    static constexpr uint32_t DISCONNECT_THRESHOLD_MS = 90000; // 90s
};
