/**
 * @file connection_manager.cpp
 * @brief ESP-NOW Connection Manager Implementation
 * 
 * Generic 3-state state machine implementation.
 * Works identically for both transmitter and receiver.
 */

#include "connection_manager.h"
#include <Arduino.h>
#include <logging_config.h>

// Global event queue (created on init)
QueueHandle_t g_connection_event_queue = nullptr;

// Singleton instance
static EspNowConnectionManager* g_manager = nullptr;

EspNowConnectionManager& EspNowConnectionManager::instance() {
    if (g_manager == nullptr) {
        g_manager = new EspNowConnectionManager();
    }
    return *g_manager;
}

EspNowConnectionManager::EspNowConnectionManager()
    : current_state_(EspNowConnectionState::IDLE),
      state_enter_time_(0),
      event_queue_(nullptr),
      auto_reconnect_enabled_(false),
      connecting_timeout_ms_(0),
            heartbeat_timeout_enabled_(false),
            heartbeat_timeout_ms_(5000),
            heartbeat_timeout_reported_(false),
      heartbeat_monitor_(5000),  // 5-second timeout
      backoff_manager_() {
    memset(peer_mac_, 0, 6);
}

bool EspNowConnectionManager::init() {
    LOG_INFO("CONN_MGR", "Initializing ESP-NOW Connection Manager");
    
    // Create event queue
    g_connection_event_queue = xQueueCreate(10, sizeof(EspNowStateChange));
    
    if (g_connection_event_queue == nullptr) {
        LOG_ERROR("CONN_MGR", "Failed to create event queue!");
        return false;
    }
    
    event_queue_ = g_connection_event_queue;
    current_state_ = EspNowConnectionState::IDLE;
    state_enter_time_ = millis();
    auto_reconnect_enabled_ = false;
    connecting_timeout_ms_ = 0;
    heartbeat_timeout_enabled_ = false;
    heartbeat_timeout_ms_ = 5000;
    heartbeat_timeout_reported_ = false;
    
    LOG_INFO("CONN_MGR", "Connection Manager initialized");
    LOG_INFO("CONN_MGR", "State: IDLE");
    LOG_INFO("CONN_MGR", "Event queue: created (10 events)");
    
    return true;
}

void EspNowConnectionManager::register_state_callback(StateChangeCallback callback) {
    state_callbacks_.push_back(callback);
    LOG_INFO("CONN_MGR", "Registered state change callback (total: %d)", state_callbacks_.size());
}

void EspNowConnectionManager::set_auto_reconnect(bool enable) {
    auto_reconnect_enabled_ = enable;
    LOG_INFO("CONN_MGR", "Auto-reconnect: %s", enable ? "ENABLED" : "DISABLED");
}

void EspNowConnectionManager::set_connecting_timeout_ms(uint32_t timeout_ms) {
    connecting_timeout_ms_ = timeout_ms;
    LOG_INFO("CONN_MGR", "CONNECTING timeout: %ldms", timeout_ms);
}

void EspNowConnectionManager::set_heartbeat_timeout_enabled(bool enable) {
    heartbeat_timeout_enabled_ = enable;
    heartbeat_timeout_reported_ = false;
    LOG_INFO("CONN_MGR", "Heartbeat timeout ownership: %s", enable ? "ENABLED" : "DISABLED");
}

void EspNowConnectionManager::set_heartbeat_timeout_ms(uint32_t timeout_ms) {
    heartbeat_timeout_ms_ = timeout_ms;
    heartbeat_monitor_ = EspNowHeartbeatMonitor(timeout_ms);
    heartbeat_timeout_reported_ = false;
    LOG_INFO("CONN_MGR", "Heartbeat timeout threshold: %ldms", timeout_ms);
}

bool EspNowConnectionManager::post_event(EspNowEvent event, const uint8_t* mac) {
    if (event_queue_ == nullptr) {
        return false;  // Not initialized
    }
    
    EspNowStateChange change(event, mac);
    change.timestamp = millis();
    
    BaseType_t result = xQueueSend(event_queue_, &change, pdMS_TO_TICKS(100));
    return result == pdTRUE;
}

void EspNowConnectionManager::process_events() {
    EspNowStateChange event;
    
    // Process all pending events
    while (xQueueReceive(event_queue_, &event, 0) == pdTRUE) {
        LOG_DEBUG("CONN_MGR", "Processing event: %s (state: %s)",
                  event_to_string(event.event),
                  espnow_state_to_string(current_state_));
        
        handle_event(event);
    }
    
    // Check for CONNECTING timeout
    if (current_state_ == EspNowConnectionState::CONNECTING && 
        connecting_timeout_ms_ > 0 && 
        get_state_time_ms() > connecting_timeout_ms_) {
        LOG_WARN("CONN_MGR", "CONNECTING timeout (%ldms) exceeded -> IDLE", connecting_timeout_ms_);
        transition_to_state(EspNowConnectionState::IDLE);
    }
    
    // Common heartbeat/activity timeout ownership (configurable per device)
    if (heartbeat_timeout_enabled_ &&
        current_state_ == EspNowConnectionState::CONNECTED &&
        heartbeat_monitor_.connection_lost() &&
        !heartbeat_timeout_reported_) {
        LOG_WARN("CONN_MGR", "Heartbeat/activity timeout (%ldms since last) -> CONNECTION_LOST",
                 heartbeat_monitor_.ms_since_last_heartbeat());
        heartbeat_timeouts_++;
        heartbeat_timeout_reported_ = true;
        post_event(EspNowEvent::CONNECTION_LOST);
    }
}

void EspNowConnectionManager::handle_event(const EspNowStateChange& event) {
    switch (current_state_) {
        case EspNowConnectionState::IDLE:
            handle_idle_event(event);
            break;
            
        case EspNowConnectionState::CONNECTING:
            handle_connecting_event(event);
            break;
            
        case EspNowConnectionState::CONNECTED:
            handle_connected_event(event);
            break;
    }
}

void EspNowConnectionManager::handle_idle_event(const EspNowStateChange& event) {
    switch (event.event) {
        case EspNowEvent::CONNECTION_START:
            LOG_INFO("CONN_MGR", "CONNECTION_START -> Transitioning to CONNECTING");
            transition_to_state(EspNowConnectionState::CONNECTING);
            break;
            
        case EspNowEvent::PEER_FOUND:
            // Can go directly from IDLE to CONNECTING if peer appears
            LOG_INFO("CONN_MGR", "PEER_FOUND (in IDLE) -> Transitioning to CONNECTING");
            memcpy(peer_mac_, event.peer_mac, 6);
            transition_to_state(EspNowConnectionState::CONNECTING);
            break;
            
        case EspNowEvent::RESET_CONNECTION:
        case EspNowEvent::CONNECTION_LOST:
            // Already idle, ignore
            break;
            
        default:
            LOG_WARN("CONN_MGR", "Unexpected event in IDLE: %s", event_to_string(event.event));
            break;
    }
}

void EspNowConnectionManager::handle_connecting_event(const EspNowStateChange& event) {
    switch (event.event) {
        case EspNowEvent::PEER_FOUND:
            LOG_INFO("CONN_MGR", "PEER_FOUND -> Waiting for peer registration");
            memcpy(peer_mac_, event.peer_mac, 6);
            break;
            
        case EspNowEvent::PEER_REGISTERED:
            LOG_INFO("CONN_MGR", "PEER_REGISTERED -> Transitioning to CONNECTED");
            memcpy(peer_mac_, event.peer_mac, 6);
            transition_to_state(EspNowConnectionState::CONNECTED);
            break;
            
        case EspNowEvent::CONNECTION_LOST:
        case EspNowEvent::RESET_CONNECTION:
            LOG_WARN("CONN_MGR", "Connection reset/lost -> Back to IDLE");
            transition_to_state(EspNowConnectionState::IDLE);
            break;
            
        default:
            LOG_WARN("CONN_MGR", "Unexpected event in CONNECTING: %s", event_to_string(event.event));
            break;
    }
}

void EspNowConnectionManager::handle_connected_event(const EspNowStateChange& event) {
    switch (event.event) {
        case EspNowEvent::DATA_RECEIVED:
            LOG_DEBUG("CONN_MGR", "DATA_RECEIVED (remaining connected)");
            break;
            
        case EspNowEvent::CONNECTION_LOST:
            LOG_WARN("CONN_MGR", "CONNECTION_LOST -> Back to IDLE");
            transition_to_state(EspNowConnectionState::IDLE);
            break;
            
        case EspNowEvent::RESET_CONNECTION:
            LOG_INFO("CONN_MGR", "RESET_CONNECTION -> Back to IDLE");
            transition_to_state(EspNowConnectionState::IDLE);
            break;
            
        case EspNowEvent::CONNECTION_START:
        case EspNowEvent::PEER_FOUND:
            LOG_DEBUG("CONN_MGR", "Already connected, ignoring %s event", event_to_string(event.event));
            break;
            
        default:
            LOG_WARN("CONN_MGR", "Unexpected event in CONNECTED: %s", event_to_string(event.event));
            break;
    }
}

void EspNowConnectionManager::transition_to_state(EspNowConnectionState new_state) {
    if (new_state == current_state_) {
        return;  // No change
    }
    
    uint32_t state_duration = millis() - state_enter_time_;
    EspNowConnectionState old_state = current_state_;
    
    LOG_INFO("CONN_MGR", "State transition: %s -> %s (duration: %ldms)",
             espnow_state_to_string(current_state_),
             espnow_state_to_string(new_state),
             state_duration);
    
    current_state_ = new_state;
    state_enter_time_ = millis();
    
    // PHASE 0: Update metrics on state transitions
    if (old_state == EspNowConnectionState::CONNECTED) {
        total_connected_time_ms_ += state_duration;
        disconnections_++;
    }
    
    // Additional logging for specific transitions
    if (new_state == EspNowConnectionState::IDLE) {
        memset(peer_mac_, 0, 6);
        LOG_INFO("CONN_MGR", "Peer MAC cleared");
        heartbeat_timeout_reported_ = false;
        
        // Auto-reconnect if enabled
        if (auto_reconnect_enabled_ && old_state == EspNowConnectionState::CONNECTED) {
            LOG_INFO("CONN_MGR", "Auto-reconnect enabled -> posting CONNECTION_START");
            post_event(EspNowEvent::CONNECTION_START);
        }
    } else if (new_state == EspNowConnectionState::CONNECTED) {
        LOG_INFO("CONN_MGR", "Peer: %02X:%02X:%02X:%02X:%02X:%02X",
                 peer_mac_[0], peer_mac_[1], peer_mac_[2],
                 peer_mac_[3], peer_mac_[4], peer_mac_[5]);
        
        // PHASE 0: Initialize heartbeat monitoring
        heartbeat_monitor_.on_connection_success();
        heartbeat_timeout_reported_ = false;
        backoff_manager_.on_connection_success();
        successful_connections_++;
    }
    
    // Invoke registered callbacks
    for (const auto& callback : state_callbacks_) {
        callback(old_state, new_state);
    }
}

uint32_t EspNowConnectionManager::get_connected_time_ms() const {
    if (current_state_ != EspNowConnectionState::CONNECTED) {
        return 0;
    }
    return millis() - state_enter_time_;
}

uint32_t EspNowConnectionManager::get_state_time_ms() const {
    return millis() - state_enter_time_;
}

// ===== PHASE 0: HEARTBEAT & RECONNECTION METHODS =====

void EspNowConnectionManager::on_heartbeat_received() {
    heartbeat_monitor_.on_heartbeat_received();
    heartbeat_timeout_reported_ = false;
    heartbeats_received_++;
}

bool EspNowConnectionManager::is_heartbeat_timeout() const {
    return heartbeat_monitor_.connection_lost();
}

uint32_t EspNowConnectionManager::ms_since_last_heartbeat() const {
    return heartbeat_monitor_.ms_since_last_heartbeat();
}

bool EspNowConnectionManager::should_attempt_reconnect() const {
    return backoff_manager_.should_attempt_now();
}

void EspNowConnectionManager::on_reconnect_attempt() {
    backoff_manager_.on_retry_attempt();
    reconnect_attempts_++;
}

EspNowConnectionMetrics EspNowConnectionManager::get_metrics() const {
    EspNowConnectionMetrics metrics;
    metrics.successful_connections = successful_connections_;
    metrics.disconnections = disconnections_;
    metrics.reconnect_attempts = reconnect_attempts_;
    metrics.heartbeats_received = heartbeats_received_;
    metrics.heartbeat_timeouts = heartbeat_timeouts_;
    metrics.total_connected_time_ms = total_connected_time_ms_;
    
    // Add current connection duration if connected
    if (current_state_ == EspNowConnectionState::CONNECTED) {
        metrics.total_connected_time_ms += get_connected_time_ms();
    }
    
    return metrics;
}
