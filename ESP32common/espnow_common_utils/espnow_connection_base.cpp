/**
 * @file espnow_connection_base.cpp
 * @brief Implementation of base ESP-NOW connection management class
 */

#include "espnow_connection_base.h"
#include <esp_timer.h>
#include <cstring>

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

EspNowConnectionBase::EspNowConnectionBase()
    : state_mutex_(nullptr),
      has_peer_(false),
      current_channel_(0),
      max_history_entries_(EspNowTiming::MAX_STATE_HISTORY_ENTRIES),
      log_tag_("ESPNOW_BASE") {
    
    // Create mutex for thread-safe state access
    state_mutex_ = xSemaphoreCreateMutex();
    if (state_mutex_ == nullptr) {
        LOG_ERROR(log_tag_, "Failed to create state mutex!");
    }
    
    // Initialize peer MAC to zeros
    memset(peer_mac_, 0, sizeof(peer_mac_));
    
    // Reserve space for state history
    state_history_.reserve(max_history_entries_);
    
    LOG_INFO(log_tag_, "ESP-NOW connection base initialized");
}

EspNowConnectionBase::~EspNowConnectionBase() {
    // Clean up mutex
    if (state_mutex_ != nullptr) {
        vSemaphoreDelete(state_mutex_);
        state_mutex_ = nullptr;
    }
    
    LOG_INFO(log_tag_, "ESP-NOW connection base destroyed");
}

// ============================================================================
// THREAD SAFETY
// ============================================================================

bool EspNowConnectionBase::lock_state() {
    if (state_mutex_ != nullptr) {
        return xSemaphoreTake(state_mutex_, portMAX_DELAY) == pdTRUE;
    }
    LOG_WARN(log_tag_, "State mutex is null!");
    return false;
}

void EspNowConnectionBase::unlock_state() {
    if (state_mutex_ != nullptr) {
        xSemaphoreGive(state_mutex_);
    }
}

// ============================================================================
// SAFE SEND OPERATION
// ============================================================================

bool EspNowConnectionBase::safe_send(const uint8_t* mac, const uint8_t* data, size_t len) {
    // Check if ready to send
    if (!is_ready_to_send()) {
        if (EspNowTiming::DEBUG_SEND_OPERATIONS) {
            LOG_WARN(log_tag_, "Not ready to send - state: %s", get_state_string());
        }
        
        // Queue message for later
        return queue_message(mac, data, len);
    }
    
    // Validate parameters
    if (mac == nullptr || data == nullptr || len == 0 || len > EspNowTiming::MAX_ESPNOW_PAYLOAD) {
        LOG_ERROR(log_tag_, "Invalid send parameters");
        record_send_failure();
        return false;
    }
    
    // Attempt send with retry
    uint32_t attempt = 0;
    esp_err_t result = ESP_FAIL;
    
    while (attempt < EspNowTiming::MAX_SEND_RETRIES) {
        result = esp_now_send(mac, data, len);
        
        if (result == ESP_OK) {
            record_send_success();
            if (EspNowTiming::DEBUG_SEND_OPERATIONS) {
                LOG_DEBUG(log_tag_, "Send successful (attempt %u)", attempt + 1);
            }
            trigger_event(EspNowConnectionEvent::SEND_SUCCESS, nullptr);
            return true;
        }
        
        // Send failed - retry with backoff
        attempt++;
        if (attempt < EspNowTiming::MAX_SEND_RETRIES) {
            uint32_t delay = EspNowTiming::calculate_backoff_delay(attempt);
            if (EspNowTiming::DEBUG_SEND_OPERATIONS) {
                LOG_WARN(log_tag_, "Send failed (attempt %u), retrying in %u ms...", 
                         attempt, delay);
            }
            vTaskDelay(pdMS_TO_TICKS(delay));
        }
    }
    
    // All retries exhausted
    LOG_ERROR(log_tag_, "Send failed after %u attempts: %s", 
              EspNowTiming::MAX_SEND_RETRIES, esp_err_to_name(result));
    record_send_failure();
    trigger_event(EspNowConnectionEvent::SEND_FAILED, nullptr);
    
    // Queue message for retry later
    return queue_message(mac, data, len);
}

// ============================================================================
// METRICS & STATISTICS
// ============================================================================

void EspNowConnectionBase::reset_metrics() {
    if (lock_state()) {
        metrics_.reset();
        state_history_.clear();
        unlock_state();
        LOG_INFO(log_tag_, "Metrics reset");
    }
}

float EspNowConnectionBase::get_send_success_rate() const {
    return metrics_.calculate_success_rate();
}

float EspNowConnectionBase::get_connection_quality() const {
    return metrics_.connection_quality;
}

uint32_t EspNowConnectionBase::get_uptime_connected_ms() const {
    return metrics_.get_connection_uptime(get_current_time_ms());
}

void EspNowConnectionBase::record_send_success() {
    if (lock_state()) {
        metrics_.total_sends++;
        metrics_.successful_sends++;
        metrics_.last_send_timestamp = get_current_time_ms();
        metrics_.current_success_rate = metrics_.calculate_success_rate();
        update_connection_quality();
        unlock_state();
    }
}

void EspNowConnectionBase::record_send_failure() {
    if (lock_state()) {
        metrics_.total_sends++;
        metrics_.failed_sends++;
        metrics_.current_success_rate = metrics_.calculate_success_rate();
        update_connection_quality();
        unlock_state();
    }
}

void EspNowConnectionBase::record_receive() {
    if (lock_state()) {
        metrics_.total_receives++;
        metrics_.last_receive_timestamp = get_current_time_ms();
        unlock_state();
    }
}

void EspNowConnectionBase::update_connection_quality() {
    // Quality is primarily based on success rate
    float quality = metrics_.current_success_rate;
    
    // Degrade quality if no recent activity
    uint32_t time_since_send = metrics_.time_since_last_send(get_current_time_ms());
    if (time_since_send > EspNowTiming::HEARTBEAT_DEGRADED_TIMEOUT_MS) {
        quality *= 0.7f;  // 30% penalty for staleness
    }
    
    // Cap at 100%
    if (quality > 100.0f) {
        quality = 100.0f;
    }
    
    metrics_.connection_quality = quality;
}

bool EspNowConnectionBase::is_degraded() const {
    return metrics_.connection_quality < 70.0f;  // Below 70% is degraded
}

// ============================================================================
// STATE HISTORY
// ============================================================================

void EspNowConnectionBase::record_state_change(uint8_t state_code, const char* state_name) {
    if (!lock_state()) {
        return;
    }
    
    // Update duration of previous state
    if (!state_history_.empty()) {
        StateHistoryEntry& prev = state_history_.back();
        prev.duration_ms = get_current_time_ms() - prev.timestamp_ms;
    }
    
    // Add new state entry
    StateHistoryEntry entry(state_code, state_name, get_current_time_ms());
    state_history_.push_back(entry);
    
    // Limit history size
    if (state_history_.size() > max_history_entries_) {
        state_history_.erase(state_history_.begin());
    }
    
    // Update metrics
    metrics_.total_state_changes++;
    metrics_.last_state_change_timestamp = get_current_time_ms();
    
    unlock_state();
    
    if (EspNowTiming::DEBUG_STATE_TRANSITIONS) {
        LOG_INFO(log_tag_, "State changed to: %s", state_name);
    }
    
    trigger_event(EspNowConnectionEvent::STATE_CHANGED, nullptr);
}

// ============================================================================
// EVENT CALLBACKS
// ============================================================================

void EspNowConnectionBase::register_callback(EspNowEventCallback callback) {
    if (lock_state()) {
        callbacks_.push_back(callback);
        unlock_state();
        LOG_INFO(log_tag_, "Event callback registered (total: %u)", callbacks_.size());
    }
}

void EspNowConnectionBase::trigger_event(EspNowConnectionEvent event, void* data) {
    // Call all registered callbacks
    for (const auto& callback : callbacks_) {
        if (callback) {
            callback(event, data);
        }
    }
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================

size_t EspNowConnectionBase::generate_diagnostic_report(char* buffer, size_t buffer_size) const {
    if (buffer == nullptr || buffer_size == 0) {
        return 0;
    }
    
    size_t written = 0;
    
    // Header
    written += snprintf(buffer + written, buffer_size - written,
        "=== ESP-NOW Connection Diagnostics ===\n\n");
    
    // Current state
    written += snprintf(buffer + written, buffer_size - written,
        "Current State: %s\n", get_state_string());
    written += snprintf(buffer + written, buffer_size - written,
        "Connected: %s\n", is_connected() ? "YES" : "NO");
    written += snprintf(buffer + written, buffer_size - written,
        "Ready to Send: %s\n", is_ready_to_send() ? "YES" : "NO");
    written += snprintf(buffer + written, buffer_size - written,
        "Channel: %u\n\n", current_channel_);
    
    // Peer info
    if (has_peer_) {
        written += snprintf(buffer + written, buffer_size - written,
            "Peer MAC: %02X:%02X:%02X:%02X:%02X:%02X\n\n",
            peer_mac_[0], peer_mac_[1], peer_mac_[2], 
            peer_mac_[3], peer_mac_[4], peer_mac_[5]);
    } else {
        written += snprintf(buffer + written, buffer_size - written,
            "Peer: None\n\n");
    }
    
    // Send statistics
    written += snprintf(buffer + written, buffer_size - written,
        "=== Send Statistics ===\n");
    written += snprintf(buffer + written, buffer_size - written,
        "Total Sends: %u\n", metrics_.total_sends);
    written += snprintf(buffer + written, buffer_size - written,
        "Successful: %u\n", metrics_.successful_sends);
    written += snprintf(buffer + written, buffer_size - written,
        "Failed: %u\n", metrics_.failed_sends);
    written += snprintf(buffer + written, buffer_size - written,
        "Success Rate: %.1f%%\n\n", metrics_.current_success_rate);
    
    // Connection quality
    written += snprintf(buffer + written, buffer_size - written,
        "=== Connection Quality ===\n");
    written += snprintf(buffer + written, buffer_size - written,
        "Quality Score: %.1f%%\n", metrics_.connection_quality);
    written += snprintf(buffer + written, buffer_size - written,
        "Status: %s\n", is_degraded() ? "DEGRADED" : "GOOD");
    written += snprintf(buffer + written, buffer_size - written,
        "Uptime: %u ms\n\n", get_uptime_connected_ms());
    
    // State history (last 5 states)
    written += snprintf(buffer + written, buffer_size - written,
        "=== Recent State History ===\n");
    
    size_t history_start = state_history_.size() > 5 ? state_history_.size() - 5 : 0;
    for (size_t i = history_start; i < state_history_.size(); i++) {
        const auto& entry = state_history_[i];
        written += snprintf(buffer + written, buffer_size - written,
            "%u ms: %s (duration: %u ms)\n",
            entry.timestamp_ms, entry.state_name, entry.duration_ms);
    }
    
    written += snprintf(buffer + written, buffer_size - written, "\n");
    
    // Timing configuration summary
    written += snprintf(buffer + written, buffer_size - written,
        "=== Timing Configuration ===\n%s\n",
        EspNowTiming::get_timing_summary());
    
    return written;
}

// ============================================================================
// UTILITY
// ============================================================================

uint32_t EspNowConnectionBase::get_current_time_ms() const {
    return esp_timer_get_time() / 1000;
}
