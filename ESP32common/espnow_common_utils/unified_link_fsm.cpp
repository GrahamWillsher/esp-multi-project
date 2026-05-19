#include "unified_link_fsm.h"

#include <Arduino.h>

namespace esp32common::espnow {

UnifiedLinkFsm& UnifiedLinkFsm::instance() {
    static UnifiedLinkFsm fsm;
    return fsm;
}

void UnifiedLinkFsm::init(LinkRole role, uint32_t now_ms) {
    const uint32_t resolved_now = resolve_now(now_ms);
    portENTER_CRITICAL(&mux_);
    role_ = role;
    truth_ = LinkTruth{};
    truth_.phase = LinkPhase::BOOTSTRAP;
    truth_.phase_enter_ms = resolved_now;
    truth_.last_link_activity_ms = resolved_now;
    truth_.control_only_mode = true;
    truth_.recovery_level = RecoveryLevel::NONE;
    portEXIT_CRITICAL(&mux_);
}

void UnifiedLinkFsm::set_role(LinkRole role) {
    portENTER_CRITICAL(&mux_);
    role_ = role;
    portEXIT_CRITICAL(&mux_);
}

LinkRole UnifiedLinkFsm::role() const {
    portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&mux_));
    const LinkRole role = role_;
    portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&mux_));
    return role;
}

LinkTruth UnifiedLinkFsm::snapshot() const {
    portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&mux_));
    const LinkTruth truth = truth_;
    portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&mux_));
    return truth;
}

LinkPhase UnifiedLinkFsm::phase() const {
    return snapshot().phase;
}

RecoveryLevel UnifiedLinkFsm::recovery_level() const {
    return snapshot().recovery_level;
}

uint16_t UnifiedLinkFsm::session_boot_nonce() const {
    return snapshot().session_boot_nonce;
}

uint16_t UnifiedLinkFsm::session_id() const {
    return snapshot().session_id;
}

uint8_t UnifiedLinkFsm::operating_channel() const {
    return snapshot().operating_channel;
}

uint8_t UnifiedLinkFsm::last_good_channel() const {
    return snapshot().last_good_channel;
}

bool UnifiedLinkFsm::control_only_mode() const {
    return snapshot().control_only_mode;
}

void UnifiedLinkFsm::set_session_boot_nonce(uint16_t session_boot_nonce, uint32_t now_ms) {
    const uint32_t resolved_now = resolve_now(now_ms);
    portENTER_CRITICAL(&mux_);
    truth_.session_boot_nonce = session_boot_nonce;
    truth_.last_link_activity_ms = resolved_now;
    portEXIT_CRITICAL(&mux_);
}

void UnifiedLinkFsm::set_session_id(uint16_t session_id, uint32_t now_ms) {
    const uint32_t resolved_now = resolve_now(now_ms);
    portENTER_CRITICAL(&mux_);
    truth_.session_id = session_id;
    truth_.last_link_activity_ms = resolved_now;
    portEXIT_CRITICAL(&mux_);
}

void UnifiedLinkFsm::begin_discovery(uint32_t now_ms) {
    transition_to(LinkPhase::DISCOVERY, RecoveryLevel::NONE, resolve_now(now_ms));
}

void UnifiedLinkFsm::begin_handshake(uint16_t session_boot_nonce,
                                     uint16_t session_id,
                                     uint8_t operating_channel,
                                     uint32_t now_ms) {
    const uint32_t resolved_now = resolve_now(now_ms);
    portENTER_CRITICAL(&mux_);
    truth_.session_boot_nonce = session_boot_nonce;
    truth_.session_id = session_id;
    if (is_valid_channel(operating_channel)) {
        truth_.operating_channel = operating_channel;
    }
    truth_.last_link_activity_ms = resolved_now;
    portEXIT_CRITICAL(&mux_);
    transition_to(LinkPhase::HANDSHAKE, RecoveryLevel::NONE, resolved_now);
}

void UnifiedLinkFsm::commit_link_up(uint16_t session_boot_nonce,
                                    uint16_t session_id,
                                    uint8_t operating_channel,
                                    bool persist_last_good,
                                    uint32_t now_ms) {
    const uint32_t resolved_now = resolve_now(now_ms);
    portENTER_CRITICAL(&mux_);
    truth_.session_boot_nonce = session_boot_nonce;
    truth_.session_id = session_id;
    if (is_valid_channel(operating_channel)) {
        truth_.operating_channel = operating_channel;
        if (persist_last_good) {
            truth_.last_good_channel = operating_channel;
        }
    }
    truth_.control_only_mode = false;
    truth_.last_link_activity_ms = resolved_now;
    truth_.no_mem_consecutive = 0;
    portEXIT_CRITICAL(&mux_);
    transition_to(LinkPhase::LINK_UP, RecoveryLevel::NONE, resolved_now);
}

void UnifiedLinkFsm::enter_degraded(uint32_t now_ms) {
    transition_to(LinkPhase::DEGRADED, RecoveryLevel::NONE, resolve_now(now_ms));
}

void UnifiedLinkFsm::enter_recovery(RecoveryLevel level, uint32_t now_ms) {
    const LinkPhase phase = (level == RecoveryLevel::L2)
        ? LinkPhase::RECOVERY_L2
        : (level == RecoveryLevel::RESTART ? LinkPhase::RESTART_REQUIRED : LinkPhase::RECOVERY_L1);
    transition_to(phase, level, resolve_now(now_ms));
}

void UnifiedLinkFsm::recovery_succeeded_to_discovery(uint32_t now_ms) {
    transition_to(LinkPhase::DISCOVERY, RecoveryLevel::NONE, resolve_now(now_ms));
}

void UnifiedLinkFsm::mark_restart_required(uint32_t now_ms) {
    transition_to(LinkPhase::RESTART_REQUIRED, RecoveryLevel::RESTART, resolve_now(now_ms));
}

void UnifiedLinkFsm::set_operating_channel(uint8_t operating_channel, uint32_t now_ms) {
    if (!is_valid_channel(operating_channel)) {
        return;
    }
    const uint32_t resolved_now = resolve_now(now_ms);
    portENTER_CRITICAL(&mux_);
    truth_.operating_channel = operating_channel;
    truth_.last_link_activity_ms = resolved_now;
    portEXIT_CRITICAL(&mux_);
}

void UnifiedLinkFsm::set_last_good_channel(uint8_t last_good_channel, uint32_t now_ms) {
    if (!is_valid_channel(last_good_channel)) {
        return;
    }
    const uint32_t resolved_now = resolve_now(now_ms);
    portENTER_CRITICAL(&mux_);
    truth_.last_good_channel = last_good_channel;
    truth_.last_link_activity_ms = resolved_now;
    portEXIT_CRITICAL(&mux_);
}

void UnifiedLinkFsm::set_control_only_mode(bool control_only_mode, uint32_t now_ms) {
    const uint32_t resolved_now = resolve_now(now_ms);
    portENTER_CRITICAL(&mux_);
    truth_.control_only_mode = control_only_mode;
    truth_.last_link_activity_ms = resolved_now;
    portEXIT_CRITICAL(&mux_);
}

void UnifiedLinkFsm::set_no_mem_consecutive(uint32_t consecutive, uint32_t now_ms) {
    const uint32_t resolved_now = resolve_now(now_ms);
    portENTER_CRITICAL(&mux_);
    truth_.no_mem_consecutive = consecutive;
    truth_.last_link_activity_ms = resolved_now;
    portEXIT_CRITICAL(&mux_);
}

void UnifiedLinkFsm::note_link_activity(uint32_t now_ms) {
    const uint32_t resolved_now = resolve_now(now_ms);
    portENTER_CRITICAL(&mux_);
    truth_.last_link_activity_ms = resolved_now;
    portEXIT_CRITICAL(&mux_);
}

void UnifiedLinkFsm::transition_to(LinkPhase phase, RecoveryLevel recovery_level, uint32_t now_ms) {
    portENTER_CRITICAL(&mux_);
    truth_.phase = phase;
    truth_.recovery_level = recovery_level;
    truth_.phase_enter_ms = now_ms;
    truth_.last_link_activity_ms = now_ms;
    if (phase == LinkPhase::LINK_UP) {
        truth_.control_only_mode = false;
    } else if (phase != LinkPhase::BOOTSTRAP) {
        truth_.control_only_mode = true;
    }
    portEXIT_CRITICAL(&mux_);
}

uint32_t UnifiedLinkFsm::resolve_now(uint32_t now_ms) {
    return (now_ms == 0U) ? static_cast<uint32_t>(millis()) : now_ms;
}

}  // namespace esp32common::espnow
