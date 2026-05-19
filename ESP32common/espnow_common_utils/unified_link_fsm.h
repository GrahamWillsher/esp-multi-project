#pragma once

#include "link_truth.h"

#include <freertos/FreeRTOS.h>
#include <cstdint>

namespace esp32common::espnow {

class UnifiedLinkFsm {
public:
    static UnifiedLinkFsm& instance();

    void init(LinkRole role = LinkRole::Unknown, uint32_t now_ms = 0);
    void set_role(LinkRole role);

    LinkRole role() const;
    LinkTruth snapshot() const;
    LinkPhase phase() const;
    RecoveryLevel recovery_level() const;
    uint16_t session_boot_nonce() const;
    uint16_t session_id() const;
    uint8_t operating_channel() const;
    uint8_t last_good_channel() const;
    bool control_only_mode() const;

    void set_session_boot_nonce(uint16_t session_boot_nonce, uint32_t now_ms = 0);
    void set_session_id(uint16_t session_id, uint32_t now_ms = 0);

    void begin_discovery(uint32_t now_ms = 0);
    void begin_handshake(uint16_t session_boot_nonce,
                         uint16_t session_id,
                         uint8_t operating_channel,
                         uint32_t now_ms = 0);
    void commit_link_up(uint16_t session_boot_nonce,
                        uint16_t session_id,
                        uint8_t operating_channel,
                        bool persist_last_good = true,
                        uint32_t now_ms = 0);
    void enter_degraded(uint32_t now_ms = 0);
    void enter_recovery(RecoveryLevel level, uint32_t now_ms = 0);
    void recovery_succeeded_to_discovery(uint32_t now_ms = 0);
    void mark_restart_required(uint32_t now_ms = 0);

    void set_operating_channel(uint8_t operating_channel, uint32_t now_ms = 0);
    void set_last_good_channel(uint8_t last_good_channel, uint32_t now_ms = 0);
    void set_control_only_mode(bool control_only_mode, uint32_t now_ms = 0);
    void set_no_mem_consecutive(uint32_t consecutive, uint32_t now_ms = 0);
    void note_link_activity(uint32_t now_ms = 0);

private:
    UnifiedLinkFsm() = default;

    void transition_to(LinkPhase phase, RecoveryLevel recovery_level, uint32_t now_ms);
    static uint32_t resolve_now(uint32_t now_ms);

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    LinkRole role_ = LinkRole::Unknown;
    LinkTruth truth_{};
};

}  // namespace esp32common::espnow
