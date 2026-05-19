#pragma once

#include <cstdint>

namespace esp32common::espnow {

enum class LinkRole : uint8_t {
    Unknown = 0,
    TransmitterAuthority,
    ReceiverMirror,
};

enum class LinkPhase : uint8_t {
    BOOTSTRAP = 0,
    DISCOVERY,
    HANDSHAKE,
    LINK_UP,
    DEGRADED,
    RECOVERY_L1,
    RECOVERY_L2,
    RESTART_REQUIRED,
};

enum class RecoveryLevel : uint8_t {
    NONE = 0,
    L1,
    L2,
    RESTART,
};

struct LinkTruth {
    uint16_t session_boot_nonce = 0;
    uint16_t session_id = 0;
    LinkPhase phase = LinkPhase::BOOTSTRAP;
    uint8_t operating_channel = 0;
    uint8_t last_good_channel = 0;
    uint32_t phase_enter_ms = 0;
    uint32_t last_link_activity_ms = 0;
    bool control_only_mode = true;
    RecoveryLevel recovery_level = RecoveryLevel::NONE;
    uint32_t no_mem_consecutive = 0;
};

inline const char* to_string(LinkRole role) {
    switch (role) {
        case LinkRole::Unknown:
            return "Unknown";
        case LinkRole::TransmitterAuthority:
            return "TransmitterAuthority";
        case LinkRole::ReceiverMirror:
            return "ReceiverMirror";
    }
    return "Unknown";
}

inline const char* to_string(LinkPhase phase) {
    switch (phase) {
        case LinkPhase::BOOTSTRAP:
            return "BOOTSTRAP";
        case LinkPhase::DISCOVERY:
            return "DISCOVERY";
        case LinkPhase::HANDSHAKE:
            return "HANDSHAKE";
        case LinkPhase::LINK_UP:
            return "LINK_UP";
        case LinkPhase::DEGRADED:
            return "DEGRADED";
        case LinkPhase::RECOVERY_L1:
            return "RECOVERY_L1";
        case LinkPhase::RECOVERY_L2:
            return "RECOVERY_L2";
        case LinkPhase::RESTART_REQUIRED:
            return "RESTART_REQUIRED";
    }
    return "UNKNOWN";
}

inline const char* to_string(RecoveryLevel level) {
    switch (level) {
        case RecoveryLevel::NONE:
            return "NONE";
        case RecoveryLevel::L1:
            return "L1";
        case RecoveryLevel::L2:
            return "L2";
        case RecoveryLevel::RESTART:
            return "RESTART";
    }
    return "UNKNOWN";
}

inline bool is_valid_channel(uint8_t channel) {
    return channel >= 1 && channel <= 13;
}

}  // namespace esp32common::espnow
