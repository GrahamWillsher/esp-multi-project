/**
 * @file receiver_connection_manager.cpp
 * @brief Implementation of receiver ESP-NOW connection state machine
 */

#include "receiver_connection_manager.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <cstring>

// Static instance
ReceiverConnectionManager* ReceiverConnectionManager::instance_ = nullptr;

// ============================================================================
// SINGLETON
// ============================================================================

ReceiverConnectionManager& ReceiverConnectionManager::instance() {
    if (instance_ == nullptr) {
        instance_ = new ReceiverConnectionManager();
    }
    return *instance_;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

ReceiverConnectionManager::ReceiverConnectionManager()
    : EspNowConnectionBase(),
      current_state_(ReceiverConnectionState::UNINITIALIZED),
      state_enter_time_(0),
      last_receive_time_(0),
      last_probe_time_(0),
      transmitter_lock_start_time_(0),
      has_transmitter_(false) {
    
    log_tag_ = "RX_CONN_MGR";
    memset(transmitter_mac_, 0, sizeof(transmitter_mac_));
    LOG_INFO(log_tag_, "Receiver Connection Manager created");
}

ReceiverConnectionManager::~ReceiverConnectionManager() {
    LOG_INFO(log_tag_, "Receiver Connection Manager destroyed");
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool ReceiverConnectionManager::init() {
    if (current_state_ != ReceiverConnectionState::UNINITIALIZED) {
        LOG_WARN(log_tag_, "Already initialized");
        return true;
    }
    
    LOG_INFO(log_tag_, "Initializing receiver connection manager...");
    set_state(ReceiverConnectionState::INITIALIZING);
    
    // ESP-NOW initialization happens externally
    // Move to LISTENING state
    set_state(ReceiverConnectionState::LISTENING);
    
    LOG_INFO(log_tag_, "Initialization complete - listening for transmitter");
    return true;
}

// ============================================================================
// PURE VIRTUAL METHOD IMPLEMENTATIONS
// ============================================================================

bool ReceiverConnectionManager::is_ready_to_send() const {
    // Can only send when in CONNECTED or DEGRADED states
    return (current_state_ == ReceiverConnectionState::CONNECTED ||
            current_state_ == ReceiverConnectionState::DEGRADED);
}

bool ReceiverConnectionManager::is_connected() const {
    return (current_state_ == ReceiverConnectionState::CONNECTED ||
            current_state_ == ReceiverConnectionState::DEGRADED);
}

const char* ReceiverConnectionManager::get_state_string() const {
    switch (current_state_) {
        case ReceiverConnectionState::UNINITIALIZED:        return "UNINITIALIZED";
        case ReceiverConnectionState::INITIALIZING:         return "INITIALIZING";
        case ReceiverConnectionState::LISTENING:            return "LISTENING";
        case ReceiverConnectionState::PROBE_RECEIVED:       return "PROBE_RECEIVED";
        case ReceiverConnectionState::SENDING_ACK:          return "SENDING_ACK";
        case ReceiverConnectionState::TRANSMITTER_LOCKING:  return "TRANSMITTER_LOCKING";
        case ReceiverConnectionState::CONNECTED:            return "CONNECTED";
        case ReceiverConnectionState::DEGRADED:             return "DEGRADED";
        case ReceiverConnectionState::CONNECTION_LOST:      return "CONNECTION_LOST";
        case ReceiverConnectionState::ERROR_STATE:          return "ERROR_STATE";
        default:                                            return "UNKNOWN";
    }
}

bool ReceiverConnectionManager::queue_message(const uint8_t* mac, const uint8_t* data, size_t len) {
    return message_queue_.push(mac, data, len);
}

// ============================================================================
// STATE MANAGEMENT
// ============================================================================

void ReceiverConnectionManager::set_state(ReceiverConnectionState new_state) {
    if (current_state_ == new_state) {
        return;  // No change
    }
    
    if (lock_state()) {
        current_state_ = new_state;
        state_enter_time_ = get_current_time_ms();
        
        // Record state change in history
        record_state_change(static_cast<uint8_t>(new_state), get_state_string());
        
        unlock_state();
        
        if (EspNowTiming::DEBUG_STATE_TRANSITIONS) {
            LOG_INFO(log_tag_, "State changed");
        }
    }
}

// ============================================================================
// UPDATE LOOP
// ============================================================================

void ReceiverConnectionManager::update() {
    update_state_machine();
    
    // Flush queue if connected
    if (is_ready_to_send() && !message_queue_.empty()) {
        flush_queue();
    }
    
    // Update quality metrics periodically
    static uint32_t last_quality_update = 0;
    uint32_t now = get_current_time_ms();
    if (now - last_quality_update > EspNowTiming::QUALITY_ASSESSMENT_INTERVAL_MS) {
        update_quality_metrics();
        last_quality_update = now;
    }
}

void ReceiverConnectionManager::update_state_machine() {
    switch (current_state_) {
        case ReceiverConnectionState::UNINITIALIZED:
            handle_uninitialized();
            break;
        case ReceiverConnectionState::INITIALIZING:
            handle_initializing();
            break;
        case ReceiverConnectionState::LISTENING:
            handle_listening();
            break;
        case ReceiverConnectionState::PROBE_RECEIVED:
            handle_probe_received();
            break;
        case ReceiverConnectionState::SENDING_ACK:
            handle_sending_ack();
            break;
        case ReceiverConnectionState::TRANSMITTER_LOCKING:
            handle_transmitter_locking();
            break;
        case ReceiverConnectionState::CONNECTED:
            handle_connected();
            break;
        case ReceiverConnectionState::DEGRADED:
            handle_degraded();
            break;
        case ReceiverConnectionState::CONNECTION_LOST:
            handle_connection_lost();
            break;
        case ReceiverConnectionState::ERROR_STATE:
            handle_error_state();
            break;
    }
}

// ============================================================================
// STATE HANDLERS
// ============================================================================

void ReceiverConnectionManager::handle_uninitialized() {
    // Waiting for init() to be called
}

void ReceiverConnectionManager::handle_initializing() {
    // ESP-NOW init happens externally
    // This state transitions immediately to LISTENING in init()
}

void ReceiverConnectionManager::handle_listening() {
    // Passively waiting for PROBE from transmitter
    // PROBE handling done via handle_probe_received() callback
}

void ReceiverConnectionManager::handle_probe_received() {
    // Immediately send ACK
    set_state(ReceiverConnectionState::SENDING_ACK);
}

void ReceiverConnectionManager::handle_sending_ack() {
    // Send ACK to transmitter
    if (!send_ack()) {
        LOG_ERROR(log_tag_, "Failed to send ACK");
        set_state(ReceiverConnectionState::LISTENING);
        return;
    }
    
    LOG_INFO(log_tag_, "ACK sent, waiting for transmitter to lock channel");
    transmitter_lock_start_time_ = get_current_time_ms();
    set_state(ReceiverConnectionState::TRANSMITTER_LOCKING);
}

void ReceiverConnectionManager::handle_transmitter_locking() {
    uint32_t now = get_current_time_ms();
    uint32_t elapsed = now - transmitter_lock_start_time_;
    
    // Wait for transmitter to complete channel lock sequence (~450ms)
    if (elapsed >= EspNowTiming::RECEIVER_WAIT_FOR_LOCK_MS) {
        LOG_INFO(log_tag_, "Transmitter should be locked, registering peer");
        
        // Register transmitter as peer
        if (!register_transmitter()) {
            LOG_ERROR(log_tag_, "Failed to register transmitter");
            set_state(ReceiverConnectionState::ERROR_STATE);
            return;
        }
        
        // Move to CONNECTED state
        metrics_.connection_established_timestamp = get_current_time_ms();
        metrics_.total_connects++;
        last_receive_time_ = now;
        
        trigger_event(EspNowConnectionEvent::CONNECTED, nullptr);
        set_state(ReceiverConnectionState::CONNECTED);
        LOG_INFO(log_tag_, "Connection established");
    }
}

void ReceiverConnectionManager::handle_connected() {
    // Check connection health
    if (!check_connection_health()) {
        LOG_WARN(log_tag_, "Connection degraded");
        set_state(ReceiverConnectionState::DEGRADED);
        trigger_event(EspNowConnectionEvent::DEGRADED, nullptr);
        return;
    }
    
    // Check for transmitter timeout
    uint32_t now = get_current_time_ms();
    if (now - last_receive_time_ > EspNowTiming::HEARTBEAT_CRITICAL_TIMEOUT_MS) {
        LOG_ERROR(log_tag_, "Transmitter lost (timeout)");
        set_state(ReceiverConnectionState::CONNECTION_LOST);
        trigger_event(EspNowConnectionEvent::DISCONNECTED, nullptr);
    }
}

void ReceiverConnectionManager::handle_degraded() {
    // Check if connection recovered
    if (check_connection_health()) {
        LOG_INFO(log_tag_, "Connection recovered");
        set_state(ReceiverConnectionState::CONNECTED);
        return;
    }
    
    // Check if connection completely lost
    uint32_t now = get_current_time_ms();
    if (now - last_receive_time_ > EspNowTiming::HEARTBEAT_CRITICAL_TIMEOUT_MS) {
        LOG_ERROR(log_tag_, "Connection lost (critical timeout)");
        set_state(ReceiverConnectionState::CONNECTION_LOST);
        trigger_event(EspNowConnectionEvent::DISCONNECTED, nullptr);
    }
}

void ReceiverConnectionManager::handle_connection_lost() {
    // Connection lost - cleanup and return to listening
    metrics_.total_disconnects++;
    unregister_transmitter();
    
    LOG_INFO(log_tag_, "Returning to listening state");
    set_state(ReceiverConnectionState::LISTENING);
}

void ReceiverConnectionManager::handle_error_state() {
    // Unrecoverable error - manual intervention required
    LOG_ERROR(log_tag_, "In ERROR_STATE - manual reset required");
}

// ============================================================================
// PROBE/ACK HANDLING
// ============================================================================

bool ReceiverConnectionManager::handle_probe_received(const uint8_t* transmitter_mac, uint8_t channel) {
    if (transmitter_mac == nullptr) {
        LOG_ERROR(log_tag_, "Invalid transmitter MAC");
        return false;
    }
    
    // Only accept PROBE in LISTENING state
    if (current_state_ != ReceiverConnectionState::LISTENING) {
        LOG_WARN(log_tag_, "PROBE received in wrong state: %s", get_state_string());
        return false;
    }
    
    LOG_INFO(log_tag_, "PROBE received from transmitter");
    
    // Save transmitter info
    memcpy(transmitter_mac_, transmitter_mac, 6);
    memcpy(peer_mac_, transmitter_mac, 6);  // Base class peer_mac_
    has_transmitter_ = true;
    has_peer_ = true;  // Base class has_peer_
    current_channel_ = channel;
    last_probe_time_ = get_current_time_ms();
    
    // Trigger state change to send ACK
    set_state(ReceiverConnectionState::PROBE_RECEIVED);
    
    return true;
}

bool ReceiverConnectionManager::send_ack() {
    if (!has_transmitter_) {
        LOG_ERROR(log_tag_, "Cannot send ACK - no transmitter info");
        return false;
    }
    
    // ACK message structure (simple - just needs to be recognized)
    struct {
        uint8_t message_type;  // ACK type
        uint8_t channel;       // Receiver's channel
    } ack_msg;
    
    ack_msg.message_type = 0x02;  // ACK message type
    ack_msg.channel = current_channel_;
    
    // Send ACK using broadcast (transmitter not registered yet)
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t result = esp_now_send(broadcast_mac, (uint8_t*)&ack_msg, sizeof(ack_msg));
    
    if (result != ESP_OK) {
        LOG_ERROR(log_tag_, "Failed to send ACK: %s", esp_err_to_name(result));
        return false;
    }
    
    LOG_INFO(log_tag_, "ACK sent on channel %u", current_channel_);
    record_send_success();
    return true;
}

void ReceiverConnectionManager::handle_message_received() {
    last_receive_time_ = get_current_time_ms();
    record_receive();
}

// ============================================================================
// PEER MANAGEMENT
// ============================================================================

bool ReceiverConnectionManager::register_transmitter() {
    if (!has_transmitter_) {
        LOG_ERROR(log_tag_, "Cannot register transmitter - no transmitter info");
        return false;
    }
    
    // Add transmitter to ESP-NOW
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, transmitter_mac_, 6);
    peer_info.channel = current_channel_;
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;
    
    esp_err_t result = esp_now_add_peer(&peer_info);
    if (result != ESP_OK) {
        LOG_ERROR(log_tag_, "Failed to add transmitter: %s", esp_err_to_name(result));
        return false;
    }
    
    LOG_INFO(log_tag_, "Transmitter registered on channel %u", current_channel_);
    trigger_event(EspNowConnectionEvent::PEER_REGISTERED, nullptr);
    return true;
}

void ReceiverConnectionManager::unregister_transmitter() {
    if (!has_transmitter_) {
        return;
    }
    
    esp_now_del_peer(transmitter_mac_);
    has_transmitter_ = false;
    has_peer_ = false;
    memset(transmitter_mac_, 0, 6);
    memset(peer_mac_, 0, 6);
    
    LOG_INFO(log_tag_, "Transmitter unregistered");
    trigger_event(EspNowConnectionEvent::PEER_REMOVED, nullptr);
}

void ReceiverConnectionManager::disconnect() {
    LOG_INFO(log_tag_, "Disconnecting...");
    unregister_transmitter();
    set_state(ReceiverConnectionState::LISTENING);
}

// ============================================================================
// QUEUE MANAGEMENT
// ============================================================================

size_t ReceiverConnectionManager::flush_queue() {
    size_t sent_count = 0;
    
    while (!message_queue_.empty() && is_ready_to_send()) {
        QueuedMessage msg;
        if (!message_queue_.peek(msg)) {
            break;
        }
        
        // Try to send
        esp_err_t result = esp_now_send(msg.mac, msg.data, msg.len);
        if (result == ESP_OK) {
            record_send_success();
            message_queue_.pop();
            sent_count++;
        } else {
            // Send failed - stop flushing
            record_send_failure();
            break;
        }
    }
    
    if (sent_count > 0) {
        LOG_DEBUG(log_tag_, "Flushed %u messages from queue", sent_count);
    }
    
    return sent_count;
}

// ============================================================================
// HEALTH & QUALITY
// ============================================================================

bool ReceiverConnectionManager::check_connection_health() {
    // Check success rate
    float success_rate = get_send_success_rate();
    if (success_rate < 70.0f) {
        return false;  // Degraded
    }
    
    // Check last receive time
    uint32_t now = get_current_time_ms();
    if (now - last_receive_time_ > EspNowTiming::HEARTBEAT_DEGRADED_TIMEOUT_MS) {
        return false;  // Degraded
    }
    
    return true;  // Healthy
}

void ReceiverConnectionManager::update_quality_metrics() {
    update_connection_quality();
}

uint32_t ReceiverConnectionManager::get_time_since_last_message() const {
    if (last_receive_time_ == 0) {
        return 0;
    }
    return get_current_time_ms() - last_receive_time_;
}
