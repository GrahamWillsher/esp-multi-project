#include "keep_alive_manager.h"
#include "message_handler.h"
#include "discovery_task.h"
#include "../config/logging_config.h"
#include <Arduino.h>
#include <espnow_transmitter.h>

// ============================================================================
// SINGLETON
// ============================================================================

KeepAliveManager& KeepAliveManager::instance() {
    static KeepAliveManager instance;
    return instance;
}

// ============================================================================
// TASK MANAGEMENT
// ============================================================================

void KeepAliveManager::start(uint8_t priority, uint8_t core) {
    if (task_handle_ != nullptr) {
        LOG_WARN("[KEEPALIVE] Task already running");
        return;
    }
    
    LOG_INFO("[KEEPALIVE] Starting keep-alive manager (Priority: %d, Core: %d)", priority, core);
    LOG_INFO("[KEEPALIVE] Heartbeat: %ds, Degraded: %ds, Failure: %ds, Disconnect: %ds",
             HEARTBEAT_INTERVAL_MS / 1000,
             DEGRADED_THRESHOLD_MS / 1000,
             FAILURE_THRESHOLD_MS / 1000,
             DISCONNECT_THRESHOLD_MS / 1000);
    
    xTaskCreatePinnedToCore(
        task_impl,
        "keepalive",
        3072,  // Stack size
        this,
        priority,  // Low priority (default: 2)
        &task_handle_,
        core  // Core 1
    );
    
    if (task_handle_ == nullptr) {
        LOG_ERROR("[KEEPALIVE] Failed to create task!");
    } else {
        LOG_INFO("[KEEPALIVE] Task started successfully");
    }
}

void KeepAliveManager::stop() {
    if (task_handle_ == nullptr) {
        LOG_WARN("[KEEPALIVE] Task not running");
        return;
    }
    
    LOG_INFO("[KEEPALIVE] Stopping keep-alive manager...");
    vTaskDelete(task_handle_);
    task_handle_ = nullptr;
    LOG_INFO("[KEEPALIVE] Task stopped");
}

// ============================================================================
// HEARTBEAT TRACKING
// ============================================================================

void KeepAliveManager::record_heartbeat_received() {
    last_heartbeat_received_ = millis();
    
    // Transition to CONNECTED if not already
    if (state_ != ConnectionState::CONNECTED) {
        transition_to(ConnectionState::CONNECTED);
    }
    
    LOG_DEBUG("[KEEPALIVE] Heartbeat received (state: %s)", state_to_string(state_));
}

uint32_t KeepAliveManager::get_time_since_heartbeat() const {
    if (last_heartbeat_received_ == 0) {
        return 0xFFFFFFFF;  // Never received
    }
    return millis() - last_heartbeat_received_;
}

// ============================================================================
// TASK IMPLEMENTATION
// ============================================================================

void KeepAliveManager::task_impl(void* parameter) {
    KeepAliveManager* self = static_cast<KeepAliveManager*>(parameter);
    
    LOG_INFO("[KEEPALIVE] ═══ KEEP-ALIVE MANAGER STARTED ═══");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(1000);  // Check every 1s
    
    while (true) {
        vTaskDelayUntil(&last_wake_time, interval_ticks);
        
        // Only operate when receiver is connected
        if (!EspnowMessageHandler::instance().is_receiver_connected()) {
            LOG_TRACE("[KEEPALIVE] Receiver not connected - skipping");
            continue;
        }
        
        // Send heartbeat at intervals
        uint32_t time_since_sent = millis() - self->last_heartbeat_sent_;
        if (time_since_sent >= HEARTBEAT_INTERVAL_MS) {
            self->send_heartbeat();
        }
        
        // Update connection state based on received heartbeats
        self->update_connection_state();
    }
}

void KeepAliveManager::send_heartbeat() {
    heartbeat_t msg;
    msg.type = msg_heartbeat;
    msg.timestamp = millis();
    msg.seq = last_heartbeat_sent_ / HEARTBEAT_INTERVAL_MS;  // Heartbeat sequence
    
    esp_err_t result = esp_now_send(receiver_mac, (const uint8_t*)&msg, sizeof(msg));
    
    if (result == ESP_OK) {
        last_heartbeat_sent_ = millis();
        LOG_DEBUG("[KEEPALIVE] Heartbeat sent (seq: %u, state: %s)", 
                  msg.seq, state_to_string(state_));
    } else {
        LOG_ERROR("[KEEPALIVE] Failed to send heartbeat: %s", esp_err_to_name(result));
    }
}

void KeepAliveManager::update_connection_state() {
    uint32_t time_since_heartbeat = get_time_since_heartbeat();
    
    // Never received heartbeat - this is normal before initial discovery completes
    // Don't trigger restart, let active channel hopping do its job
    if (last_heartbeat_received_ == 0) {
        if (state_ != ConnectionState::DISCONNECTED) {
            transition_to(ConnectionState::DISCONNECTED);
        }
        // Don't trigger restart - initial discovery is still in progress
        return;
    }
    
    // State machine based on time since last heartbeat
    if (time_since_heartbeat >= DISCONNECT_THRESHOLD_MS) {
        // Total failure - trigger recovery
        if (state_ != ConnectionState::DISCONNECTED) {
            transition_to(ConnectionState::DISCONNECTED);
            
            LOG_ERROR("[KEEPALIVE] Connection lost (no heartbeat for %ds) - triggering recovery",
                      time_since_heartbeat / 1000);
            
            // Only trigger restart if we have a valid channel (discovery previously completed)
            // This prevents restart during initial discovery phase
            extern volatile uint8_t g_lock_channel;
            if (g_lock_channel != 0) {
                LOG_INFO("[KEEPALIVE] Triggering discovery restart (locked channel: %d)", g_lock_channel);
                DiscoveryTask::instance().restart();
            } else {
                LOG_WARN("[KEEPALIVE] Cannot restart - no valid channel yet (initial discovery in progress)");
            }
        }
    } else if (time_since_heartbeat >= FAILURE_THRESHOLD_MS) {
        // Extended timeout (grace period)
        if (state_ != ConnectionState::FAILURE) {
            transition_to(ConnectionState::FAILURE);
            LOG_WARN("[KEEPALIVE] Connection failure (no heartbeat for %ds) - grace period",
                     time_since_heartbeat / 1000);
        }
    } else if (time_since_heartbeat >= DEGRADED_THRESHOLD_MS) {
        // Degraded (warning)
        if (state_ != ConnectionState::DEGRADED) {
            transition_to(ConnectionState::DEGRADED);
            LOG_WARN("[KEEPALIVE] Connection degraded (no heartbeat for %ds)",
                     time_since_heartbeat / 1000);
        }
    } else {
        // Normal operation
        if (state_ != ConnectionState::CONNECTED) {
            transition_to(ConnectionState::CONNECTED);
        }
    }
}

void KeepAliveManager::transition_to(ConnectionState new_state) {
    if (state_ != new_state) {
        LOG_INFO("[KEEPALIVE] State transition: %s → %s", 
                 state_to_string(state_), state_to_string(new_state));
        state_ = new_state;
        state_entry_time_ = millis();
    }
}

const char* KeepAliveManager::state_to_string(ConnectionState state) const {
    switch (state) {
        case ConnectionState::DISCONNECTED: return "DISCONNECTED";
        case ConnectionState::CONNECTED: return "CONNECTED";
        case ConnectionState::DEGRADED: return "DEGRADED";
        case ConnectionState::FAILURE: return "FAILURE";
        default: return "UNKNOWN";
    }
}
