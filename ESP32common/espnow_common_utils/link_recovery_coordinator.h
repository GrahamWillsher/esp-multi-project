#pragma once

#include "link_truth.h"

#include <cstdint>

namespace esp32common::espnow {

struct LinkRecoveryCoordinatorHooks {
    void* context = nullptr;
    void (*on_radio_deinit)(void* context) = nullptr;
    void (*on_l2_wifi_restarted)(void* context) = nullptr;
    void (*reinstall_send_cb)(void* context) = nullptr;
    void (*restart_device)(void* context) = nullptr;
};

struct LinkRecoveryCoordinatorConfig {
    uint32_t no_mem_trigger_threshold = 10;
    uint32_t httpd_persistent_failure_threshold = 3;
    uint32_t httpd_failure_window_ms = 300000;
    uint8_t l1_budget = 3;
    uint8_t l2_budget = 3;
    uint32_t escalation_reset_window_ms = 30000;
    uint32_t l1_reinit_delay_ms = 100;
    uint32_t l2_stop_delay_ms = 200;
    uint32_t l2_reassoc_delay_ms = 1500;
};

struct LinkRecoveryCoordinatorStats {
    uint8_t l1_attempts = 0;
    uint8_t l2_attempts = 0;
    uint32_t last_recovery_ms = 0;
    uint32_t handled_events = 0;
    uint32_t last_httpd_failure_ms = 0;
    uint32_t last_httpd_errno = 0;
    RecoveryLevel last_level = RecoveryLevel::NONE;
};

class LinkRecoveryCoordinator {
public:
    static LinkRecoveryCoordinator& instance();

    void configure(const LinkRecoveryCoordinatorConfig& config);
    void configure_hooks(const LinkRecoveryCoordinatorHooks& hooks);
    void reset();

    bool handle_no_mem_pressure(uint32_t now_ms,
                                uint32_t consecutive_no_mem,
                                uint32_t trigger_threshold,
                                bool allow_escalation,
                                const uint8_t* peer_mac);

    bool handle_persistent_httpd_failure(uint32_t now_ms,
                                         uint32_t consecutive_failures,
                                         int last_errno,
                                         const uint8_t* peer_mac);

    void recovery_succeeded_transition_to_normal(uint32_t now_ms);

    bool is_recovery_active() const;
    RecoveryLevel current_recovery_level() const { return stats_.last_level; }

    LinkRecoveryCoordinatorStats stats() const { return stats_; }

private:
    LinkRecoveryCoordinator() = default;

    bool run_escalation_sequence(uint32_t now_ms, const uint8_t* peer_mac, bool reset_no_mem_state);

    void restore_post_reinit_peer_state(const uint8_t* peer_mac) const;

    LinkRecoveryCoordinatorConfig config_{};
    LinkRecoveryCoordinatorHooks hooks_{};
    LinkRecoveryCoordinatorStats stats_{};
};

}  // namespace esp32common::espnow
