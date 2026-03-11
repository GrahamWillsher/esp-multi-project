#include "reconnection_backoff.h"
#include <algorithm>
#include <cstdlib>
#include <Arduino.h>

uint32_t ReconnectionBackoff::get_next_delay_ms() {
    return add_jitter(current_delay_ms_);
}

void ReconnectionBackoff::on_retry_attempt() {
    retry_count_++;
    last_attempt_time_ = millis();
    total_backoff_time_ms_ += current_delay_ms_;
    
    // Exponential backoff with cap
    uint32_t new_delay = (uint32_t)(current_delay_ms_ * BACKOFF_MULTIPLIER);
    current_delay_ms_ = std::min(new_delay, MAX_DELAY_MS);
}

void ReconnectionBackoff::on_connection_success() {
    // Reset for next time
    current_delay_ms_ = INITIAL_DELAY_MS;
    retry_count_ = 0;
    last_attempt_time_ = millis();
}

bool ReconnectionBackoff::should_attempt_now() const {
    if (millis() - last_attempt_time_ >= current_delay_ms_) {
        return true;
    }
    return false;
}

void ReconnectionBackoff::reset() {
    current_delay_ms_ = INITIAL_DELAY_MS;
    retry_count_ = 0;
    last_attempt_time_ = millis();
    total_backoff_time_ms_ = 0;
}

uint32_t ReconnectionBackoff::add_jitter(uint32_t base_delay) {
    // Calculate jitter range: ±20%
    uint32_t jitter_range = (base_delay * JITTER_FACTOR);
    
    // Random value between -jitter_range and +jitter_range
    int32_t jitter = (rand() % (2 * jitter_range + 1)) - jitter_range;
    
    // Apply jitter (ensure non-negative)
    int32_t result = base_delay + jitter;
    return (result < 0) ? base_delay : (uint32_t)result;
}
