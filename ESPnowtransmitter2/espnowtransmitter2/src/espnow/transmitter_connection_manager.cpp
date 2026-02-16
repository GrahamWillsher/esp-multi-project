/**
 * @file transmitter_connection_manager.cpp
 * @brief Implementation of transmitter ESP-NOW connection state machine
 */

#include "transmitter_connection_manager.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <cstring>

// Static instance
TransmitterConnectionManager* TransmitterConnectionManager::instance_ = nullptr;

// ============================================================================
// SINGLETON
// ============================================================================

TransmitterConnectionManager& TransmitterConnectionManager::instance() {
    if (instance_ == nullptr) {
        instance_ = new TransmitterConnectionManager();
    }
    return *instance_;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

TransmitterConnectionManager::TransmitterConnectionManager()
    : EspNowConnectionBase(),
      current_state_(EspNowConnectionState::UNINITIALIZED),
      discovery_start_time_(0),
      last_probe_time_(0),
      total_discoveries_(0),
      discovery_active_(false),
      channel_lock_start_time_(0),
      target_channel_(0),
      state_enter_time_(0),
      last_heartbeat_time_(0),
      last_receive_time_(0),
      reconnect_start_time_(0),
      reconnect_attempts_(0) {
    
    log_tag_ = "TX_CONN_MGR";
    LOG_INFO(log_tag_, "Transmitter Connection Manager created");
}

TransmitterConnectionManager::~TransmitterConnectionManager() {
    LOG_INFO(log_tag_, "Transmitter Connection Manager destroyed");
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool TransmitterConnectionManager::init() {
    if (current_state_ != EspNowConnectionState::UNINITIALIZED) {
        LOG_WARN(log_tag_, "Already initialized");
        return true;
    }
    
    LOG_INFO(log_tag_, "Initializing transmitter connection manager...");
    set_state(EspNowConnectionState::INITIALIZING);
    
    // ESP-NOW initialization happens externally
    // Move to IDLE state
    set_state(EspNowConnectionState::IDLE);
    
    LOG_INFO(log_tag_, "Initialization complete");
    return true;
}

// ============================================================================
// PURE VIRTUAL METHOD IMPLEMENTATIONS
// ============================================================================

bool TransmitterConnectionManager::is_ready_to_send() const {
    // Can only send when in CONNECTED or DEGRADED states
    return (current_state_ == EspNowConnectionState::CONNECTED ||
            current_state_ == EspNowConnectionState::DEGRADED);
}

bool TransmitterConnectionManager::is_connected() const {
    return (current_state_ == EspNowConnectionState::CONNECTED ||
            current_state_ == EspNowConnectionState::DEGRADED);
}

const char* TransmitterConnectionManager::get_state_string() const {
    switch (current_state_) {
        case EspNowConnectionState::UNINITIALIZED:      return "UNINITIALIZED";
        case EspNowConnectionState::INITIALIZING:       return "INITIALIZING";
        case EspNowConnectionState::IDLE:               return "IDLE";
        case EspNowConnectionState::DISCOVERING:        return "DISCOVERING";
        case EspNowConnectionState::WAITING_FOR_ACK:    return "WAITING_FOR_ACK";
        case EspNowConnectionState::ACK_RECEIVED:       return "ACK_RECEIVED";
        case EspNowConnectionState::CHANNEL_TRANSITION: return "CHANNEL_TRANSITION";
        case EspNowConnectionState::PEER_REGISTRATION:  return "PEER_REGISTRATION";
        case EspNowConnectionState::CHANNEL_STABILIZING:return "CHANNEL_STABILIZING";
        case EspNowConnectionState::CHANNEL_LOCKED:     return "CHANNEL_LOCKED";
        case EspNowConnectionState::CONNECTED:          return "CONNECTED";
        case EspNowConnectionState::DEGRADED:           return "DEGRADED";
        case EspNowConnectionState::DISCONNECTING:      return "DISCONNECTING";
        case EspNowConnectionState::DISCONNECTED:       return "DISCONNECTED";
        case EspNowConnectionState::CONNECTION_LOST:    return "CONNECTION_LOST";
        case EspNowConnectionState::RECONNECTING:       return "RECONNECTING";
        case EspNowConnectionState::ERROR_STATE:        return "ERROR_STATE";
        default:                                        return "UNKNOWN";
    }
}

bool TransmitterConnectionManager::queue_message(const uint8_t* mac, const uint8_t* data, size_t len) {
    return message_queue_.push(mac, data, len);
}

// ============================================================================
// STATE MANAGEMENT
// ============================================================================

void TransmitterConnectionManager::set_state(EspNowConnectionState new_state) {
    if (current_state_ == new_state) {
        return;  // No change
    }
    
    if (lock_state()) {
        EspNowConnectionState old_state = current_state_;
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

void TransmitterConnectionManager::update() {
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

void TransmitterConnectionManager::update_state_machine() {
    switch (current_state_) {
        case EspNowConnectionState::UNINITIALIZED:
            handle_uninitialized();
            break;
        case EspNowConnectionState::INITIALIZING:
            handle_initializing();
            break;
        case EspNowConnectionState::IDLE:
            handle_idle();
            break;
        case EspNowConnectionState::DISCOVERING:
            handle_discovering();
            break;
        case EspNowConnectionState::WAITING_FOR_ACK:
            handle_waiting_for_ack();
            break;
        case EspNowConnectionState::ACK_RECEIVED:
            handle_ack_received();
            break;
        case EspNowConnectionState::CHANNEL_TRANSITION:
            handle_channel_transition();
            break;
        case EspNowConnectionState::PEER_REGISTRATION:
            handle_peer_registration();
            break;
        case EspNowConnectionState::CHANNEL_STABILIZING:
            handle_channel_stabilizing();
            break;
        case EspNowConnectionState::CHANNEL_LOCKED:
            handle_channel_locked();
            break;
        case EspNowConnectionState::CONNECTED:
            handle_connected();
            break;
        case EspNowConnectionState::DEGRADED:
            handle_degraded();
            break;
        case EspNowConnectionState::DISCONNECTING:
            handle_disconnecting();
            break;
        case EspNowConnectionState::DISCONNECTED:
            handle_disconnected();
            break;
        case EspNowConnectionState::CONNECTION_LOST:
            handle_connection_lost();
            break;
        case EspNowConnectionState::RECONNECTING:
            handle_reconnecting();
            break;
        case EspNowConnectionState::ERROR_STATE:
            handle_error_state();
            break;
    }
}

// ============================================================================
// STATE HANDLERS
// ============================================================================

void TransmitterConnectionManager::handle_uninitialized() {
    // Waiting for init() to be called
}

void TransmitterConnectionManager::handle_initializing() {
    // ESP-NOW init happens externally
    // This state transitions immediately to IDLE in init()
}

void TransmitterConnectionManager::handle_idle() {
    // Waiting for discovery to start
    // Discovery started by calling start_discovery()
}

void TransmitterConnectionManager::handle_discovering() {
    uint32_t now = get_current_time_ms();
    
    // Check discovery timeout
    if (now - discovery_start_time_ > EspNowTiming::DISCOVERY_TOTAL_TIMEOUT_MS) {
        LOG_WARN(log_tag_, "Discovery timeout");
        stop_discovery();
        set_state(EspNowConnectionState::IDLE);
        return;
    }
    
    // Broadcast PROBE periodically (handled by discovery_task.cpp)
    // This state just monitors timeout
}

void TransmitterConnectionManager::handle_waiting_for_ack() {
    uint32_t now = get_current_time_ms();
    
    // Check ACK timeout
    if (now - last_probe_time_ > EspNowTiming::ACK_WAIT_TIMEOUT_MS) {
        LOG_WARN(log_tag_, "ACK timeout, resuming discovery");
        set_state(EspNowConnectionState::DISCOVERING);
    }
}

void TransmitterConnectionManager::handle_ack_received() {
    // Immediately start channel lock sequence
    set_state(EspNowConnectionState::CHANNEL_TRANSITION);
}

void TransmitterConnectionManager::handle_channel_transition() {
    uint32_t now = get_current_time_ms();
    uint32_t elapsed = now - state_enter_time_;
    
    // Wait for channel transition delay
    if (elapsed >= EspNowTiming::CHANNEL_TRANSITION_DELAY_MS) {
        LOG_INFO(log_tag_, "Channel transition complete");
        set_state(EspNowConnectionState::PEER_REGISTRATION);
    }
}

void TransmitterConnectionManager::handle_peer_registration() {
    uint32_t now = get_current_time_ms();
    uint32_t elapsed = now - state_enter_time_;
    
    // Register peer (if not already done)
    if (!has_peer_) {
        if (!register_peer()) {
            LOG_ERROR(log_tag_, "Failed to register peer");
            set_state(EspNowConnectionState::ERROR_STATE);
            return;
        }
    }
    
    // Wait for registration delay
    if (elapsed >= EspNowTiming::PEER_REGISTRATION_DELAY_MS) {
        LOG_INFO(log_tag_, "Peer registration complete");
        set_state(EspNowConnectionState::CHANNEL_STABILIZING);
    }
}

void TransmitterConnectionManager::handle_channel_stabilizing() {
    uint32_t now = get_current_time_ms();
    uint32_t elapsed = now - state_enter_time_;
    
    // Wait for channel to stabilize
    if (elapsed >= EspNowTiming::CHANNEL_STABILIZING_DELAY_MS) {
        LOG_INFO(log_tag_, "Channel stabilized");
        set_state(EspNowConnectionState::CHANNEL_LOCKED);
    }
}

void TransmitterConnectionManager::handle_channel_locked() {
    // Channel is now locked and stable
    // Move to CONNECTED state
    LOG_INFO(log_tag_, "Channel locked, connection established");
    metrics_.connection_established_timestamp = get_current_time_ms();
    metrics_.total_connects++;
    total_discoveries_++;
    discovery_active_ = false;
    
    trigger_event(EspNowConnectionEvent::CONNECTED, nullptr);
    set_state(EspNowConnectionState::CONNECTED);
}

void TransmitterConnectionManager::handle_connected() {
    // Check connection health
    if (!check_connection_health()) {
        LOG_WARN(log_tag_, "Connection degraded");
        set_state(EspNowConnectionState::DEGRADED);
        trigger_event(EspNowConnectionEvent::DEGRADED, nullptr);
        return;
    }
    
    // Send heartbeat periodically
    uint32_t now = get_current_time_ms();
    if (now - last_heartbeat_time_ > EspNowTiming::HEARTBEAT_INTERVAL_MS) {
        // Heartbeat sending handled externally (keep_alive_manager.cpp)
        last_heartbeat_time_ = now;
    }
}

void TransmitterConnectionManager::handle_degraded() {
    // Check if connection recovered
    if (check_connection_health()) {
        LOG_INFO(log_tag_, "Connection recovered");
        set_state(EspNowConnectionState::CONNECTED);
        return;
    }
    
    // Check if connection completely lost
    uint32_t now = get_current_time_ms();
    if (now - last_receive_time_ > EspNowTiming::HEARTBEAT_CRITICAL_TIMEOUT_MS) {
        LOG_ERROR(log_tag_, "Connection lost (critical timeout)");
        set_state(EspNowConnectionState::CONNECTION_LOST);
        trigger_event(EspNowConnectionEvent::DISCONNECTED, nullptr);
    }
}

void TransmitterConnectionManager::handle_disconnecting() {
    // Clean up peer
    unregister_peer();
    set_state(EspNowConnectionState::DISCONNECTED);
}

void TransmitterConnectionManager::handle_disconnected() {
    // Waiting for reconnect or new discovery
}

void TransmitterConnectionManager::handle_connection_lost() {
    // Connection lost unexpectedly
    metrics_.total_disconnects++;
    unregister_peer();
    
    // Attempt reconnection
    set_state(EspNowConnectionState::RECONNECTING);
}

void TransmitterConnectionManager::handle_reconnecting() {
    uint32_t now = get_current_time_ms();
    
    // Start discovery for reconnection
    if (!discovery_active_) {
        LOG_INFO(log_tag_, "Starting reconnection discovery...");
        reconnect_start_time_ = now;
        reconnect_attempts_++;
        start_discovery();
    }
    
    // Check reconnect timeout
    if (now - reconnect_start_time_ > EspNowTiming::RECONNECT_MAX_DELAY_MS) {
        LOG_WARN(log_tag_, "Reconnection timeout");
        stop_discovery();
        set_state(EspNowConnectionState::DISCONNECTED);
    }
}

void TransmitterConnectionManager::handle_error_state() {
    // Unrecoverable error - manual intervention required
    LOG_ERROR(log_tag_, "In ERROR_STATE - manual reset required");
}

// ============================================================================
// DISCOVERY CONTROL
// ============================================================================

bool TransmitterConnectionManager::start_discovery() {
    if (discovery_active_) {
        LOG_WARN(log_tag_, "Discovery already active");
        return false;
    }
    
    LOG_INFO(log_tag_, "Starting discovery...");
    discovery_active_ = true;
    discovery_start_time_ = get_current_time_ms();
    last_probe_time_ = 0;
    
    set_state(EspNowConnectionState::DISCOVERING);
    trigger_event(EspNowConnectionEvent::DISCOVERY_STARTED, nullptr);
    
    return true;
}

void TransmitterConnectionManager::stop_discovery() {
    if (!discovery_active_) {
        return;
    }
    
    LOG_INFO(log_tag_, "Stopping discovery");
    discovery_active_ = false;
    trigger_event(EspNowConnectionEvent::DISCOVERY_COMPLETE, nullptr);
}

bool TransmitterConnectionManager::is_discovering() const {
    return (current_state_ == EspNowConnectionState::DISCOVERING ||
            current_state_ == EspNowConnectionState::WAITING_FOR_ACK);
}

bool TransmitterConnectionManager::is_channel_locking() const {
    return (current_state_ == EspNowConnectionState::CHANNEL_TRANSITION ||
            current_state_ == EspNowConnectionState::PEER_REGISTRATION ||
            current_state_ == EspNowConnectionState::CHANNEL_STABILIZING ||
            current_state_ == EspNowConnectionState::CHANNEL_LOCKED);
}

// ============================================================================
// ACK HANDLING
// ============================================================================

bool TransmitterConnectionManager::handle_ack_received(const uint8_t* receiver_mac, uint8_t channel) {
    if (current_state_ != EspNowConnectionState::WAITING_FOR_ACK &&
        current_state_ != EspNowConnectionState::DISCOVERING) {
        LOG_WARN(log_tag_, "ACK received in wrong state: %s", get_state_string());
        return false;
    }
    
    // Validate parameters
    if (receiver_mac == nullptr || !EspNowTiming::is_valid_channel(channel)) {
        LOG_ERROR(log_tag_, "Invalid ACK parameters");
        return false;
    }
    
    LOG_INFO(log_tag_, "ACK received from receiver on channel %u", channel);
    
    // Save peer info
    memcpy(peer_mac_, receiver_mac, 6);
    has_peer_ = true;
    target_channel_ = channel;
    current_channel_ = channel;
    
    // Stop discovery
    stop_discovery();
    
    // Start channel lock sequence
    set_state(EspNowConnectionState::ACK_RECEIVED);
    channel_lock_start_time_ = get_current_time_ms();
    
    return true;
}

// ============================================================================
// CONNECTION CONTROL
// ============================================================================

void TransmitterConnectionManager::disconnect() {
    LOG_INFO(log_tag_, "Disconnecting...");
    set_state(EspNowConnectionState::DISCONNECTING);
}

void TransmitterConnectionManager::reconnect() {
    LOG_INFO(log_tag_, "Reconnecting...");
    set_state(EspNowConnectionState::RECONNECTING);
}

// ============================================================================
// PEER MANAGEMENT
// ============================================================================

bool TransmitterConnectionManager::register_peer() {
    if (!has_peer_) {
        LOG_ERROR(log_tag_, "Cannot register peer - no peer info");
        return false;
    }
    
    // CRITICAL: Get WiFi home channel and verify peer is registered on that channel
    // ESP-NOW requires all peers to be on the same channel as WiFi
    uint8_t wifi_channel = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&wifi_channel, &secondary);
    
    LOG_INFO(log_tag_, "WiFi home channel: %u (was peer detected on: %u)", wifi_channel, current_channel_);
    
    // Always use WiFi channel for peer registration
    current_channel_ = wifi_channel;
    
    // Add peer to ESP-NOW
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, peer_mac_, 6);
    peer_info.channel = wifi_channel;  // Use WiFi home channel, not discovery channel
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;
    
    esp_err_t result = esp_now_add_peer(&peer_info);
    if (result != ESP_OK) {
        LOG_ERROR(log_tag_, "Failed to add peer: %s", esp_err_to_name(result));
        return false;
    }
    
    LOG_INFO(log_tag_, "Peer registered on WiFi home channel %u", wifi_channel);
    trigger_event(EspNowConnectionEvent::PEER_REGISTERED, nullptr);
    return true;
}

void TransmitterConnectionManager::unregister_peer() {
    if (!has_peer_) {
        return;
    }
    
    esp_now_del_peer(peer_mac_);
    has_peer_ = false;
    memset(peer_mac_, 0, 6);
    current_channel_ = 0;
    
    LOG_INFO(log_tag_, "Peer unregistered");
    trigger_event(EspNowConnectionEvent::PEER_REMOVED, nullptr);
}

// ============================================================================
// QUEUE MANAGEMENT
// ============================================================================

size_t TransmitterConnectionManager::flush_queue() {
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

bool TransmitterConnectionManager::check_connection_health() {
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

void TransmitterConnectionManager::update_quality_metrics() {
    update_connection_quality();
}
