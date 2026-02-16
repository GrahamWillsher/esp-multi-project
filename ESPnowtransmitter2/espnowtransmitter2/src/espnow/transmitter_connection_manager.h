/**
 * @file transmitter_connection_manager.h
 * @brief ESP-NOW connection state machine for transmitter device
 * 
 * Extends EspNowConnectionBase with transmitter-specific 17-state machine.
 * Manages channel hopping, peer discovery, and connection lifecycle.
 * 
 * DEVICE-SPECIFIC CODE - Only for transmitter
 */

#pragma once

#include "espnow_connection_base.h"
#include "espnow_message_queue.h"
#include <cstdint>

/**
 * @brief Transmitter connection states (17 states)
 * 
 * This enum is DEVICE-SPECIFIC - only for transmitter
 */
enum class EspNowConnectionState : uint8_t {
    // Initialization
    UNINITIALIZED = 0,           // Initial state before ESP-NOW init
    INITIALIZING = 1,            // ESP-NOW being initialized
    
    // Discovery states
    IDLE = 2,                    // Ready but no peer
    DISCOVERING = 3,             // Broadcasting PROBE messages
    WAITING_FOR_ACK = 4,         // Waiting for receiver ACK
    ACK_RECEIVED = 5,            // ACK received, preparing to register
    
    // Channel locking states (CRITICAL - prevents race condition)
    CHANNEL_TRANSITION = 6,      // Switching to receiver's channel
    PEER_REGISTRATION = 7,       // Adding peer to ESP-NOW
    CHANNEL_STABILIZING = 8,     // Waiting for channel stability
    CHANNEL_LOCKED = 9,          // Channel locked and stable
    
    // Connected states
    CONNECTED = 10,              // Peer registered, ready to send
    DEGRADED = 11,               // Connected but poor quality
    
    // Disconnection states
    DISCONNECTING = 12,          // Graceful disconnect in progress
    DISCONNECTED = 13,           // Clean disconnect complete
    
    // Error/recovery states
    CONNECTION_LOST = 14,        // Unexpected connection loss
    RECONNECTING = 15,           // Attempting to reconnect
    ERROR_STATE = 16             // Unrecoverable error
};

/**
 * @brief Singleton connection manager for transmitter
 * 
 * Manages ESP-NOW connection lifecycle with 17-state machine.
 * Prevents race conditions during channel hopping.
 */
class TransmitterConnectionManager : public EspNowConnectionBase {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to singleton
     */
    static TransmitterConnectionManager& instance();
    
    /**
     * @brief Initialize the connection manager
     * @return true if initialized successfully
     */
    bool init();
    
    /**
     * @brief Update state machine (call regularly from main loop)
     */
    void update();
    
    // ========================================================================
    // PURE VIRTUAL METHOD IMPLEMENTATIONS (Required by base class)
    // ========================================================================
    
    /**
     * @brief Check if ready to send messages
     * @return true if in CONNECTED or DEGRADED state
     */
    bool is_ready_to_send() const override;
    
    /**
     * @brief Check if connected to peer
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
     * @param mac Peer MAC address
     * @param data Message data
     * @param len Data length
     * @return true if queued successfully
     */
    bool queue_message(const uint8_t* mac, const uint8_t* data, size_t len) override;
    
    // ========================================================================
    // TRANSMITTER-SPECIFIC INTERFACE
    // ========================================================================
    
    /**
     * @brief Get current connection state
     * @return Current state enum value
     */
    EspNowConnectionState get_state() const { return current_state_; }
    
    /**
     * @brief Set new state and record history
     * @param new_state State to transition to
     */
    void set_state(EspNowConnectionState new_state);
    
    /**
     * @brief Start discovery process
     * @return true if discovery started
     */
    bool start_discovery();
    
    /**
     * @brief Stop discovery process
     */
    void stop_discovery();
    
    /**
     * @brief Check if currently discovering
     * @return true if in discovery states
     */
    bool is_discovering() const;
    
    /**
     * @brief Check if in channel locking sequence
     * @return true if in channel locking states
     */
    bool is_channel_locking() const;
    
    /**
     * @brief Handle ACK received from receiver
     * @param receiver_mac Receiver MAC address
     * @param channel Receiver's channel
     * @return true if ACK handled successfully
     */
    bool handle_ack_received(const uint8_t* receiver_mac, uint8_t channel);
    
    /**
     * @brief Force disconnect and cleanup
     */
    void disconnect();
    
    /**
     * @brief Trigger reconnection attempt
     */
    void reconnect();
    
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
     * @brief Get total reconnection attempts
     */
    uint32_t get_total_reconnects() const { return reconnect_stats_.total_reconnects; }
    
    /**
     * @brief Get total discoveries completed
     */
    uint32_t get_total_discoveries() const { return total_discoveries_; }
    
    /**
     * @brief Get average reconnect time
     */
    uint32_t get_average_reconnect_time_ms() const { 
        return reconnect_stats_.average_reconnect_time_ms; 
    }
    
private:
    /**
     * @brief Private constructor (singleton)
     */
    TransmitterConnectionManager();
    
    /**
     * @brief Private destructor
     */
    ~TransmitterConnectionManager();
    
    // Prevent copying
    TransmitterConnectionManager(const TransmitterConnectionManager&) = delete;
    TransmitterConnectionManager& operator=(const TransmitterConnectionManager&) = delete;
    
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
     * @brief Handle IDLE state
     */
    void handle_idle();
    
    /**
     * @brief Handle DISCOVERING state
     */
    void handle_discovering();
    
    /**
     * @brief Handle WAITING_FOR_ACK state
     */
    void handle_waiting_for_ack();
    
    /**
     * @brief Handle ACK_RECEIVED state
     */
    void handle_ack_received();
    
    /**
     * @brief Handle CHANNEL_TRANSITION state
     */
    void handle_channel_transition();
    
    /**
     * @brief Handle PEER_REGISTRATION state
     */
    void handle_peer_registration();
    
    /**
     * @brief Handle CHANNEL_STABILIZING state
     */
    void handle_channel_stabilizing();
    
    /**
     * @brief Handle CHANNEL_LOCKED state
     */
    void handle_channel_locked();
    
    /**
     * @brief Handle CONNECTED state
     */
    void handle_connected();
    
    /**
     * @brief Handle DEGRADED state
     */
    void handle_degraded();
    
    /**
     * @brief Handle DISCONNECTING state
     */
    void handle_disconnecting();
    
    /**
     * @brief Handle DISCONNECTED state
     */
    void handle_disconnected();
    
    /**
     * @brief Handle CONNECTION_LOST state
     */
    void handle_connection_lost();
    
    /**
     * @brief Handle RECONNECTING state
     */
    void handle_reconnecting();
    
    /**
     * @brief Handle ERROR_STATE
     */
    void handle_error_state();
    
    // ========================================================================
    // HELPER METHODS
    // ========================================================================
    
    /**
     * @brief Register peer in ESP-NOW
     * @return true if registered successfully
     */
    bool register_peer();
    
    /**
     * @brief Unregister peer from ESP-NOW
     */
    void unregister_peer();
    
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
    EspNowConnectionState current_state_;
    
    // Message queue (own instance - NOT shared)
    EspNowMessageQueue message_queue_;
    
    // Reconnection statistics
    ReconnectionStats reconnect_stats_;
    
    // Discovery tracking
    uint32_t discovery_start_time_;
    uint32_t last_probe_time_;
    uint32_t total_discoveries_;
    bool discovery_active_;
    
    // Channel locking tracking
    uint32_t channel_lock_start_time_;
    uint8_t target_channel_;
    
    // Timing
    uint32_t state_enter_time_;
    uint32_t last_heartbeat_time_;
    uint32_t last_receive_time_;
    
    // Reconnection tracking
    uint32_t reconnect_start_time_;
    uint32_t reconnect_attempts_;
    
    // Singleton instance
    static TransmitterConnectionManager* instance_;
};
