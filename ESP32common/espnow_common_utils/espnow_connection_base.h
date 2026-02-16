/**
 * @file espnow_connection_base.h
 * @brief Base class for ESP-NOW connection state management
 * 
 * This is SHARED CODE used by both transmitter and receiver.
 * Provides common functionality for state management, metrics, and send operations.
 * 
 * Each device extends this base class with device-specific state machine:
 * - Transmitter: TransmitterConnectionManager (17 states)
 * - Receiver: ReceiverConnectionManager (10 states)
 * 
 * CRITICAL: Each device creates its own instance - NO SHARED DATA
 */

#pragma once

#include <esp_now.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>
#include <functional>
#include <queue>
#include "espnow_timing_config.h"
#include "espnow_metrics.h"

/**
 * @brief Connection event types for callbacks
 */
enum class EspNowConnectionEvent {
    STATE_CHANGED,
    CONNECTED,
    DISCONNECTED,
    DEGRADED,
    RECONNECTING,
    SEND_SUCCESS,
    SEND_FAILED,
    PEER_REGISTERED,
    PEER_REMOVED,
    DISCOVERY_STARTED,
    DISCOVERY_COMPLETE,
    CHANNEL_CHANGED
};

/**
 * @brief Event callback function type
 */
using EspNowEventCallback = std::function<void(EspNowConnectionEvent event, void* data)>;

/**
 * @brief Base class for ESP-NOW connection management
 * 
 * Provides common functionality for both transmitter and receiver:
 * - Thread-safe state management
 * - Connection metrics tracking
 * - Safe send operations with retry
 * - Event callbacks
 * - Diagnostic reporting
 * 
 * Pure virtual methods that MUST be implemented by derived classes:
 * - is_ready_to_send() - Check if device can send messages
 * - is_connected() - Check if device has active connection
 * - get_state_string() - Get human-readable state name
 * - queue_message() - Queue message for sending
 */
class EspNowConnectionBase {
public:
    /**
     * @brief Constructor - initializes mutex and metrics
     */
    EspNowConnectionBase();
    
    /**
     * @brief Virtual destructor - cleans up mutex
     */
    virtual ~EspNowConnectionBase();
    
    // ========================================================================
    // PURE VIRTUAL METHODS - Must be implemented by derived classes
    // ========================================================================
    
    /**
     * @brief Check if ready to send messages
     * @return true if in a state that allows sending
     */
    virtual bool is_ready_to_send() const = 0;
    
    /**
     * @brief Check if currently connected to peer
     * @return true if connected
     */
    virtual bool is_connected() const = 0;
    
    /**
     * @brief Get current state as human-readable string
     * @return State name (device-specific)
     */
    virtual const char* get_state_string() const = 0;
    
    /**
     * @brief Queue message for sending when ready
     * @param mac Peer MAC address
     * @param data Message data
     * @param len Data length
     * @return true if queued successfully
     */
    virtual bool queue_message(const uint8_t* mac, const uint8_t* data, size_t len) = 0;
    
    // ========================================================================
    // COMMON PUBLIC INTERFACE
    // ========================================================================
    
    /**
     * @brief Safe send with state checking and retry
     * @param mac Peer MAC address
     * @param data Message data
     * @param len Data length
     * @return true if sent successfully or queued
     */
    bool safe_send(const uint8_t* mac, const uint8_t* data, size_t len);
    
    /**
     * @brief Register event callback
     * @param callback Function to call on events
     */
    void register_callback(EspNowEventCallback callback);
    
    /**
     * @brief Get current metrics
     * @return Reference to metrics structure
     */
    const EspNowMetrics& get_metrics() const { return metrics_; }
    
    /**
     * @brief Reset all metrics
     */
    void reset_metrics();
    
    /**
     * @brief Get send success rate
     * @return Percentage (0-100)
     */
    float get_send_success_rate() const;
    
    /**
     * @brief Get connection quality score
     * @return Quality percentage (0-100)
     */
    float get_connection_quality() const;
    
    /**
     * @brief Get total successful sends
     */
    uint32_t get_successful_sends() const { return metrics_.successful_sends; }
    
    /**
     * @brief Get total failed sends
     */
    uint32_t get_failed_sends() const { return metrics_.failed_sends; }
    
    /**
     * @brief Get total state changes
     */
    uint32_t get_total_state_changes() const { return metrics_.total_state_changes; }
    
    /**
     * @brief Get connection uptime in milliseconds
     */
    uint32_t get_uptime_connected_ms() const;
    
    /**
     * @brief Get state history
     * @return Vector of state history entries
     */
    const std::vector<StateHistoryEntry>& get_state_history() const { return state_history_; }
    
    /**
     * @brief Generate diagnostic report
     * @param buffer Buffer to write report into
     * @param buffer_size Size of buffer
     * @return Number of characters written
     */
    size_t generate_diagnostic_report(char* buffer, size_t buffer_size) const;
    
    /**
     * @brief Check if in degraded state
     * @return true if connection quality is poor
     */
    bool is_degraded() const;
    
protected:
    // ========================================================================
    // PROTECTED METHODS - Available to derived classes
    // ========================================================================
    
    /**
     * @brief Lock state mutex for thread-safe access
     * @return true if locked successfully
     */
    bool lock_state();
    
    /**
     * @brief Unlock state mutex
     */
    void unlock_state();
    
    /**
     * @brief Record state change in history
     * @param state_code Numeric state value
     * @param state_name Human-readable name
     */
    void record_state_change(uint8_t state_code, const char* state_name);
    
    /**
     * @brief Trigger event callback
     * @param event Event type
     * @param data Optional event data
     */
    void trigger_event(EspNowConnectionEvent event, void* data = nullptr);
    
    /**
     * @brief Update connection quality based on metrics
     */
    void update_connection_quality();
    
    /**
     * @brief Record successful send
     */
    void record_send_success();
    
    /**
     * @brief Record failed send
     */
    void record_send_failure();
    
    /**
     * @brief Record message received
     */
    void record_receive();
    
    /**
     * @brief Get current timestamp in milliseconds
     * @return Milliseconds since boot
     */
    uint32_t get_current_time_ms() const;
    
    // ========================================================================
    // PROTECTED MEMBER VARIABLES
    // ========================================================================
    
    // Thread safety
    SemaphoreHandle_t state_mutex_;
    
    // Metrics
    EspNowMetrics metrics_;
    
    // State history
    std::vector<StateHistoryEntry> state_history_;
    uint32_t max_history_entries_;
    
    // Event callbacks
    std::vector<EspNowEventCallback> callbacks_;
    
    // Peer information (common to both devices)
    uint8_t peer_mac_[6];
    bool has_peer_;
    uint8_t current_channel_;
    
    // Logging tag
    const char* log_tag_;
};
