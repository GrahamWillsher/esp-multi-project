#include "transmission_task.h"
#include "enhanced_cache.h"
#include "message_handler.h"
#include "tx_send_guard.h"
#include "../config/logging_config.h"
#include <Arduino.h>
#include <esp32common/espnow/connection_manager.h>
#include <espnow_transmitter.h>

// ============================================================================
// SINGLETON
// ============================================================================

TransmissionTask& TransmissionTask::instance() {
    static TransmissionTask instance;
    return instance;
}

// ============================================================================
// TASK MANAGEMENT
// ============================================================================

void TransmissionTask::start(uint8_t priority, uint8_t core) {
    if (task_handle_ != nullptr) {
        LOG_WARN("TX_TASK", "Task already running");
        return;
    }
    
    LOG_INFO("TX_TASK", "Starting background transmission task (Priority: %d, Core: %d)", 
             priority, core);
    LOG_INFO("TX_TASK", "Rate limit: %dms (%d msg/sec max)", 
             TRANSMIT_INTERVAL_MS, 1000 / TRANSMIT_INTERVAL_MS);
    
    xTaskCreatePinnedToCore(
        task_impl,
        "tx_bg",
        4096,  // Stack size
        this,
        priority,  // Low priority (default: 2)
        &task_handle_,
        core  // Core 1 (isolated from Battery Emulator on Core 0)
    );
    
    if (task_handle_ == nullptr) {
        LOG_ERROR("TX_TASK", "Failed to create task!");
    } else {
        LOG_INFO("TX_TASK", "Task started successfully");
    }
}

void TransmissionTask::stop() {
    if (task_handle_ == nullptr) {
        LOG_WARN("TX_TASK", "Task not running");
        return;
    }
    
    LOG_INFO("TX_TASK", "Stopping transmission task...");
    vTaskDelete(task_handle_);
    task_handle_ = nullptr;
    LOG_INFO("TX_TASK", "Task stopped");
}

// ============================================================================
// TASK IMPLEMENTATION
// ============================================================================

void TransmissionTask::task_impl(void* parameter) {
    TransmissionTask* self = static_cast<TransmissionTask*>(parameter);
    
    LOG_INFO("TX_TASK", "═══ BACKGROUND TRANSMISSION STARTED ═══");
    LOG_INFO("TX_TASK", "Transmitting from EnhancedCache at %dms intervals", TRANSMIT_INTERVAL_MS);
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(TRANSMIT_INTERVAL_MS);
    uint32_t last_cleanup_ms = millis();
    
    while (true) {
        vTaskDelayUntil(&last_wake_time, interval_ticks);
        
        // Only transmit if receiver is connected
        if (!EspNowConnectionManager::instance().is_connected()) {
            static uint32_t last_not_connected_log_ms = 0;
            uint32_t now = millis();
            if (now - last_not_connected_log_ms > 5000) {
                last_not_connected_log_ms = now;
                LOG_WARN("TX_TASK", "Receiver not connected - skipping ESP-NOW transmission");
            }
            continue;
        }
        
        // Priority 1: Transmit transient data (telemetry)
        self->transmit_next_transient();
        
        // Priority 2: Transmit state data (config sync) if no transient pending
        if (EnhancedCache::instance().transient_unsent_count() == 0) {
            self->transmit_next_state();
        }
        
        // Periodic cleanup: Remove ACKed/expired transient entries every 10 seconds
        uint32_t now = millis();
        if (now - last_cleanup_ms >= CLEANUP_INTERVAL_MS) {
            EnhancedCache::instance().cleanup_acked_transient();
            last_cleanup_ms = now;
        }
    }
}

// ============================================================================
// TRANSMISSION HELPERS
// ============================================================================

void TransmissionTask::transmit_next_transient() {
    TransientEntry entry;
    
    // Peek at next unsent transient entry (non-destructive)
    if (!EnhancedCache::instance().peek_next_transient(entry)) {
        LOG_TRACE("TX_TASK", "No transient data to transmit");
        return;  // No unsent data
    }
    
    // Send via ESP-NOW
    const uint8_t* peer_mac = EspNowConnectionManager::instance().get_peer_mac();
    esp_err_t result = TxSendGuard::send_to_receiver_guarded(
        peer_mac,
        (const uint8_t*)&entry.data,
        sizeof(espnow_payload_t),
        "transient"
    );
    
    if (result == ESP_OK) {
        // Mark as sent in cache
        EnhancedCache::instance().mark_transient_sent(entry.seq);
        // Rate-limit success log: report once every 10 s with cumulative send count
        static uint32_t last_transient_log_ms = 0;
        static uint32_t transient_send_count = 0;
        transient_send_count++;
        const uint32_t now_ms = millis();
        if (now_ms - last_transient_log_ms >= 10000) {
            LOG_INFO("TX_TASK", "ESP-NOW TX: SOC=%d%%, Power=%dW (seq:%u) [%u pkts/10s]",
                     entry.data.soc, entry.data.power, entry.seq, transient_send_count);
            last_transient_log_ms = now_ms;
            transient_send_count = 0;
        }
    } else {
        LOG_ERROR("TX_TASK", "Failed to send transient (seq: %u): %s", 
                  entry.seq, esp_err_to_name(result));
        
        // Retry will happen on next iteration (entry stays unsent)
        // If retry limit exceeded, background task will eventually drop it via cleanup
    }
}

void TransmissionTask::transmit_next_state() {
    // Check if any state has unsent updates
    if (!EnhancedCache::instance().has_unsent_state()) {
        LOG_TRACE("TX_TASK", "No state data to transmit");
        return;
    }
    
    // Try each state type (network, MQTT, battery)
    CacheDataType state_types[] = {
        CacheDataType::STATE_NETWORK,
        CacheDataType::STATE_MQTT,
        CacheDataType::STATE_BATTERY
    };
    
    for (auto type : state_types) {
        StateEntry entry;
        if (!EnhancedCache::instance().get_state(type, entry)) {
            continue;  // No state data for this type
        }
        
        if (entry.sent) {
            continue;  // Already sent
        }
        
        // Prepare CONFIG_CHANGED message
        config_changed_t msg;
        msg.type = msg_config_changed;
        msg.config_type = static_cast<uint8_t>(type);
        msg.version = entry.version;
        msg.timestamp = entry.timestamp;
        
        // Copy config data
        switch (type) {
            case CacheDataType::STATE_NETWORK:
                memcpy(&msg.data, &entry.config.network, sizeof(network_config_t));
                break;
            case CacheDataType::STATE_MQTT:
                memcpy(&msg.data, &entry.config.mqtt, sizeof(mqtt_config_t));
                break;
            case CacheDataType::STATE_BATTERY:
                memcpy(&msg.data, &entry.config.battery, sizeof(battery_config_t));
                break;
            default:
                continue;
        }
        
        // Send via ESP-NOW
        const uint8_t* peer_mac = EspNowConnectionManager::instance().get_peer_mac();
        esp_err_t result = TxSendGuard::send_to_receiver_guarded(
            peer_mac,
            (const uint8_t*)&msg,
            sizeof(config_changed_t),
            "state_config"
        );
        
        if (result == ESP_OK) {
            // Mark as sent in cache
            EnhancedCache::instance().mark_state_sent(type);
            LOG_DEBUG("TX_TASK", "State config sent (type: %d, version: %d, timestamp: %u)",
                      static_cast<uint8_t>(type), entry.version, entry.timestamp);
        } else {
            LOG_ERROR("TX_TASK", "Failed to send state config (type: %d): %s", 
                      static_cast<uint8_t>(type), esp_err_to_name(result));
        }
        
        // Only send one state update per iteration (rate limiting)
        break;
    }
}
