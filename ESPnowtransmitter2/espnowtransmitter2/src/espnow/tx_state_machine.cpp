#include "tx_state_machine.h"

#include "../config/logging_config.h"

#include <esp32common/espnow/channel_authority.h>
#include <esp32common/espnow/link_truth.h>
#include <esp32common/espnow/unified_link_fsm.h>

namespace {

using ConnectionState = TxStateMachine::ConnectionState;
using esp32common::espnow::ChannelAuthority;
using esp32common::espnow::LinkPhase;
using esp32common::espnow::UnifiedLinkFsm;

}  // namespace

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
    last_reported_state_ = derive_state_locked();
    stats_.last_known_channel = ChannelAuthority::instance().last_good_channel();
    return true;
}

void TxStateMachine::set_state(ConnectionState state, const char* reason) {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;

    switch (state) {
        case ConnectionState::DISCONNECTED:
            transmission_active_ = false;
            UnifiedLinkFsm::instance().enter_degraded();
            break;
        case ConnectionState::DISCOVERING:
            transmission_active_ = false;
            UnifiedLinkFsm::instance().begin_discovery();
            break;
        case ConnectionState::CONNECTED:
            transmission_active_ = false;
            if (ChannelAuthority::instance().operating_channel() > 0) {
                UnifiedLinkFsm::instance().commit_link_up(
                    UnifiedLinkFsm::instance().session_boot_nonce(),
                    UnifiedLinkFsm::instance().session_id(),
                    ChannelAuthority::instance().operating_channel());
            }
            break;
        case ConnectionState::ACTIVE:
            transmission_active_ = true;
            break;
        case ConnectionState::STALE:
            UnifiedLinkFsm::instance().enter_degraded();
            break;
        case ConnectionState::RECONNECTING:
            transmission_active_ = false;
            UnifiedLinkFsm::instance().begin_discovery();
            break;
        case ConnectionState::FAILED:
            transmission_active_ = false;
            UnifiedLinkFsm::instance().mark_restart_required();
            break;
    }

    note_transition_locked(derive_state_locked());
    if (reason) {
        LOG_INFO("TX_STATE", "State -> %s (%s)", espnow_device_state_to_string(last_reported_state_), reason);
    }
    xSemaphoreGive(mutex_);
}

TxStateMachine::ConnectionState TxStateMachine::derive_state_locked() const {
    const auto truth = UnifiedLinkFsm::instance().snapshot();
    switch (truth.phase) {
        case LinkPhase::BOOTSTRAP:
            return ConnectionState::DISCONNECTED;
        case LinkPhase::DISCOVERY:
            return ConnectionState::DISCOVERING;
        case LinkPhase::HANDSHAKE:
            return ConnectionState::RECONNECTING;
        case LinkPhase::LINK_UP:
            return transmission_active_ ? ConnectionState::ACTIVE : ConnectionState::CONNECTED;
        case LinkPhase::DEGRADED:
            return ConnectionState::STALE;
        case LinkPhase::RECOVERY_L1:
        case LinkPhase::RECOVERY_L2:
            return ConnectionState::RECONNECTING;
        case LinkPhase::RESTART_REQUIRED:
            return ConnectionState::FAILED;
    }
    return ConnectionState::DISCONNECTED;
}

void TxStateMachine::note_transition_locked(ConnectionState next_state) {
    if (last_reported_state_ != next_state) {
        last_reported_state_ = next_state;
        ++stats_.transitions;
    }
    stats_.last_known_channel = ChannelAuthority::instance().last_good_channel();
}

TxStateMachine::ConnectionState TxStateMachine::state() const {
    if (!mutex_) {
        return ConnectionState::DISCONNECTED;
    }
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        return last_reported_state_;
    }
    const ConnectionState current = derive_state_locked();
    xSemaphoreGive(mutex_);
    return current;
}

void TxStateMachine::on_discovery_started() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;
    transmission_active_ = false;
    UnifiedLinkFsm::instance().begin_discovery();
    note_transition_locked(derive_state_locked());
    xSemaphoreGive(mutex_);
}

void TxStateMachine::on_connected(uint8_t channel) {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;

    transmission_active_ = false;
    stats_.last_known_channel = channel;
    stats_.last_heartbeat_ack_ms = millis();
    reconnect_exp_ = 0;
    ChannelAuthority::instance().persist_last_good_channel(channel, "TX_STATE");
    if (UnifiedLinkFsm::instance().phase() != LinkPhase::LINK_UP) {
        UnifiedLinkFsm::instance().commit_link_up(
            UnifiedLinkFsm::instance().session_boot_nonce(),
            UnifiedLinkFsm::instance().session_id(),
            channel);
    } else {
        UnifiedLinkFsm::instance().note_link_activity();
    }
    note_transition_locked(derive_state_locked());
    xSemaphoreGive(mutex_);
}

void TxStateMachine::on_transmission_started() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;
    transmission_active_ = true;
    note_transition_locked(derive_state_locked());
    xSemaphoreGive(mutex_);
}

void TxStateMachine::on_transmission_stopped() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;
    transmission_active_ = false;
    note_transition_locked(derive_state_locked());
    xSemaphoreGive(mutex_);
}

void TxStateMachine::on_connection_lost() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;
    transmission_active_ = false;
    UnifiedLinkFsm::instance().enter_degraded();
    note_transition_locked(derive_state_locked());
    xSemaphoreGive(mutex_);
}

void TxStateMachine::on_heartbeat_ack() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;
    stats_.last_heartbeat_ack_ms = millis();
    UnifiedLinkFsm::instance().note_link_activity(stats_.last_heartbeat_ack_ms);
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
    return ChannelAuthority::instance().last_good_channel();
}

bool TxStateMachine::is_transmission_active() const {
    if (!mutex_) return false;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return false;
    const bool active = transmission_active_;
    xSemaphoreGive(mutex_);
    return active;
}

uint32_t TxStateMachine::next_backoff_ms() {
    if (!mutex_) return 500;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return 500;

    const uint8_t exp = reconnect_exp_ > 6 ? 6 : reconnect_exp_;
    const uint32_t backoff = 500U * (1U << exp);
    reconnect_exp_ = (reconnect_exp_ < 6) ? (reconnect_exp_ + 1) : reconnect_exp_;
    ++stats_.reconnect_attempts;

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
    Stats snapshot = stats_;
    snapshot.last_known_channel = ChannelAuthority::instance().last_good_channel();
    xSemaphoreGive(mutex_);
    return snapshot;
}
