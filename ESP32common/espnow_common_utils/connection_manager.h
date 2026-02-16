/**
 * @file connection_manager.h
 * @brief Generic ESP-NOW Connection Manager (Common Code - No Device-Specific Logic)
 * 
 * This is the core 3-state state machine used by BOTH transmitter and receiver.
 * 
 * Key Principles:
 * - ZERO device-specific code
 * - Pure state management and transitions
 * - Event-driven via FreeRTOS queues
 * - Singleton pattern for global access
 * - Thread-safe event posting from ISR callbacks
 * 
 * Device-specific behavior (how to discover, how to timeout, etc.) is
 * implemented in device-specific handlers (tx_connection_handler, rx_connection_handler)
 * which post events to this manager.
 */

#pragma once

#include "connection_event.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstdint>
#include <functional>
#include <vector>
#include <Arduino.h>

/**
 * @class EspNowConnectionManager
 * @brief Generic connection state machine
 * 
 * Responsibilities:
 * - Manage 3-state machine (IDLE → CONNECTING → CONNECTED)
 * - Process events from queue
 * - Track state transitions and timing
 * - Provide thread-safe state queries
 * 
 * NOT responsible for:
 * - How to discover peers (device-specific)
 * - How to detect timeouts (device-specific)
 * - How to send/receive data (handled by message router)
 * - Broadcasting probes/ACKs (device-specific)
 */
// State change callback type
using StateChangeCallback = std::function<void(EspNowConnectionState old_state, EspNowConnectionState new_state)>;

class EspNowConnectionManager {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to global manager instance
     */
    static EspNowConnectionManager& instance();
    
    /**
     * @brief Initialize the connection manager
     * 
     * Must be called once during setup, after FreeRTOS starts.
     * Creates the event queue and initializes state to IDLE.
     * 
     * @return true if initialization successful
     */
    bool init();
    
    /**
     * @brief Register a callback for state changes
     * 
     * Callbacks are invoked when state transitions occur, AFTER the
     * transition is complete. Multiple callbacks can be registered.
     * 
     * Callbacks are invoked from process_events() context (not ISR).
     * 
     * @param callback Function to call on state change
     */
    void register_state_callback(StateChangeCallback callback);
    
    /**
     * @brief Enable or disable auto-reconnect
     * 
     * When enabled, CONNECTION_LOST → IDLE transitions will automatically
     * post a CONNECTION_START event to restart the connection process.
     * 
     * @param enable true to enable auto-reconnect
     */
    void set_auto_reconnect(bool enable);
    
    /**
     * @brief Set timeout for CONNECTING state
     * 
     * If set to non-zero, the connection manager will automatically
     * transition to IDLE if CONNECTING state lasts longer than timeout.
     * 
     * @param timeout_ms Timeout in milliseconds (0 = no timeout)
     */
    void set_connecting_timeout_ms(uint32_t timeout_ms);
    
    /**
     * @brief Post an event to the state machine
     * 
     * Thread-safe. Can be called from:
     * - ISR callbacks (xQueueSendFromISR)
     * - Task context (xQueueSend)
     * - Any thread
     * 
     * @param event The event to post
     * @param mac Optional peer MAC address (depends on event type)
     * @return true if event queued successfully
     */
    bool post_event(EspNowEvent event, const uint8_t* mac = nullptr);
    
    /**
     * @brief Process all pending events
     * 
     * Should be called from the event processor task periodically
     * (typically every 100ms). Processes all events in queue,
     * making state transitions as needed.
     * 
     * Not called from ISR - safe to use logging and allocate memory.
     */
    void process_events();
    
    // ===== STATE QUERIES (const, thread-safe) =====
    
    /**
     * @brief Get current connection state
     * @return Current state
     */
    EspNowConnectionState get_state() const { return current_state_; }
    
    /**
     * @brief Check if idle
     * @return true if IDLE
     */
    bool is_idle() const { return current_state_ == EspNowConnectionState::IDLE; }
    
    /**
     * @brief Check if connecting
     * @return true if CONNECTING
     */
    bool is_connecting() const { return current_state_ == EspNowConnectionState::CONNECTING; }
    
    /**
     * @brief Check if connected
     * @return true if CONNECTED
     */
    bool is_connected() const { return current_state_ == EspNowConnectionState::CONNECTED; }
    
    /**
     * @brief Check if ready to send/receiv   // Current state
    uint8_t peer_mac_[6];                     // Connected peer MAC
    uint32_t state_enter_time_;               // When we entered current state
    QueueHandle_t event_queue_;               // FreeRTOS queue for events
    std::vector<StateChangeCallback> state_callbacks_;  // Registered state change callbacks
    bool auto_reconnect_enabled_;             // Auto-reconnect on connection loss
    uint32_t connecting_timeout_ms_;          // Timeout for CONNECTING state (0 = no timeout)
    
    /**
     * @brief Get state as human-readable string
     * @return Pointer to static string (do not free)
     */
    const char* get_state_string() const { return state_to_string(current_state_); }
    
    /**
     * @brief Get peer MAC address
     * @return Pointer to 6-byte MAC address
     */
    const uint8_t* get_peer_mac() const { return peer_mac_; }
    
    /**
     * @brief Get time connected in milliseconds
     * @return Milliseconds since entering CONNECTED state (0 if not connected)
     */
    uint32_t get_connected_time_ms() const;
    
    /**
     * @brief Get time in current state
     * @return Milliseconds since last state change
     */
    uint32_t get_state_time_ms() const;

private:
    // ===== PRIVATE STATE =====
    
    EspNowConnectionState current_state_;     // Current state
    uint8_t peer_mac_[6];                     // Connected peer MAC
    uint32_t state_enter_time_;               // When we entered current state
    QueueHandle_t event_queue_;               // FreeRTOS queue for events
    std::vector<StateChangeCallback> state_callbacks_;  // Registered state change callbacks
    bool auto_reconnect_enabled_;             // Auto-reconnect on connection loss
    uint32_t connecting_timeout_ms_;          // Timeout for CONNECTING state (0 = no timeout)
    
    // Private constructor (singleton)
    EspNowConnectionManager();
    
    // Prevent copying
    EspNowConnectionManager(const EspNowConnectionManager&) = delete;
    EspNowConnectionManager& operator=(const EspNowConnectionManager&) = delete;
    
    // ===== PRIVATE METHODS =====
    
    /**
     * @brief Handle a single event
     * @param event The event to handle
     */
    void handle_event(const EspNowStateChange& event);
    
    /**
     * @brief Transition to a new state
     * @param new_state The state to transition to
     */
    void transition_to_state(EspNowConnectionState new_state);
    
    /**
     * @brief Handle IDLE state transitions
     * @param event The event to handle
     */
    void handle_idle_event(const EspNowStateChange& event);
    
    /**
     * @brief Handle CONNECTING state transitions
     * @param event The event to handle
     */
    void handle_connecting_event(const EspNowStateChange& event);
    
    /**
     * @brief Handle CONNECTED state transitions
     * @param event The event to handle
     */
    void handle_connected_event(const EspNowStateChange& event);
};

// ===== GLOBAL QUEUE (exported for use by all modules) =====

/**
 * @brief Global event queue
 * 
 * Created by connection_manager.cpp after init().
 * Used to post events from callbacks/tasks.
 */
extern QueueHandle_t g_connection_event_queue;

/**
 * @brief Helper function to post event from ISR context
 * 
 * Thread-safe wrapper that uses xQueueSendFromISR internally.
 * Can be called from any context (ISR, task, etc.)
 * 
 * @param event The event to post
 * @param mac Optional peer MAC address
 * @return true if event queued successfully
 */
inline bool post_connection_event(EspNowEvent event, const uint8_t* mac = nullptr) {
    if (g_connection_event_queue == nullptr) {
        return false;  // Not initialized yet
    }
    
    EspNowStateChange change(event, mac);
    change.timestamp = millis();
    
    BaseType_t result = xQueueSendFromISR(g_connection_event_queue, &change, nullptr);
    return result == pdTRUE;
}
