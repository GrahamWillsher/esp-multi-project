/**
 * @file receiver_connection_manager.h
 * @brief ESP-NOW connection state machine for receiver device
 * 
 * Extends EspNowConnectionBase with receiver-specific 10-state machine.
 * Manages ACK responses, transmitter tracking, and passive connection lifecycle.
 * 
 * DEVICE-SPECIFIC CODE - Only for receiver
 */

#pragma once

#include "espnow_connection_base.h"
#include "espnow_message_queue.h"
#include <cstdint>

/**
 * @brief Receiver connection states (10 states)
 * 
 * This enum is DEVICE-SPECIFIC - only for receiver
 */
enum class ReceiverConnectionState : uint8_t {
    // Initialization
    UNINITIALIZED = 0,           // Initial state before ESP-NOW init
    INITIALIZING = 1,            // ESP-NOW being initialized
    
    // Listening states
    LISTENING = 2,               // Waiting for PROBE from transmitter
    PROBE_RECEIVED = 3,          // PROBE received, preparing ACK
    SENDING_ACK = 4,             // Sending ACK to transmitter
    
    // Waiting for transmitter channel lock
    TRANSMITTER_LOCKING = 5,     // Waiting for transmitter to lock channel (~450ms)
    
    // Connected states
    CONNECTED = 6,               // Transmitter registered, active connection
    DEGRADED = 7,                // Connected but poor quality
    
    // Disconnection states
    CONNECTION_LOST = 8,         // Transmitter lost (timeout)
    ERROR_STATE = 9              // Unrecoverable error
};

/**
 * @brief Singleton connection manager for receiver
 * 
 * Manages ESP-NOW connection lifecycle with 10-state machine.
 * Passively responds to transmitter discovery and waits during channel locking.
 */
class ReceiverConnectionManager : public EspNowConnectionBase {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to singleton
     */
    static ReceiverConnectionManager& instance();
    
    /**
     * @brief Initialize the connection manager
     * @return true if initialized successfully
     */
    bool init();
    
    /**
     * @brief Update state machine (call regularly from main loop)
     */
    void update();
    
    /**
     * @brief Set new state and record history
     * @param new_state State to transition to
     */
    void set_state(ReceiverConnectionState new_state);
    
    // ========================================================================
    // PURE VIRTUAL METHOD IMPLEMENTATIONS (Required by base class)
    // ========================================================================
    
    /**
     * @brief Check if ready to send messages
     * @return true if in CONNECTED or DEGRADED state
     */
    bool is_ready_to_send() const override;
    
    /**
     * @brief Check if connected to transmitter
     * @return true if in CONNECTED or DEGRADED state
     */
    bool is_connected() const override;
    
    /**
     * @brief Get current state as string
     * @return Human-readable state name
     */
    const char* get_state_string() const override;
    
    /**
     * @brief Queue message for sending
     * @param mac Transmitter MAC address
     * @param data Message data
     * @param len Data length
     * @return true if queued successfully
     */
    bool queue_message(const uint8_t* mac, const uint8_t* data, size_t len) override;
    
    // ========================================================================
    // RECEIVER-SPECIFIC INTERFACE
    // ========================================================================
    
    /**
     * @brief Get current connection state
     * @return Current state enum value
     */
    ReceiverConnectionState get_state() const { return current_state_; }
    
    /**
     * @brief Handle PROBE received from transmitter
     * @param transmitter_mac Transmitter MAC address
     * @param channel Receiver's own channel
     * @return true if PROBE handled successfully
     */
    bool handle_probe_received(const uint8_t* transmitter_mac, uint8_t channel);
    
    /**
     * @brief Handle message received from transmitter (updates last_receive_time)
     */
    void handle_message_received();
    
    /**
     * @brief Force disconnect and cleanup
     */
    void disconnect();
    
    /**
     * @brief Get queue size
     * @return Number of queued messages
     */
    size_t get_queue_size() const { return message_queue_.size(); }
    
    /**
     * @brief Flush queued messages (send all pending)
     * @return Number of messages sent
     */
    size_t flush_queue();
    
    /**
     * @brief Get time since last message from transmitter
     * @return Milliseconds since last receive
     */
    uint32_t get_time_since_last_message() const;
    
private:
    /**
     * @brief Private constructor (singleton)
     */
    ReceiverConnectionManager();
    
    /**
     * @brief Private destructor
     */
    ~ReceiverConnectionManager();
    
    // Prevent copying
    ReceiverConnectionManager(const ReceiverConnectionManager&) = delete;
    ReceiverConnectionManager& operator=(const ReceiverConnectionManager&) = delete;
    
    // ========================================================================
    // STATE MACHINE METHODS
    // ========================================================================
    
    /**
     * @brief Update state machine logic
     */
    void update_state_machine();
    
    /**
     * @brief Handle UNINITIALIZED state
     */
    void handle_uninitialized();
    
    /**
     * @brief Handle INITIALIZING state
     */
    void handle_initializing();
    
    /**
     * @brief Handle LISTENING state
     */
    void handle_listening();
    
    /**
     * @brief Handle PROBE_RECEIVED state
     */
    void handle_probe_received();
    
    /**
     * @brief Handle SENDING_ACK state
     */
    void handle_sending_ack();
    
    /**
     * @brief Handle TRANSMITTER_LOCKING state
     */
    void handle_transmitter_locking();
    
    /**
     * @brief Handle CONNECTED state
     */
    void handle_connected();
    
    /**
     * @brief Handle DEGRADED state
     */
    void handle_degraded();
    
    /**
     * @brief Handle CONNECTION_LOST state
     */
    void handle_connection_lost();
    
    /**
     * @brief Handle ERROR_STATE
     */
    void handle_error_state();
    
    // ========================================================================
    // HELPER METHODS
    // ========================================================================
    
    /**
     * @brief Send ACK response to transmitter
     * @return true if ACK sent successfully
     */
    bool send_ack();
    
    /**
     * @brief Register transmitter as peer
     * @return true if registered successfully
     */
    bool register_transmitter();
    
    /**
     * @brief Unregister transmitter
     */
    void unregister_transmitter();
    
    /**
     * @brief Check connection health
     * @return true if connection is healthy
     */
    bool check_connection_health();
    
    /**
     * @brief Update connection quality metrics
     */
    void update_quality_metrics();
    
    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================
    
    // Current state
    ReceiverConnectionState current_state_;
    
    // Message queue (own instance - NOT shared)
    EspNowMessageQueue message_queue_;
    
    // Timing
    uint32_t state_enter_time_;
    uint32_t last_receive_time_;
    uint32_t last_probe_time_;
    uint32_t transmitter_lock_start_time_;
    
    // Transmitter tracking
    uint8_t transmitter_mac_[6];
    bool has_transmitter_;
    
    // Singleton instance
    static ReceiverConnectionManager* instance_;
};
