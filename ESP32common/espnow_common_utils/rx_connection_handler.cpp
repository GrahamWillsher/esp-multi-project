#include "rx_connection_handler.h"
#include "channel_authority.h"
#include "link_recovery_coordinator.h"
#include "radio_pressure_state.h"
#include "unified_link_fsm.h"

#include <Arduino.h>
#include <cstring>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/common.h>
#include <esp32common/espnow/mac_utils.h>
#include <esp32common/patterns/numeric_safety.h>
#include <esp32common/espnow/rx_heartbeat_manager.h>
#include <esp32common/espnow/standard_handlers.h>
#include <esp32common/espnow/tx_scheduler.h>
#include <channel_manager.h>
#include <espnow_peer_manager.h>
#include <logging_config.h>
#include <rx_radio_arbiter_fsm.h>

namespace {

constexpr uint32_t kConnectConfirmAckRetryIntervalMs = 250;
constexpr uint8_t kConnectConfirmAckMaxRetries = 20;
constexpr uint8_t kConnectConfirmAckStatusOk = CONNECT_CONFIRM_STATUS_OK;
constexpr uint8_t kConnectConfirmAckStatusVersionMismatch = CONNECT_CONFIRM_STATUS_VERSION_MISMATCH;

esp_err_t send_request_data_message(const uint8_t* mac, uint8_t subtype) {
    request_data_t request{msg_request_data, subtype};
    return EspnowTxScheduler::send(mac, &request, sizeof(request), "REQUEST_DATA");
}

void call_disconnect_mqtt(const ReceiverConnectionHandlerHooks& hooks) {
    if (hooks.disconnect_mqtt != nullptr) {
        hooks.disconnect_mqtt(hooks.context);
    }
}

void call_on_connected(const ReceiverConnectionHandlerHooks& hooks) {
    if (hooks.on_connected != nullptr) {
        hooks.on_connected(hooks.context);
    }
}

void call_on_connection_lost(const ReceiverConnectionHandlerHooks& hooks) {
    if (hooks.on_connection_lost != nullptr) {
        hooks.on_connection_lost(hooks.context);
    }
}

void call_on_config_update_sent(const ReceiverConnectionHandlerHooks& hooks) {
    if (hooks.on_config_update_sent != nullptr) {
        hooks.on_config_update_sent(hooks.context);
    }
}

bool call_send_initialization_burst(const ReceiverConnectionHandlerHooks& hooks,
                                    const uint8_t* transmitter_mac) {
    if (hooks.send_initialization_burst == nullptr) {
        return false;
    }

    return hooks.send_initialization_burst(hooks.context, transmitter_mac);
}

bool call_has_recent_power_data(const ReceiverConnectionHandlerHooks& hooks,
                                const ReceiverConnectionHandler& handler,
                                uint32_t now_ms,
                                uint32_t freshness_ms) {
    if (hooks.has_recent_power_data != nullptr) {
        return hooks.has_recent_power_data(hooks.context, handler, now_ms, freshness_ms);
    }

    return handler.get_last_rx_time_ms() > 0 &&
           ((now_ms - handler.get_last_rx_time_ms()) <= freshness_ms);
}

void call_run_project_specific_tick(const ReceiverConnectionHandlerHooks& hooks,
                                    ReceiverConnectionHandler& handler,
                                    uint32_t now_ms) {
    if (hooks.run_project_specific_tick != nullptr) {
        hooks.run_project_specific_tick(hooks.context, handler, now_ms);
    }
}

void call_on_radio_deinit(const ReceiverConnectionHandlerHooks& hooks) {
    if (hooks.on_radio_deinit != nullptr) {
        hooks.on_radio_deinit(hooks.context);
    }
}

void call_on_l2_wifi_restarted(const ReceiverConnectionHandlerHooks& hooks) {
    if (hooks.on_l2_wifi_restarted != nullptr) {
        hooks.on_l2_wifi_restarted(hooks.context);
    }
}

uint32_t select_no_mem_trigger_threshold(uint32_t base_threshold,
                                         EspNowConnectionState cm_state,
                                         RadioPressureState pressure,
                                         uint32_t consecutive_no_mem) {
    uint32_t threshold = base_threshold;

    // CONNECTING requires quicker decisiveness than steady-state CONNECTED.
    if (cm_state == EspNowConnectionState::CONNECTING && threshold > 10U) {
        threshold = 10U;
    }

    // Under CRITICAL pressure, trigger recovery sooner.
    if (pressure == RadioPressureState::CRITICAL && threshold > 6U) {
        threshold = 6U;
    }

    // If failures are already climbing, don't wait for very large static thresholds.
    const uint32_t half_base = (base_threshold >= 2U) ? (base_threshold / 2U) : 1U;
    if (consecutive_no_mem >= half_base && threshold > 8U) {
        threshold = 8U;
    }

    if (threshold < 4U) {
        threshold = 4U;
    }

    return threshold;
}

}  // namespace

ReceiverConnectionHandler& ReceiverConnectionHandler::instance() {
    static ReceiverConnectionHandler instance;
    return instance;
}

ReceiverConnectionHandler::ReceiverConnectionHandler() {
    std::memset(transmitter_mac_, 0, sizeof(transmitter_mac_));
}

void ReceiverConnectionHandler::configure_hooks(const ReceiverConnectionHandlerHooks& hooks) {
    hooks_ = hooks;
}

void ReceiverConnectionHandler::configure(const ReceiverConnectionHandlerConfig& config) {
    config_ = config;
}

void ReceiverConnectionHandler::init() {
    const uint32_t now_ms = millis();
    last_rx_time_ms_ = now_ms;
    pending_peer_cleanup_ = false;
    std::memset(pending_peer_cleanup_mac_, 0, sizeof(pending_peer_cleanup_mac_));

    // ── Wire recovery coordinator hooks from project hooks ─────────────────
    // Threshold budgets are coordinator-owned (LinkRecoveryCoordinatorConfig).
    // This handler only selects the dynamic trigger threshold.
    {
        esp32common::espnow::LinkRecoveryCoordinatorHooks rc_hooks{};
        rc_hooks.context              = hooks_.context;
        rc_hooks.on_radio_deinit      = hooks_.on_radio_deinit;
        rc_hooks.on_l2_wifi_restarted = hooks_.on_l2_wifi_restarted;
        rc_hooks.reinstall_send_cb    = hooks_.reinstall_send_cb;
        esp32common::espnow::LinkRecoveryCoordinator::instance().configure_hooks(rc_hooks);
        esp32common::espnow::LinkRecoveryCoordinator::instance().reset();
    }

    auto& connection_manager = EspNowConnectionManager::instance();
    RxRadioArbiterFsm::instance().reset(now_ms);
    connection_manager.set_heartbeat_timeout_ms(config_.heartbeat_timeout_ms);
    connection_manager.set_heartbeat_timeout_enabled(true);

    connection_manager.register_state_callback(
        [](EspNowConnectionState old_state, EspNowConnectionState new_state) {
            LOG_INFO("RX_CONN", "State change: %s -> %s",
                     espnow_state_to_string(old_state),
                     espnow_state_to_string(new_state));

            auto& self = ReceiverConnectionHandler::instance();

            if (new_state == EspNowConnectionState::CONNECTED) {
                // End reconnect diagnostics (if a prior disconnect was tracked)
                if (self.reconnect_diag_active_) {
                    self.end_reconnect_diagnostics(static_cast<uint32_t>(millis()), "reconnected");
                }

                call_on_connected(self.hooks_);

                // Clean connection — clear control-only mode and reset NO_MEM recovery counters
                EspnowTxScheduler::set_control_only_mode(false);
                esp32common::espnow::LinkRecoveryCoordinator::instance().reset();

                self.deferred_peer_.reset();
                self.pending_peer_cleanup_ = false;
                std::memset(self.pending_peer_cleanup_mac_, 0, sizeof(self.pending_peer_cleanup_mac_));
                self.pending_connect_confirm_ack_ = false;
                self.pending_connect_confirm_ack_session_id_ = 0;
                self.pending_connect_confirm_ack_session_boot_nonce_ = 0;
                self.pending_connect_confirm_ack_due_ms_ = 0;
                self.pending_connect_confirm_ack_retry_count_ = 0;
                self.pending_connect_confirm_ack_rx_status_ = 0;
                std::memset(self.pending_connect_confirm_ack_mac_, 0, sizeof(self.pending_connect_confirm_ack_mac_));

                const uint8_t current_channel = ChannelManager::instance().get_channel();
                ChannelManager::instance().lock_channel(current_channel, "RX_CONN");
                LOG_INFO("RX_CONN", "Connected - channel locked at %u",
                         static_cast<unsigned>(current_channel));

                self.connected_at_ms_ = millis();
                self.last_retry_ms_ = millis();
                self.last_config_retry_ms_ = millis();
                self.power_data_confirmed_ = false;
                self.had_connected_session_ = true;
                self.init_pending_ = true;

                auto& arbiter = RxRadioArbiterFsm::instance();
                const uint32_t now = static_cast<uint32_t>(millis());
                (void)arbiter.on_event(RxRadioArbiterFsm::Event::EV_DISCOVERY_ACK_SENT_OK, now);
                self.fsm_waiting_first_heartbeat_after_recovery_ = true;

                LOG_INFO("RX_CONN", "[INIT] Connected - init burst deferred to tick() (ACK priority)");
            } else if (old_state == EspNowConnectionState::CONNECTED &&
                       new_state == EspNowConnectionState::IDLE) {
                uint8_t saved_peer_mac[6] = {};
                const uint8_t* current_peer = self.get_transmitter_mac();
                if (current_peer != nullptr) {
                    std::memcpy(saved_peer_mac, current_peer, 6);
                }

                self.pending_peer_cleanup_ =
                    EspNowMacUtils::has_valid_mac(saved_peer_mac) &&
                    !EspNowMacUtils::is_broadcast_mac(saved_peer_mac);
                if (self.pending_peer_cleanup_) {
                    std::memcpy(self.pending_peer_cleanup_mac_, saved_peer_mac, 6);
                } else {
                    std::memset(self.pending_peer_cleanup_mac_, 0, sizeof(self.pending_peer_cleanup_mac_));
                }

                // Activate control-only mode immediately on link loss
                EspnowTxScheduler::set_control_only_mode(true);

                (void)RxRadioArbiterFsm::instance().on_event(
                    RxRadioArbiterFsm::Event::EV_HEARTBEAT_STALE,
                    static_cast<uint32_t>(millis()));

                // Start reconnect diagnostics if this was a clean prior session
                if (self.had_connected_session_ && !self.reconnect_diag_active_) {
                    self.begin_reconnect_diagnostics(static_cast<uint32_t>(millis()));
                }

                call_on_connection_lost(self.hooks_);
                self.on_connection_lost();
                self.deferred_peer_.reset();
                self.pending_connect_confirm_ack_ = false;
                self.pending_connect_confirm_ack_session_id_ = 0;
                self.pending_connect_confirm_ack_session_boot_nonce_ = 0;
                self.pending_connect_confirm_ack_due_ms_ = 0;
                self.pending_connect_confirm_ack_retry_count_ = 0;
                self.pending_connect_confirm_ack_rx_status_ = 0;
                std::memset(self.pending_connect_confirm_ack_mac_, 0, sizeof(self.pending_connect_confirm_ack_mac_));

                ChannelManager::instance().unlock_channel("RX_CONN");
                LOG_INFO("RX_CONN", "Connection lost - channel unlocked (peer cleanup deferred)");
            } else if (old_state == EspNowConnectionState::CONNECTING &&
                       new_state == EspNowConnectionState::IDLE) {
                if (self.pending_peer_cleanup_ &&
                    EspnowPeerManager::is_peer_registered(self.pending_peer_cleanup_mac_)) {
                    if (EspnowPeerManager::remove_peer(self.pending_peer_cleanup_mac_)) {
                        LOG_INFO("RX_CONN", "Removed peer after CONNECTING timeout");
                    } else {
                        LOG_WARN("RX_CONN", "Failed to remove peer after CONNECTING timeout");
                    }
                }

                self.pending_peer_cleanup_ = false;
                std::memset(self.pending_peer_cleanup_mac_, 0, sizeof(self.pending_peer_cleanup_mac_));
            } else if (new_state == EspNowConnectionState::CONNECTING) {
                self.flush_deferred_peer_registered();
            }
        });

    (void)post_connection_event(EspNowEvent::CONNECTION_START, nullptr);
    LOG_INFO("RX_CONN", "Receiver connection handler initialized");
}

void ReceiverConnectionHandler::on_probe_received(const uint8_t* transmitter_mac) {
    if (transmitter_mac != nullptr) {
        std::memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }

    const uint32_t now = millis();
    last_rx_time_ms_ = now;

    const auto state = EspNowConnectionManager::instance().get_state();
    if (had_connected_session_ && state != EspNowConnectionState::CONNECTED) {
        (void)RxRadioArbiterFsm::instance().on_event(
            RxRadioArbiterFsm::Event::EV_PROBE_WHILE_PREVIOUSLY_CONNECTED,
            now);
    }

    const bool already_connecting_same_peer =
        (state == EspNowConnectionState::CONNECTING) &&
        EspNowMacUtils::has_valid_mac(transmitter_mac_) &&
        (std::memcmp(EspNowConnectionManager::instance().get_peer_mac(), transmitter_mac_, 6) == 0);

    if (!already_connecting_same_peer) {
        (void)post_connection_event(EspNowEvent::PEER_FOUND, transmitter_mac_);
    }

    flush_deferred_peer_registered();
}

void ReceiverConnectionHandler::on_peer_registered(const uint8_t* transmitter_mac) {
    if (transmitter_mac != nullptr) {
        std::memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();

    const auto state = EspNowConnectionManager::instance().get_state();
    const bool is_connecting = (state == EspNowConnectionState::CONNECTING);
    const bool is_connected = (state == EspNowConnectionState::CONNECTED);

    if (is_connected) {
        LOG_DEBUG("RX_CONN", "on_peer_registered() while CONNECTED - ignoring duplicate");
        return;
    }

    const bool should_post = deferred_peer_.on_peer_registered(transmitter_mac_, is_connecting, millis());
    if (should_post) {
        (void)post_connection_event(EspNowEvent::PEER_REGISTERED, transmitter_mac_);
        deferred_peer_.on_event_posted();
    }
}

void ReceiverConnectionHandler::on_connect_confirm_received(const uint8_t* transmitter_mac,
                                                           uint16_t session_id,
                                                           uint16_t session_boot_nonce,
                                                           uint8_t protocol_version) {
    if (transmitter_mac != nullptr) {
        std::memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
        std::memcpy(pending_connect_confirm_ack_mac_, transmitter_mac, sizeof(pending_connect_confirm_ack_mac_));
    }

    last_rx_time_ms_ = millis();
    pending_connect_confirm_ack_session_id_ = session_id;
    pending_connect_confirm_ack_session_boot_nonce_ = session_boot_nonce;
    pending_connect_confirm_ack_rx_status_ =
        (protocol_version == ESPNOW_PROTOCOL_VERSION)
            ? kConnectConfirmAckStatusOk
            : kConnectConfirmAckStatusVersionMismatch;
    pending_connect_confirm_ack_retry_count_ = 0;
    pending_connect_confirm_ack_due_ms_ = 0;
    pending_connect_confirm_ack_ = EspNowMacUtils::has_valid_mac(pending_connect_confirm_ack_mac_);

    LOG_INFO("RX_CONN",
             "Queued connect_confirm_ack session=%u boot_nonce=%u rx_status=%u",
             static_cast<unsigned>(pending_connect_confirm_ack_session_id_),
             static_cast<unsigned>(pending_connect_confirm_ack_session_boot_nonce_),
             static_cast<unsigned>(pending_connect_confirm_ack_rx_status_));

    try_send_pending_connect_confirm_ack(last_rx_time_ms_, "immediate");
}

void ReceiverConnectionHandler::on_data_received(const uint8_t* transmitter_mac) {
    if (transmitter_mac != nullptr) {
        std::memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();

    EspNowConnectionManager::instance().on_heartbeat_received();
    (void)post_connection_event(EspNowEvent::DATA_RECEIVED, transmitter_mac_);
}

void ReceiverConnectionHandler::on_link_activity(const uint8_t* transmitter_mac) {
    if (transmitter_mac != nullptr) {
        std::memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();

    if (connected_at_ms_ == 0 && EspNowMacUtils::has_valid_mac(transmitter_mac_)) {
        connected_at_ms_ = millis();
        last_retry_ms_ = millis();
    }

    flush_deferred_peer_registered();
}

void ReceiverConnectionHandler::on_power_data_received() {
    if (!power_data_confirmed_) {
        power_data_confirmed_ = true;
        LOG_INFO("RX_CONN", "Power-profile data confirmed");
    }
}

void ReceiverConnectionHandler::on_transmitter_reboot_detected() {
    power_data_confirmed_ = false;
    connected_at_ms_ = millis();
    last_retry_ms_ = 0;
    LOG_WARN("RX_CONN", "TX reboot detected - re-arming stream request retries");
}

void ReceiverConnectionHandler::on_ack_send_pressure(const char* reason) {
    const uint32_t now = static_cast<uint32_t>(millis());
    (void)RxRadioArbiterFsm::instance().on_event(
        RxRadioArbiterFsm::Event::EV_DISCOVERY_ACK_SEND_FAIL_NO_MEM,
        now);

    LOG_WARN("RX_CONN", "ACK send pressure: %s", reason ? reason : "unknown");
}

void ReceiverConnectionHandler::on_heartbeat_fresh() {
    const uint32_t now = static_cast<uint32_t>(millis());
    auto& arbiter = RxRadioArbiterFsm::instance();
    (void)arbiter.on_event(RxRadioArbiterFsm::Event::EV_HEARTBEAT_FRESH, now);

    if (fsm_waiting_first_heartbeat_after_recovery_) {
        (void)arbiter.on_event(RxRadioArbiterFsm::Event::EV_FIRST_HEARTBEAT_AFTER_RECOVERY, now);
        fsm_waiting_first_heartbeat_after_recovery_ = false;
    }
}

void ReceiverConnectionHandler::on_type_catalog_versions_received() {
    catalog_retry_.on_versions_received();
}

void ReceiverConnectionHandler::on_led_state_received() {
    if (led_sync_.is_pending()) {
        LOG_DEBUG("RX_CONN", "Transmitter LED state received after %u attempt(s)",
                  static_cast<unsigned>(led_sync_.attempt_count()));
        led_sync_.on_response_received();
    }
}

void ReceiverConnectionHandler::on_config_update_sent() {
    call_on_config_update_sent(hooks_);
    LOG_DEBUG("RX_CONN", "Config update sent - stale detection grace window started");
}

void ReceiverConnectionHandler::on_connection_lost() {
    first_data_received_ = false;
    power_data_confirmed_ = false;
    connected_at_ms_ = 0;
    last_retry_ms_ = 0;
    last_config_retry_ms_ = 0;
    init_pending_ = false;
    EspnowTxScheduler::set_control_only_mode(true);
    EspnowTxScheduler::clear_ack_tokens();
    std::memset(transmitter_mac_, 0, sizeof(transmitter_mac_));

    led_sync_.reset();
    catalog_retry_.reset();
    deferred_peer_.reset();

    esp32common::espnow::LinkRecoveryCoordinator::instance().reset();
    fsm_waiting_first_heartbeat_after_recovery_ = false;
}


void ReceiverConnectionHandler::begin_reconnect_diagnostics(uint32_t now_ms) {
    reconnect_diag_active_ = true;
    reconnect_diag_start_ms_ = now_ms;
    reconnect_diag_first_probe_ms_ = now_ms;
    reconnect_diag_quiet_enter_ms_ = 0;

    EspnowStandardHandlers::AckSendStats discovery_stats{};
    (void)EspnowStandardHandlers::read_ack_send_stats(discovery_stats);
    reconnect_diag_base_discovery_ack_attempts_ = discovery_stats.direct_attempts;
    reconnect_diag_base_discovery_ack_no_mem_failures_ = discovery_stats.direct_no_mem_failures;
    reconnect_diag_base_discovery_ack_other_failures_ = discovery_stats.direct_other_failures;

    RxHeartbeatAckEnqueueStats heartbeat_ack_stats{};
    (void)RxHeartbeatManager::instance().read_ack_enqueue_stats(heartbeat_ack_stats);
    reconnect_diag_base_hb_ack_attempts_ = heartbeat_ack_stats.enqueue_attempts;
    reconnect_diag_base_hb_ack_no_mem_failures_ = heartbeat_ack_stats.enqueue_no_mem_failures;
    reconnect_diag_base_hb_ack_other_failures_ = heartbeat_ack_stats.enqueue_other_failures;

    EspnowTxScheduler::Stats sched_stats{};
    (void)EspnowTxScheduler::read_stats(sched_stats);
    reconnect_diag_base_sched_fail_control_ = sched_stats.send_fail_control;
    reconnect_diag_base_sched_fail_discovery_ = sched_stats.send_fail_discovery;
    reconnect_diag_base_sched_fail_data_ = sched_stats.send_fail_data;
    reconnect_diag_base_sched_fail_monitoring_ = sched_stats.send_fail_monitoring;
}

void ReceiverConnectionHandler::end_reconnect_diagnostics(uint32_t now_ms, const char* outcome) {
    if (!reconnect_diag_active_) {
        return;
    }

    EspnowStandardHandlers::AckSendStats discovery_stats{};
    (void)EspnowStandardHandlers::read_ack_send_stats(discovery_stats);
    const uint32_t discovery_ack_attempts =
        discovery_stats.direct_attempts - reconnect_diag_base_discovery_ack_attempts_;
    const uint32_t discovery_ack_fail_no_mem =
        discovery_stats.direct_no_mem_failures - reconnect_diag_base_discovery_ack_no_mem_failures_;
    const uint32_t discovery_ack_fail_other =
        discovery_stats.direct_other_failures - reconnect_diag_base_discovery_ack_other_failures_;

    RxHeartbeatAckEnqueueStats heartbeat_ack_stats{};
    (void)RxHeartbeatManager::instance().read_ack_enqueue_stats(heartbeat_ack_stats);
    const uint32_t heartbeat_ack_attempts =
        heartbeat_ack_stats.enqueue_attempts - reconnect_diag_base_hb_ack_attempts_;
    const uint32_t heartbeat_ack_fail_no_mem =
        heartbeat_ack_stats.enqueue_no_mem_failures - reconnect_diag_base_hb_ack_no_mem_failures_;
    const uint32_t heartbeat_ack_fail_other =
        heartbeat_ack_stats.enqueue_other_failures - reconnect_diag_base_hb_ack_other_failures_;

    EspnowTxScheduler::Stats sched_stats{};
    (void)EspnowTxScheduler::read_stats(sched_stats);
    const uint32_t sched_fail_control = sched_stats.send_fail_control - reconnect_diag_base_sched_fail_control_;
    const uint32_t sched_fail_discovery = sched_stats.send_fail_discovery - reconnect_diag_base_sched_fail_discovery_;
    const uint32_t sched_fail_data = sched_stats.send_fail_data - reconnect_diag_base_sched_fail_data_;
    const uint32_t sched_fail_monitoring = sched_stats.send_fail_monitoring - reconnect_diag_base_sched_fail_monitoring_;

    LOG_INFO("RX_RECONNECT_DIAG",
             "attempt_start=%lu first_probe=%lu quiet_enter=%lu quiet_exit=%lu duration_ms=%lu outcome=%s",
             static_cast<unsigned long>(reconnect_diag_start_ms_),
             static_cast<unsigned long>(reconnect_diag_first_probe_ms_),
             static_cast<unsigned long>(reconnect_diag_quiet_enter_ms_),
             static_cast<unsigned long>(now_ms),
             static_cast<unsigned long>(now_ms - reconnect_diag_start_ms_),
             outcome ? outcome : "unknown");

    LOG_INFO("RX_RECONNECT_DIAG",
             "discovery_ack: attempts=%lu fail_no_mem=%lu fail_other=%lu | heartbeat_ack: attempts=%lu fail_no_mem=%lu fail_other=%lu | scheduler_send_fail: control=%lu discovery=%lu data=%lu monitoring=%lu",
             static_cast<unsigned long>(discovery_ack_attempts),
             static_cast<unsigned long>(discovery_ack_fail_no_mem),
             static_cast<unsigned long>(discovery_ack_fail_other),
             static_cast<unsigned long>(heartbeat_ack_attempts),
             static_cast<unsigned long>(heartbeat_ack_fail_no_mem),
             static_cast<unsigned long>(heartbeat_ack_fail_other),
             static_cast<unsigned long>(sched_fail_control),
             static_cast<unsigned long>(sched_fail_discovery),
             static_cast<unsigned long>(sched_fail_data),
             static_cast<unsigned long>(sched_fail_monitoring));

    reconnect_diag_active_ = false;
    reconnect_diag_start_ms_ = 0;
    reconnect_diag_first_probe_ms_ = 0;
    reconnect_diag_quiet_enter_ms_ = 0;
}

void ReceiverConnectionHandler::flush_deferred_peer_registered() {
    const auto state = EspNowConnectionManager::instance().get_state();
    const bool is_connecting = (state == EspNowConnectionState::CONNECTING);

    uint8_t flush_mac[6] = {};
    if (deferred_peer_.try_flush(is_connecting, millis(), flush_mac)) {
        (void)post_connection_event(EspNowEvent::PEER_REGISTERED, flush_mac);
        deferred_peer_.on_event_posted();
        LOG_INFO("RX_CONN", "Flushed deferred PEER_REGISTERED in CONNECTING state");
    }
}

void ReceiverConnectionHandler::try_send_pending_connect_confirm_ack(uint32_t now_ms, const char* trigger) {
    if (!pending_connect_confirm_ack_) {
        return;
    }

    if (!esp32common::numeric::due_u32(now_ms, pending_connect_confirm_ack_due_ms_)) {
        return;
    }

    if (!EspNowMacUtils::has_valid_mac(pending_connect_confirm_ack_mac_)) {
        LOG_WARN("RX_CONN", "Dropping pending connect_confirm_ack: invalid MAC");
        pending_connect_confirm_ack_ = false;
        pending_connect_confirm_ack_session_id_ = 0;
        pending_connect_confirm_ack_session_boot_nonce_ = 0;
        pending_connect_confirm_ack_due_ms_ = 0;
        pending_connect_confirm_ack_retry_count_ = 0;
        pending_connect_confirm_ack_rx_status_ = 0;
        std::memset(pending_connect_confirm_ack_mac_, 0, sizeof(pending_connect_confirm_ack_mac_));
        return;
    }

    espnow_connect_confirm_ack_t ack{};
    ack.type = msg_connect_confirm_ack;
    ack.protocol_version = ESPNOW_PROTOCOL_VERSION;
    ack.session_id = pending_connect_confirm_ack_session_id_;
    ack.rx_status = pending_connect_confirm_ack_rx_status_;
    ack.session_boot_nonce = pending_connect_confirm_ack_session_boot_nonce_;

    const esp_err_t err = EspnowTxScheduler::send(
        pending_connect_confirm_ack_mac_,
        &ack,
        sizeof(ack),
        "CONNECT_CONFIRM_ACK");

    if (err == ESP_OK) {
        LOG_INFO("RX_CONN",
                 "connect_confirm_ack queued (session=%u boot_nonce=%u rx_status=%u retries=%u trigger=%s)",
                 static_cast<unsigned>(pending_connect_confirm_ack_session_id_),
                 static_cast<unsigned>(pending_connect_confirm_ack_session_boot_nonce_),
                 static_cast<unsigned>(pending_connect_confirm_ack_rx_status_),
                 static_cast<unsigned>(pending_connect_confirm_ack_retry_count_),
                 trigger ? trigger : "unknown");

        const uint8_t registered_mac[6] = {
            pending_connect_confirm_ack_mac_[0], pending_connect_confirm_ack_mac_[1], pending_connect_confirm_ack_mac_[2],
            pending_connect_confirm_ack_mac_[3], pending_connect_confirm_ack_mac_[4], pending_connect_confirm_ack_mac_[5]
        };

        pending_connect_confirm_ack_ = false;
        pending_connect_confirm_ack_session_id_ = 0;
        pending_connect_confirm_ack_session_boot_nonce_ = 0;
        pending_connect_confirm_ack_due_ms_ = 0;
        pending_connect_confirm_ack_retry_count_ = 0;
        pending_connect_confirm_ack_rx_status_ = 0;
        std::memset(pending_connect_confirm_ack_mac_, 0, sizeof(pending_connect_confirm_ack_mac_));

        if (ack.rx_status == kConnectConfirmAckStatusOk) {
            LOG_INFO("RX_CONN",
                     "[STATE_CHANGE] -> CONNECTED  reason=confirm_ack_queued  session=%u boot_nonce=%u",
                     static_cast<unsigned>(ack.session_id),
                     static_cast<unsigned>(ack.session_boot_nonce));
            EspNowConnectionManager::instance().authorize_peer_registered_transition(registered_mac);
            on_peer_registered(registered_mac);
        } else {
            LOG_WARN("RX_CONN",
                     "connect_confirm_ack queued with non-OK rx_status=%u; refusing CONNECTED transition",
                     static_cast<unsigned>(ack.rx_status));
        }
        return;
    }

    (void)esp32common::numeric::increment_saturating<uint8_t>(
        pending_connect_confirm_ack_retry_count_,
        kConnectConfirmAckMaxRetries);

    if (pending_connect_confirm_ack_retry_count_ >= kConnectConfirmAckMaxRetries) {
        LOG_WARN("RX_CONN",
                 "connect_confirm_ack retry budget exhausted (session=%u boot_nonce=%u retries=%u) - clearing pending context",
                 static_cast<unsigned>(pending_connect_confirm_ack_session_id_),
                 static_cast<unsigned>(pending_connect_confirm_ack_session_boot_nonce_),
                 static_cast<unsigned>(pending_connect_confirm_ack_retry_count_));
        pending_connect_confirm_ack_ = false;
        pending_connect_confirm_ack_session_id_ = 0;
        pending_connect_confirm_ack_session_boot_nonce_ = 0;
        pending_connect_confirm_ack_due_ms_ = 0;
        pending_connect_confirm_ack_retry_count_ = 0;
        pending_connect_confirm_ack_rx_status_ = 0;
        std::memset(pending_connect_confirm_ack_mac_, 0, sizeof(pending_connect_confirm_ack_mac_));
        return;
    }

    pending_connect_confirm_ack_due_ms_ = now_ms + kConnectConfirmAckRetryIntervalMs;

    LOG_WARN("RX_CONN",
             "connect_confirm_ack enqueue failed: %s (session=%u boot_nonce=%u retry=%u trigger=%s next_retry_in=%lu ms)",
             esp_err_to_name(err),
             static_cast<unsigned>(pending_connect_confirm_ack_session_id_),
             static_cast<unsigned>(pending_connect_confirm_ack_session_boot_nonce_),
             static_cast<unsigned>(pending_connect_confirm_ack_retry_count_),
             trigger ? trigger : "unknown",
             static_cast<unsigned long>(kConnectConfirmAckRetryIntervalMs));
}

void ReceiverConnectionHandler::send_initialization_requests(const uint8_t* transmitter_mac) {
    if (transmitter_mac == nullptr || !EspNowMacUtils::has_valid_mac(transmitter_mac)) {
        LOG_WARN("RX_CONN", "Cannot send initialization - invalid transmitter MAC");
        return;
    }

    if (EspNowConnectionManager::instance().get_state() != EspNowConnectionState::CONNECTED) {
        LOG_WARN("RX_CONN", "Cannot send initialization - need CONNECTED state");
        return;
    }

    first_data_received_ = true;
    catalog_retry_.reset();
    LOG_INFO("RX_CONN", "[INIT] Connection confirmed - beginning paced initialization");

    if (!call_send_initialization_burst(hooks_, transmitter_mac)) {
        LOG_WARN("RX_CONN", "[INIT] No project initialization hook configured");
    }
}

void ReceiverConnectionHandler::tick() {
    flush_deferred_peer_registered();
    try_send_pending_connect_confirm_ack(static_cast<uint32_t>(millis()), "tick");
    auto& connection_manager = EspNowConnectionManager::instance();

    (void)EspnowTxScheduler::check_ack_token_watchdogs();

    // ── ESP-NOW TX buffer recovery (three-level escalation) ─────────────────
    //
    // Level 1 (L1): esp_now_deinit / esp_now_init.
    //   Fast (~100 ms). Clears the ESP-NOW software layer but does NOT flush
    //   the LMAC hardware DMA descriptor ring. Effective for software-stuck
    //   states. Budget is owned by LinkRecoveryCoordinator config.
    //
    // Level 2 (L2): esp_wifi_stop / esp_wifi_start.
    //   Flushes the LMAC hardware completely (~1.7 s including AP re-assoc).
    //   This is the definitive fix for stuck hardware TX descriptors.
    //   Budget is owned by LinkRecoveryCoordinator config.
    //
    // Level 3: esp_restart — all recovery exhausted.
    //
    // Build-flag tuning applies to RXCONN_NO_MEM_TRIGGER_THRESHOLD only.
    // L1/L2 budgets are configured by LinkRecoveryCoordinator.
    //
    // RXCONN_NO_MEM_TRIGGER_THRESHOLD controls how many consecutive msg_ack
    // NO_MEM failures must occur before L1 recovery is attempted.  The default
    // (10) is intentionally conservative: transient WiFi TCP bursts from an
    // HTTP page load or an MQTT connect attempt can easily cause 5-6 failures
    // without the ESP-NOW stack being genuinely stuck.  A higher threshold
    // avoids spurious L1_REINIT which is more disruptive than the burst itself.
    //
    // Counters are reset on successful CONNECTED transition.
#ifndef RXCONN_NO_MEM_TRIGGER_THRESHOLD
#define RXCONN_NO_MEM_TRIGGER_THRESHOLD 10
#endif

    constexpr uint32_t kNoMemTriggerThresholdBase = RXCONN_NO_MEM_TRIGGER_THRESHOLD;
    const uint32_t consecutive_no_mem = EspnowTxScheduler::get_consecutive_no_mem_count();
    const EspNowConnectionState cm_state = connection_manager.get_state();
    const RadioPressureState pressure_state = get_radio_pressure_state();
    const uint32_t kNoMemTriggerThreshold = select_no_mem_trigger_threshold(
        kNoMemTriggerThresholdBase,
        cm_state,
        pressure_state,
        consecutive_no_mem);

    static uint32_t s_last_logged_threshold = UINT32_MAX;
    if (s_last_logged_threshold != kNoMemTriggerThreshold) {
        LOG_INFO("RX_CONN",
                 "Adaptive NO_MEM trigger threshold=%lu (base=%lu state=%u pressure=%s consecutive=%lu)",
                 static_cast<unsigned long>(kNoMemTriggerThreshold),
                 static_cast<unsigned long>(kNoMemTriggerThresholdBase),
                 static_cast<unsigned>(cm_state),
                 radio_pressure_state_to_string(pressure_state),
                 static_cast<unsigned long>(consecutive_no_mem));
        s_last_logged_threshold = kNoMemTriggerThreshold;
    }

    const bool allow_no_mem_recovery_escalation =
        (cm_state == EspNowConnectionState::CONNECTED) ||
        (cm_state == EspNowConnectionState::CONNECTING);

    if (consecutive_no_mem >= kNoMemTriggerThreshold && !allow_no_mem_recovery_escalation) {
        LOG_INFO("RX_CONN",
                 "NO_MEM trigger ignored while state=%u (consecutive=%lu threshold=%lu)",
                 static_cast<unsigned>(cm_state),
                 static_cast<unsigned long>(consecutive_no_mem),
                 static_cast<unsigned long>(kNoMemTriggerThreshold));
        EspnowTxScheduler::reset_consecutive_no_mem_count();
    }

    if (consecutive_no_mem >= kNoMemTriggerThreshold && allow_no_mem_recovery_escalation) {
        (void)RxRadioArbiterFsm::instance().on_event(
            RxRadioArbiterFsm::Event::EV_DISCOVERY_ACK_SEND_FAIL_NO_MEM,
            static_cast<uint32_t>(millis()));

        const uint8_t* peer_mac =
            EspNowMacUtils::has_valid_mac(transmitter_mac_) ? transmitter_mac_ : nullptr;

        // Delegate L1 / L2 / reboot ladder to the shared recovery coordinator.
        // The coordinator owns all escalation counters, window tracking, peer
        // restoration, and hook calls; the handler no longer executes raw
        // esp_now_deinit / esp_wifi_stop / esp_restart directly.
        esp32common::espnow::LinkRecoveryCoordinator::instance().handle_no_mem_pressure(
            static_cast<uint32_t>(millis()),
            consecutive_no_mem,
            kNoMemTriggerThreshold,
            /*allow_escalation=*/true,
            peer_mac);
    }

    const uint32_t now = millis();
    auto& arbiter = RxRadioArbiterFsm::instance();
    (void)arbiter.tick(now);
    EspnowTxScheduler::set_control_only_mode(arbiter.policy().control_only_mode);

    if (!connection_manager.is_connected()) {
        return;
    }

    if (connected_at_ms_ > 0 && (now - connected_at_ms_) < config_.post_connect_init_grace_ms) {
        return;
    }

    if (init_pending_) {
        init_pending_ = false;
        send_initialization_requests(connection_manager.get_peer_mac());
        return;
    }

    const bool recent_power_data =
        call_has_recent_power_data(hooks_, *this, now, config_.power_data_freshness_ms);

    if (recent_power_data) {
        power_data_confirmed_ = true;
    }

    if (!(power_data_confirmed_ && recent_power_data)) {
        if (connected_at_ms_ == 0 || !EspNowMacUtils::has_valid_mac(transmitter_mac_)) {
            return;
        }

        if ((now - connected_at_ms_) >= config_.request_retry_timeout_ms &&
            (now - last_retry_ms_) >= config_.request_retry_interval_ms) {
            last_retry_ms_ = now;
            LOG_WARN("RX_CONN", "No power-profile data yet - retrying REQUEST_DATA");
            const esp_err_t result = send_request_data_message(transmitter_mac_, subtype_power_profile);
            if (result != ESP_OK) {
                LOG_WARN("RX_CONN", "REQUEST_DATA retry failed: %s", esp_err_to_name(result));
            }
        }
    }

    call_run_project_specific_tick(hooks_, *this, now);
}

bool ReceiverConnectionHandler::quiet_mode_active() const {
    return get_radio_pressure_state() != RadioPressureState::NORMAL;
}
