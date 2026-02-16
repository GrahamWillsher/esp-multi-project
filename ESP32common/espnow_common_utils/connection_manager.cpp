/**
 * @file connection_manager.cpp
 * @brief ESP-NOW Connection Manager Implementation
 * 
 * Generic 3-state state machine implementation.
 * Works identically for both transmitter and receiver.
 */

#include "connection_manager.h"
#include <Arduino.h>

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
      connecting_timeout_ms_(0) {
    memset(peer_mac_, 0, 6);
}

bool EspNowConnectionManager::init() {
    Serial.printf("[CONN_MGR] Initializing ESP-NOW Connection Manager\n");
    
    // Create event queue
    g_connection_event_queue = xQueueCreate(10, sizeof(EspNowStateChange));
    
    if (g_connection_event_queue == nullptr) {
        Serial.printf("[CONN_MGR] ERROR: Failed to create event queue!\n");
        return false;
    }
    
    event_queue_ = g_connection_event_queue;
    current_state_ = EspNowConnectionState::IDLE;
    state_enter_time_ = millis();
    auto_reconnect_enabled_ = false;
    connecting_timeout_ms_ = 0;
    
    Serial.printf("[CONN_MGR] ✓ Connection Manager initialized\n");
    Serial.printf("[CONN_MGR]   State: IDLE\n");
    Serial.printf("[CONN_MGR]   Event queue: created (10 events)\n");
    
    return true;
}

void EspNowConnectionManager::register_state_callback(StateChangeCallback callback) {
    state_callbacks_.push_back(callback);
    Serial.printf("[CONN_MGR] Registered state change callback (total: %d)\n", state_callbacks_.size());
}

void EspNowConnectionManager::set_auto_reconnect(bool enable) {
    auto_reconnect_enabled_ = enable;
    Serial.printf("[CONN_MGR] Auto-reconnect: %s\n", enable ? "ENABLED" : "DISABLED");
}

void EspNowConnectionManager::set_connecting_timeout_ms(uint32_t timeout_ms) {
    connecting_timeout_ms_ = timeout_ms;
    Serial.printf("[CONN_MGR] CONNECTING timeout: %ldms\n", timeout_ms);
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
        Serial.printf("[CONN_MGR-DEBUG] Processing event: %s (state: %s)\n",
                 event_to_string(event.event),
                 state_to_string(current_state_));
        
        handle_event(event);
    }
    
    // Check for CONNECTING timeout
    if (current_state_ == EspNowConnectionState::CONNECTING && 
        connecting_timeout_ms_ > 0 && 
        get_state_time_ms() > connecting_timeout_ms_) {
        Serial.printf("[CONN_MGR] CONNECTING timeout (%ldms) exceeded → IDLE\n", connecting_timeout_ms_);
        transition_to_state(EspNowConnectionState::IDLE);
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
            Serial.printf("[CONN_MGR] CONNECTION_START → Transitioning to CONNECTING\n");
            transition_to_state(EspNowConnectionState::CONNECTING);
            break;
            
        case EspNowEvent::PEER_FOUND:
            // Can go directly from IDLE to CONNECTING if peer appears
            Serial.printf("[CONN_MGR] PEER_FOUND (in IDLE) → Transitioning to CONNECTING\n");
            memcpy(peer_mac_, event.peer_mac, 6);
            transition_to_state(EspNowConnectionState::CONNECTING);
            break;
            
        case EspNowEvent::RESET_CONNECTION:
        case EspNowEvent::CONNECTION_LOST:
            // Already idle, ignore
            break;
            
        default:
            Serial.printf("[CONN_MGR-WARN] Unexpected event in IDLE: %s\n", event_to_string(event.event));
            break;
    }
}

void EspNowConnectionManager::handle_connecting_event(const EspNowStateChange& event) {
    switch (event.event) {
        case EspNowEvent::PEER_FOUND:
            Serial.printf("[CONN_MGR] PEER_FOUND → Waiting for peer registration\n");
            memcpy(peer_mac_, event.peer_mac, 6);
            break;
            
        case EspNowEvent::PEER_REGISTERED:
            Serial.printf("[CONN_MGR] PEER_REGISTERED → Transitioning to CONNECTED\n");
            memcpy(peer_mac_, event.peer_mac, 6);
            transition_to_state(EspNowConnectionState::CONNECTED);
            break;
            
        case EspNowEvent::CONNECTION_LOST:
        case EspNowEvent::RESET_CONNECTION:
            Serial.printf("[CONN_MGR] Connection reset/lost → Back to IDLE\n");
            transition_to_state(EspNowConnectionState::IDLE);
            break;
            
        default:
            Serial.printf("[CONN_MGR-WARN] Unexpected event in CONNECTING: %s\n", event_to_string(event.event));
            break;
    }
}

void EspNowConnectionManager::handle_connected_event(const EspNowStateChange& event) {
    switch (event.event) {
        case EspNowEvent::DATA_RECEIVED:
            Serial.printf("[CONN_MGR-DEBUG] DATA_RECEIVED (remaining connected)\n");
            break;
            
        case EspNowEvent::CONNECTION_LOST:
            Serial.printf("[CONN_MGR-WARN] CONNECTION_LOST → Back to IDLE\n");
            transition_to_state(EspNowConnectionState::IDLE);
            break;
            
        case EspNowEvent::RESET_CONNECTION:
            Serial.printf("[CONN_MGR] RESET_CONNECTION → Back to IDLE\n");
            transition_to_state(EspNowConnectionState::IDLE);
            break;
            
        case EspNowEvent::CONNECTION_START:
        case EspNowEvent::PEER_FOUND:
            Serial.printf("[CONN_MGR-DEBUG] Already connected, ignoring %s event\n", event_to_string(event.event));
            break;
            
        default:
            Serial.printf("[CONN_MGR-WARN] Unexpected event in CONNECTED: %s\n", event_to_string(event.event));
            break;
    }
}

void EspNowConnectionManager::transition_to_state(EspNowConnectionState new_state) {
    if (new_state == current_state_) {
        return;  // No change
    }
    
    uint32_t state_duration = millis() - state_enter_time_;
    EspNowConnectionState old_state = current_state_;
    
    Serial.printf("[CONN_MGR] ═══ STATE TRANSITION ═══\n");
    Serial.printf("[CONN_MGR]   %s → %s (duration: %ldms)\n",
             state_to_string(current_state_),
             state_to_string(new_state),
             state_duration);
    
    current_state_ = new_state;
    state_enter_time_ = millis();
    
    // Additional logging for specific transitions
    if (new_state == EspNowConnectionState::IDLE) {
        memset(peer_mac_, 0, 6);
        Serial.printf("[CONN_MGR]   Peer MAC cleared\n");
        
        // Auto-reconnect if enabled
        if (auto_reconnect_enabled_ && old_state == EspNowConnectionState::CONNECTED) {
            Serial.printf("[CONN_MGR]   Auto-reconnect enabled → posting CONNECTION_START\n");
            post_event(EspNowEvent::CONNECTION_START);
        }
    } else if (new_state == EspNowConnectionState::CONNECTED) {
        Serial.printf("[CONN_MGR]   Peer: %02X:%02X:%02X:%02X:%02X:%02X\n",
                 peer_mac_[0], peer_mac_[1], peer_mac_[2],
                 peer_mac_[3], peer_mac_[4], peer_mac_[5]);
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
