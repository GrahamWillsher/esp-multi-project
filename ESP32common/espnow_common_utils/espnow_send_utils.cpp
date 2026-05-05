/**
 * @file espnow_send_utils.cpp
 * @brief Implementation of unified ESP-NOW send utilities
 */

#include "espnow_send_utils.h"
#include <log_routed.h>

// Static member initialization
uint8_t EspnowSendUtils::consecutive_failures_ = 0;
bool EspnowSendUtils::send_paused_ = false;
TimerHandle_t EspnowSendUtils::unpause_timer_ = nullptr;
volatile bool EspnowSendUtils::needs_unpause_log_ = false;

void EspnowSendUtils::reset_failure_counter() {
    consecutive_failures_ = 0;
    send_paused_ = false;
    
    if (unpause_timer_ != nullptr) {
        xTimerStop(unpause_timer_, 0);
    }
    
    LOG_INFO("SEND", "Failure counter reset");
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
        // Use MQTT-only sink: Serial was already written in the timer callback
        log_routed(LogSink::Mqtt, RoutedLevel::Info, "SEND", "Resuming sends after backoff period");
    }
}
