#include "tx_state_machine.h"
#include "../config/logging_config.h"

namespace {

using State = TxStateMachine::ConnectionState;

struct TransitionRule {
    State from;
    State to;
};

constexpr TransitionRule kTransitionRules[] = {
    {State::DISCONNECTED, State::DISCOVERING},
    {State::DISCONNECTED, State::RECONNECTING},

    {State::DISCOVERING, State::CONNECTED},
    {State::DISCOVERING, State::RECONNECTING},
    {State::DISCOVERING, State::FAILED},
    {State::DISCOVERING, State::DISCONNECTED},

    {State::CONNECTED, State::ACTIVE},
    {State::CONNECTED, State::STALE},
    {State::CONNECTED, State::RECONNECTING},
    {State::CONNECTED, State::DISCONNECTED},

    {State::ACTIVE, State::CONNECTED},
    {State::ACTIVE, State::STALE},
    {State::ACTIVE, State::RECONNECTING},
    {State::ACTIVE, State::DISCONNECTED},

    {State::STALE, State::CONNECTED},
    {State::STALE, State::ACTIVE},
    {State::STALE, State::RECONNECTING},
    {State::STALE, State::DISCONNECTED},

    {State::RECONNECTING, State::DISCOVERING},
    {State::RECONNECTING, State::CONNECTED},
    {State::RECONNECTING, State::FAILED},
    {State::RECONNECTING, State::DISCONNECTED},

    {State::FAILED, State::RECONNECTING},
    {State::FAILED, State::DISCOVERING},
    {State::FAILED, State::DISCONNECTED},
};

bool is_transition_allowed(State from, State to) {
    if (from == to) {
        return true;
    }

    for (const auto& rule : kTransitionRules) {
        if (rule.from == from && rule.to == to) {
            return true;
        }
    }
    return false;
}

} // namespace

TxStateMachine& TxStateMachine::instance() {
    static TxStateMachine s;
    return s;
}

bool TxStateMachine::init() {
    if (!mutex_) {
        mutex_ = xSemaphoreCreateMutex();
    }
    if (!mutex_) {
        LOG_ERROR("TX_STATE", "Failed to create mutex");
        return false;
    }
    return true;
}

void TxStateMachine::set_state(ConnectionState state, const char* reason) {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;

    if (state_ != state) {
        if (!is_transition_allowed(state_, state)) {
            LOG_WARN("TX_STATE", "Rejected transition %s -> %s",
                     espnow_device_state_to_string(state_),
                     espnow_device_state_to_string(state));
            xSemaphoreGive(mutex_);
            return;
        }

        state_ = state;
        stats_.transitions++;
        if (reason) {
            LOG_INFO("TX_STATE", "State -> %s (%s)", espnow_device_state_to_string(state), reason);
        } else {
            LOG_INFO("TX_STATE", "State -> %s", espnow_device_state_to_string(state));
        }
    }

    xSemaphoreGive(mutex_);
}

TxStateMachine::ConnectionState TxStateMachine::state() const {
    if (!mutex_) return state_;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return state_;
    const ConnectionState s = state_;
    xSemaphoreGive(mutex_);
    return s;
}

void TxStateMachine::on_discovery_started() {
    set_state(ConnectionState::DISCOVERING, "discovery started");
}

void TxStateMachine::on_connected(uint8_t channel) {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;

    if (!is_transition_allowed(state_, ConnectionState::CONNECTED)) {
        LOG_WARN("TX_STATE", "Rejected transition %s -> %s",
                 espnow_device_state_to_string(state_),
                 espnow_device_state_to_string(ConnectionState::CONNECTED));
        xSemaphoreGive(mutex_);
        return;
    }

    state_ = ConnectionState::CONNECTED;
    stats_.transitions++;
    stats_.last_known_channel = channel;
    stats_.last_heartbeat_ack_ms = millis();
    reconnect_exp_ = 0;
    xSemaphoreGive(mutex_);
}

void TxStateMachine::on_transmission_started() {
    set_state(ConnectionState::ACTIVE, "request_data received");
}

void TxStateMachine::on_transmission_stopped() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;

    if (state_ == ConnectionState::ACTIVE) {
        state_ = ConnectionState::CONNECTED;
        stats_.transitions++;
    }

    xSemaphoreGive(mutex_);
}

void TxStateMachine::on_connection_lost() {
    set_state(ConnectionState::RECONNECTING, "connection lost");
}

void TxStateMachine::on_heartbeat_ack() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;
    stats_.last_heartbeat_ack_ms = millis();
    xSemaphoreGive(mutex_);
}

bool TxStateMachine::heartbeat_timed_out(uint32_t timeout_ms) const {
    if (!mutex_) return false;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return false;
    const bool timed_out = (stats_.last_heartbeat_ack_ms > 0) &&
                           ((millis() - stats_.last_heartbeat_ack_ms) > timeout_ms);
    xSemaphoreGive(mutex_);
    return timed_out;
}

uint8_t TxStateMachine::last_known_channel() const {
    if (!mutex_) return stats_.last_known_channel;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return stats_.last_known_channel;
    const uint8_t ch = stats_.last_known_channel;
    xSemaphoreGive(mutex_);
    return ch;
}

bool TxStateMachine::is_transmission_active() const {
    return state() == ConnectionState::ACTIVE;
}

uint32_t TxStateMachine::next_backoff_ms() {
    if (!mutex_) return 500;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return 500;

    const uint8_t exp = reconnect_exp_ > 6 ? 6 : reconnect_exp_;
    const uint32_t backoff = 500U * (1U << exp);
    reconnect_exp_ = (reconnect_exp_ < 6) ? (reconnect_exp_ + 1) : reconnect_exp_;
    stats_.reconnect_attempts++;

    xSemaphoreGive(mutex_);
    return backoff;
}

void TxStateMachine::reset_backoff() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;
    reconnect_exp_ = 0;
    xSemaphoreGive(mutex_);
}

TxStateMachine::Stats TxStateMachine::stats() const {
    if (!mutex_) return stats_;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return stats_;
    const Stats snapshot = stats_;
    xSemaphoreGive(mutex_);
    return snapshot;
}
