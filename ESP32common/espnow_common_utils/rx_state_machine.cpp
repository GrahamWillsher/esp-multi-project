#include "rx_state_machine.h"

#include <logging_config.h>

namespace {

constexpr uint32_t kLockFailWarnCadence = 25;

inline void warn_lock_contention(const char* fn, uint32_t count) {
    if ((count % kLockFailWarnCadence) == 1) {
        LOG_WARN("RX_STATE", "Mutex contention in %s (total=%u)", fn, count);
    }
}

}  // namespace

RxStateMachine& RxStateMachine::instance() {
    static RxStateMachine s;
    return s;
}

bool RxStateMachine::init() {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }
    if (mutex_ == nullptr) {
        LOG_ERROR("RX_STATE", "Failed to create mutex");
        return false;
    }
    return true;
}

void RxStateMachine::on_message_processing(uint8_t msg_type, uint32_t sequence) {
    if (mutex_ == nullptr) return;
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
    if (mutex_ == nullptr) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("on_message_valid", ++lock_failure_count_);
        return;
    }

    message_state_ = MessageState::VALID;
    stats_.valid_messages++;

    xSemaphoreGive(mutex_);
}

void RxStateMachine::on_message_error() {
    if (mutex_ == nullptr) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("on_message_error", ++lock_failure_count_);
        return;
    }

    message_state_ = MessageState::ERROR;
    stats_.error_messages++;

    xSemaphoreGive(mutex_);
}

void RxStateMachine::on_connection_established() {
    if (mutex_ == nullptr) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        warn_lock_contention("on_connection_established", ++lock_failure_count_);
        return;
    }

    connection_state_ = ConnectionState::CONNECTED;
    stats_.last_message_ms = millis();

    xSemaphoreGive(mutex_);
}

void RxStateMachine::on_connection_lost() {
    if (mutex_ == nullptr) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        warn_lock_contention("on_connection_lost", ++lock_failure_count_);
        return;
    }

    connection_state_ = ConnectionState::DISCONNECTED;
    message_state_ = MessageState::IDLE;

    xSemaphoreGive(mutex_);
}

void RxStateMachine::on_activity() {
    if (mutex_ == nullptr) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("on_activity", ++lock_failure_count_);
        return;
    }

    stats_.last_message_ms = millis();
    if (connection_state_ != ConnectionState::ACTIVE) {
        connection_state_ = ConnectionState::ACTIVE;
    }

    xSemaphoreGive(mutex_);
}

void RxStateMachine::check_stale(uint32_t stale_timeout_ms, uint32_t grace_window_ms) {
    if (mutex_ == nullptr) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("check_stale", ++lock_failure_count_);
        return;
    }

    if (connection_state_ == ConnectionState::ACTIVE && stats_.last_message_ms > 0) {
        uint32_t effective_timeout = stale_timeout_ms;
        if (grace_window_ms > 0 && last_config_update_ms_ > 0) {
            const uint32_t age = millis() - last_config_update_ms_;
            if (age < grace_window_ms) {
                effective_timeout = stale_timeout_ms + (grace_window_ms - age);
            }
        }

        const uint32_t sample_age = millis() - stats_.last_message_ms;
        if (sample_age > effective_timeout) {
            connection_state_ = ConnectionState::STALE;
            stats_.stale_transitions++;
        }
    }

    xSemaphoreGive(mutex_);
}

void RxStateMachine::on_config_update_sent() {
    if (mutex_ == nullptr) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("on_config_update_sent", ++lock_failure_count_);
        return;
    }

    last_config_update_ms_ = millis();

    xSemaphoreGive(mutex_);
}

RxStateMachine::ConnectionState RxStateMachine::connection_state() const {
    if (mutex_ == nullptr) return connection_state_;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("connection_state", ++lock_failure_count_);
        return connection_state_;
    }

    const ConnectionState state = connection_state_;
    xSemaphoreGive(mutex_);
    return state;
}

RxStateMachine::MessageState RxStateMachine::message_state() const {
    if (mutex_ == nullptr) return message_state_;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("message_state", ++lock_failure_count_);
        return message_state_;
    }

    const MessageState state = message_state_;
    xSemaphoreGive(mutex_);
    return state;
}

RxStateMachine::Stats RxStateMachine::stats() const {
    if (mutex_ == nullptr) {
        Stats snapshot = stats_;
        snapshot.lock_failures = lock_failure_count_;
        return snapshot;
    }
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        warn_lock_contention("stats", ++lock_failure_count_);
        Stats snapshot = stats_;
        snapshot.lock_failures = lock_failure_count_;
        return snapshot;
    }

    Stats snapshot = stats_;
    snapshot.lock_failures = lock_failure_count_;
    xSemaphoreGive(mutex_);
    return snapshot;
}
