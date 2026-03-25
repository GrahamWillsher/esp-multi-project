#include "rx_state_machine.h"
#include "../config/logging_config.h"

namespace {
// Emit a throttled warning every N lock failures to avoid log flooding.
constexpr uint32_t kLockFailWarnCadence = 25;

inline void warn_lock_contention(const char* fn, uint32_t count) {
    if ((count % kLockFailWarnCadence) == 1) {
        LOG_WARN("RX_STATE", "Mutex contention in %s (total=%u)", fn, count);
    }
}
} // namespace

RxStateMachine& RxStateMachine::instance() {
    static RxStateMachine s;
    return s;
}

bool RxStateMachine::init() {
    if (!mutex_) {
        mutex_ = xSemaphoreCreateMutex();
    }
    if (!mutex_) {
        LOG_ERROR("RX_STATE", "Failed to create mutex");
        return false;
    }
    return true;
}

void RxStateMachine::on_message_processing(uint8_t msg_type, uint32_t sequence) {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("on_message_processing", ++lock_failure_count_);
        return;
    }

    message_state_ = MessageState::PROCESSING;
    last_msg_type_ = msg_type;
    stats_.total_messages++;
    stats_.last_message_seq = sequence;

    xSemaphoreGive(mutex_);
}

void RxStateMachine::on_message_valid() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("on_message_valid", ++lock_failure_count_);
        return;
    }

    message_state_ = MessageState::VALID;
    stats_.valid_messages++;

    xSemaphoreGive(mutex_);
}

void RxStateMachine::on_message_error() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("on_message_error", ++lock_failure_count_);
        return;
    }

    message_state_ = MessageState::ERROR;
    stats_.error_messages++;

    xSemaphoreGive(mutex_);
}

void RxStateMachine::on_connection_established() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        warn_lock_contention("on_connection_established", ++lock_failure_count_);
        return;
    }
    connection_state_ = ConnectionState::CONNECTED;
    stats_.last_message_ms = millis();
    xSemaphoreGive(mutex_);
}

void RxStateMachine::on_connection_lost() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        warn_lock_contention("on_connection_lost", ++lock_failure_count_);
        return;
    }
    connection_state_ = ConnectionState::DISCONNECTED;
    message_state_ = MessageState::IDLE;
    xSemaphoreGive(mutex_);
}

void RxStateMachine::on_activity() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("on_activity", ++lock_failure_count_);
        return;
    }
    stats_.last_message_ms = millis();
    // CONNECTED (link up, no data yet) and STALE both transition to ACTIVE on data arrival
    if (connection_state_ != ConnectionState::ACTIVE) {
        connection_state_ = ConnectionState::ACTIVE;
    }
    xSemaphoreGive(mutex_);
}

void RxStateMachine::check_stale(uint32_t stale_timeout_ms, uint32_t grace_window_ms) {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("check_stale", ++lock_failure_count_);
        return;
    }

    // Only mark stale when data was actually flowing (ACTIVE); skip CONNECTED (link up, no data yet)
    if (connection_state_ == ConnectionState::ACTIVE && stats_.last_message_ms > 0) {
        // Calculate effective timeout: base timeout + grace window for config updates
        uint32_t effective_timeout = stale_timeout_ms;
        if (grace_window_ms > 0 && last_config_update_ms_ > 0) {
            const uint32_t time_since_config = millis() - last_config_update_ms_;
            if (time_since_config < grace_window_ms) {
                // Still within grace window: extend timeout
                effective_timeout = stale_timeout_ms + (grace_window_ms - time_since_config);
            }
        }
        
        const uint32_t age = millis() - stats_.last_message_ms;
        if (age > effective_timeout) {
            connection_state_ = ConnectionState::STALE;
            stats_.stale_transitions++;
        }
    }

    xSemaphoreGive(mutex_);
}

void RxStateMachine::on_config_update_sent() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("on_config_update_sent", ++lock_failure_count_);
        return;
    }
    
    last_config_update_ms_ = millis();
    
    xSemaphoreGive(mutex_);
}

RxStateMachine::ConnectionState RxStateMachine::connection_state() const {
    if (!mutex_) return connection_state_;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("connection_state", ++lock_failure_count_);
        return connection_state_;
    }
    const ConnectionState state = connection_state_;
    xSemaphoreGive(mutex_);
    return state;
}

RxStateMachine::MessageState RxStateMachine::message_state() const {
    if (!mutex_) return message_state_;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("message_state", ++lock_failure_count_);
        return message_state_;
    }
    const MessageState state = message_state_;
    xSemaphoreGive(mutex_);
    return state;
}

RxStateMachine::Stats RxStateMachine::stats() const {
    if (!mutex_) {
        Stats s = stats_;
        s.lock_failures = lock_failure_count_;
        return s;
    }
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("stats", ++lock_failure_count_);
        Stats s = stats_;
        s.lock_failures = lock_failure_count_;
        return s;
    }
    Stats snapshot = stats_;
    snapshot.lock_failures = lock_failure_count_;
    xSemaphoreGive(mutex_);
    return snapshot;
}
