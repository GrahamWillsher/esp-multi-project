/**
 * @file espnow_send_utils.cpp
 * @brief Implementation of unified ESP-NOW send utilities
 */

#include "espnow_send_utils.h"
#include <mqtt_logger.h>

// Static member initialization
uint8_t EspnowSendUtils::consecutive_failures_ = 0;
bool EspnowSendUtils::send_paused_ = false;
TimerHandle_t EspnowSendUtils::unpause_timer_ = nullptr;
volatile bool EspnowSendUtils::needs_unpause_log_ = false;

bool EspnowSendUtils::send_with_retry(
    const uint8_t* mac,
    const void* data,
    size_t len,
    const char* msg_name,
    uint8_t max_failures,
    uint32_t backoff_ms
) {
    // Skip send if currently in backoff period (DON'T attempt send)
    if (send_paused_) {
        return false;  // Silently skip during backoff
    }
    
    // Attempt to send message
    esp_err_t result = esp_now_send(mac, (const uint8_t*)data, len);
    
    if (result == ESP_OK) {
        // Note: Actual delivery confirmation comes from on_data_sent callback
        // Success here only means the message was queued, not delivered
        return true;
    }
    
    // Handle immediate send failure (peer not found, queue full, etc.)
    consecutive_failures_++;
    
    // Only log first failure and every 5th failure to reduce spam
    if (consecutive_failures_ == 1 || consecutive_failures_ % 5 == 0) {
        MQTT_LOG_WARNING("SEND", "%s failed: %s (failures: %d/%d)", 
                         msg_name, esp_err_to_name(result), consecutive_failures_, max_failures);
    }
    
    // Check if we've hit the failure threshold - use exponential backoff
    if (consecutive_failures_ >= max_failures) {
        // Exponential backoff: 2s, 4s, 8s, 16s, max 30s
        uint32_t pause_duration = backoff_ms << (consecutive_failures_ / max_failures - 1);
        if (pause_duration > 30000) pause_duration = 30000;  // Cap at 30 seconds
        
        MQTT_LOG_ERROR("SEND", "Too many failures (%d) - pausing sends for %u ms", 
                      consecutive_failures_, pause_duration);
        send_paused_ = true;
        
        // Create or restart unpause timer with exponential backoff
        if (unpause_timer_ == nullptr) {
            unpause_timer_ = xTimerCreate(
                "espnow_unpause",
                pdMS_TO_TICKS(pause_duration),
                pdFALSE,  // One-shot timer
                nullptr,
                unpause_callback
            );
        } else {
            // Update timer period for exponential backoff
            xTimerChangePeriod(unpause_timer_, pdMS_TO_TICKS(pause_duration), 0);
        }
        
        if (unpause_timer_ != nullptr) {
            xTimerStart(unpause_timer_, 0);
        } else {
            MQTT_LOG_ERROR("SEND", "Failed to create unpause timer - backoff will not auto-clear");
        }
    }
    
    return false;
}

void EspnowSendUtils::reset_failure_counter() {
    consecutive_failures_ = 0;
    send_paused_ = false;
    
    if (unpause_timer_ != nullptr) {
        xTimerStop(unpause_timer_, 0);
    }
    
    MQTT_LOG_INFO("SEND", "Failure counter reset");
}

uint8_t EspnowSendUtils::get_failure_count() {
    return consecutive_failures_;
}

bool EspnowSendUtils::is_paused() {
    return send_paused_;
}

void EspnowSendUtils::unpause_callback(TimerHandle_t xTimer) {
    send_paused_ = false;
    consecutive_failures_ = 0;
    needs_unpause_log_ = true;  // Set flag for deferred logging
    // Immediate feedback to serial (lightweight, safe in timer callback)
    Serial.println("[SEND] Resuming sends after backoff period");
}

void EspnowSendUtils::handle_deferred_logging() {
    if (needs_unpause_log_) {
        needs_unpause_log_ = false;
        // This runs in a task context with adequate stack for MQTT logging
        MQTT_LOG_INFO("SEND", "Resuming sends after backoff period");
    }
}
