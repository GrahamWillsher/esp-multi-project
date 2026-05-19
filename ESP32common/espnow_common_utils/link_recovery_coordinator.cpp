#include "link_recovery_coordinator.h"

#include "espnow_peer_manager.h"
#include "unified_link_fsm.h"

#include <Arduino.h>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp32common/espnow/mac_utils.h>
#include <esp32common/espnow/tx_scheduler.h>

namespace esp32common::espnow {

LinkRecoveryCoordinator& LinkRecoveryCoordinator::instance() {
    static LinkRecoveryCoordinator coordinator;
    return coordinator;
}

void LinkRecoveryCoordinator::configure(const LinkRecoveryCoordinatorConfig& config) {
    config_ = config;
}

void LinkRecoveryCoordinator::configure_hooks(const LinkRecoveryCoordinatorHooks& hooks) {
    hooks_ = hooks;
}

void LinkRecoveryCoordinator::reset() {
    stats_ = LinkRecoveryCoordinatorStats{};
    UnifiedLinkFsm::instance().set_no_mem_consecutive(0);
}

bool LinkRecoveryCoordinator::is_recovery_active() const {
    return stats_.last_level == RecoveryLevel::L1 ||
           stats_.last_level == RecoveryLevel::L2 ||
           stats_.last_level == RecoveryLevel::RESTART;
}

void LinkRecoveryCoordinator::recovery_succeeded_transition_to_normal(uint32_t now_ms) {
    (void)now_ms;
    stats_.last_level = RecoveryLevel::NONE;
    stats_.l1_attempts = 0;
    stats_.l2_attempts = 0;
}

bool LinkRecoveryCoordinator::run_escalation_sequence(uint32_t now_ms,
                                                      const uint8_t* peer_mac,
                                                      bool reset_no_mem_state) {
    ++stats_.handled_events;

    if (reset_no_mem_state) {
        EspnowTxScheduler::reset_consecutive_no_mem_count();
        EspnowTxScheduler::clear_ack_tokens();
        EspnowTxScheduler::purge_all_queues();
    }

    if (stats_.last_recovery_ms > 0 &&
        (now_ms - stats_.last_recovery_ms) > config_.escalation_reset_window_ms) {
        stats_.l1_attempts = 0;
        stats_.l2_attempts = 0;
    }
    stats_.last_recovery_ms = now_ms;

    if (stats_.l1_attempts < config_.l1_budget) {
        ++stats_.l1_attempts;
        stats_.last_level = RecoveryLevel::L1;
        UnifiedLinkFsm::instance().enter_recovery(RecoveryLevel::L1, now_ms);

        if (hooks_.on_radio_deinit != nullptr) {
            hooks_.on_radio_deinit(hooks_.context);
        }
        esp_now_deinit();
        vTaskDelay(pdMS_TO_TICKS(config_.l1_reinit_delay_ms));
        if (esp_now_init() == ESP_OK) {
            restore_post_reinit_peer_state(peer_mac);
            UnifiedLinkFsm::instance().recovery_succeeded_to_discovery(static_cast<uint32_t>(millis()));
            stats_.last_level = RecoveryLevel::NONE;
        }
        return true;
    }

    if (stats_.l2_attempts < config_.l2_budget) {
        ++stats_.l2_attempts;
        stats_.last_level = RecoveryLevel::L2;
        UnifiedLinkFsm::instance().enter_recovery(RecoveryLevel::L2, now_ms);

        if (hooks_.on_radio_deinit != nullptr) {
            hooks_.on_radio_deinit(hooks_.context);
        }
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(config_.l2_stop_delay_ms));
        esp_wifi_start();
        vTaskDelay(pdMS_TO_TICKS(config_.l2_reassoc_delay_ms));
        if (esp_now_init() == ESP_OK) {
            restore_post_reinit_peer_state(peer_mac);
            if (hooks_.on_l2_wifi_restarted != nullptr) {
                hooks_.on_l2_wifi_restarted(hooks_.context);
            }
            stats_.l1_attempts = 0;
            UnifiedLinkFsm::instance().recovery_succeeded_to_discovery(static_cast<uint32_t>(millis()));
            stats_.last_level = RecoveryLevel::NONE;
        }
        return true;
    }

    stats_.last_level = RecoveryLevel::RESTART;
    UnifiedLinkFsm::instance().mark_restart_required(now_ms);
    if (hooks_.restart_device != nullptr) {
        hooks_.restart_device(hooks_.context);
    } else {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    return true;
}

bool LinkRecoveryCoordinator::handle_no_mem_pressure(uint32_t now_ms,
                                                     uint32_t consecutive_no_mem,
                                                     uint32_t trigger_threshold,
                                                     bool allow_escalation,
                                                     const uint8_t* peer_mac) {
    UnifiedLinkFsm::instance().set_no_mem_consecutive(consecutive_no_mem, now_ms);

    const uint32_t effective_threshold =
        (trigger_threshold > 0U) ? trigger_threshold : config_.no_mem_trigger_threshold;

    if (consecutive_no_mem < effective_threshold) {
        return false;
    }

    if (!allow_escalation) {
        EspnowTxScheduler::reset_consecutive_no_mem_count();
        return false;
    }

    return run_escalation_sequence(now_ms, peer_mac, true);
}

bool LinkRecoveryCoordinator::handle_persistent_httpd_failure(uint32_t now_ms,
                                                              uint32_t consecutive_failures,
                                                              int last_errno,
                                                              const uint8_t* peer_mac) {
    stats_.last_httpd_failure_ms = now_ms;
    stats_.last_httpd_errno = static_cast<uint32_t>(last_errno);

    if (consecutive_failures < config_.httpd_persistent_failure_threshold) {
        return false;
    }

    if (stats_.last_recovery_ms > 0 &&
        (now_ms - stats_.last_recovery_ms) > config_.httpd_failure_window_ms) {
        return false;
    }

    return run_escalation_sequence(now_ms, peer_mac, false);
}

void LinkRecoveryCoordinator::restore_post_reinit_peer_state(const uint8_t* peer_mac) const {
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (hooks_.reinstall_send_cb != nullptr) {
        hooks_.reinstall_send_cb(hooks_.context);
    }
    EspnowPeerManager::add_broadcast_peer();
    if (peer_mac != nullptr && EspNowMacUtils::has_valid_mac(peer_mac)) {
        EspnowPeerManager::add_peer(peer_mac, 0);
    }
}

}  // namespace esp32common::espnow
