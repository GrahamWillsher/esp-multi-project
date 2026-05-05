#include "rx_connection_handler.h"

#include <Arduino.h>
#include <cstring>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/common.h>
#include <esp32common/espnow/mac_utils.h>
#include <esp32common/espnow/rx_heartbeat_manager.h>
#include <esp32common/espnow/standard_handlers.h>
#include <esp32common/espnow/tx_scheduler.h>
#include <channel_manager.h>
#include <espnow_peer_manager.h>
#include <logging_config.h>

namespace {

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

    auto& connection_manager = EspNowConnectionManager::instance();
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
                self.no_mem_l1_count_       = 0;
                self.no_mem_l2_count_       = 0;
                self.no_mem_last_reinit_ms_ = 0;

                self.deferred_peer_.reset();
                self.pending_peer_cleanup_ = false;
                std::memset(self.pending_peer_cleanup_mac_, 0, sizeof(self.pending_peer_cleanup_mac_));

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

                // Start reconnect diagnostics if this was a clean prior session
                if (self.had_connected_session_ && !self.reconnect_diag_active_) {
                    self.begin_reconnect_diagnostics(static_cast<uint32_t>(millis()));
                }

                call_on_connection_lost(self.hooks_);
                self.on_connection_lost();
                self.deferred_peer_.reset();

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
    // ACK send pressure is informational — control_only_mode is already managed
    // by the connection state callback; no additional FSM driving needed.
    LOG_WARN("RX_CONN", "ACK send pressure: %s", reason ? reason : "unknown");
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

    no_mem_l1_count_       = 0;
    no_mem_l2_count_       = 0;
    no_mem_last_reinit_ms_ = 0;
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

    (void)EspnowTxScheduler::check_ack_token_watchdogs();

    // ── ESP-NOW TX buffer recovery (three-level escalation) ─────────────────
    //
    // Level 1 (L1): esp_now_deinit / esp_now_init.
    //   Fast (~100 ms). Clears the ESP-NOW software layer but does NOT flush
    //   the LMAC hardware DMA descriptor ring. Effective for software-stuck
    //   states. Attempted up to RXCONN_NO_MEM_L1_THRESHOLD times.
    //
    // Level 2 (L2): esp_wifi_stop / esp_wifi_start.
    //   Flushes the LMAC hardware completely (~1.7 s including AP re-assoc).
    //   This is the definitive fix for stuck hardware TX descriptors.
    //   Attempted up to RXCONN_NO_MEM_L2_THRESHOLD times.
    //
    // Level 3: esp_restart — all recovery exhausted.
    //
    // Thresholds are build-flag tunable via RXCONN_NO_MEM_L1_THRESHOLD and
    // RXCONN_NO_MEM_L2_THRESHOLD (both default to 3).
    //
    // Counters are reset on successful CONNECTED transition.
#ifndef RXCONN_NO_MEM_L1_THRESHOLD
#define RXCONN_NO_MEM_L1_THRESHOLD 3
#endif
#ifndef RXCONN_NO_MEM_L2_THRESHOLD
#define RXCONN_NO_MEM_L2_THRESHOLD 3
#endif

    constexpr uint32_t kNoMemTriggerThreshold = 5;
    const uint32_t consecutive_no_mem = EspnowTxScheduler::get_consecutive_no_mem_count();
    if (consecutive_no_mem >= kNoMemTriggerThreshold) {
        // Reset the scheduler counter and drain its queues before any recovery
        // attempt so the TX worker cannot race a send during reinit.
        EspnowTxScheduler::reset_consecutive_no_mem_count();
        EspnowTxScheduler::clear_ack_tokens();
        EspnowTxScheduler::purge_all_queues();

        const uint32_t now_reinit = static_cast<uint32_t>(millis());
        if (no_mem_last_reinit_ms_ > 0 &&
            (now_reinit - no_mem_last_reinit_ms_) > 30000U) {
            // More than 30 s since last recovery attempt — not a tight loop;
            // reset level counters so the escalation ladder restarts.
            no_mem_l1_count_ = 0;
            no_mem_l2_count_ = 0;
        }
        no_mem_last_reinit_ms_ = now_reinit;

        if (no_mem_l1_count_ < RXCONN_NO_MEM_L1_THRESHOLD) {
            // ── Level 1: ESP-NOW deinit / init ────────────────────────────
            ++no_mem_l1_count_;
            LOG_WARN("RX_CONN",
                     "[STATE_CHANGE] RECOVERY -> L1_REINIT  "
                     "reason=no_mem_stuck  consecutive=%lu  attempt=%u/%d",
                     static_cast<unsigned long>(consecutive_no_mem),
                     static_cast<unsigned>(no_mem_l1_count_),
                     RXCONN_NO_MEM_L1_THRESHOLD);

            esp_now_deinit();
            vTaskDelay(pdMS_TO_TICKS(100));
            const esp_err_t l1_err = esp_now_init();
            if (l1_err == ESP_OK) {
                esp_wifi_set_ps(WIFI_PS_NONE);
                if (hooks_.reinstall_send_cb != nullptr) {
                    hooks_.reinstall_send_cb(hooks_.context);
                }
                EspnowPeerManager::add_broadcast_peer();
                if (EspNowMacUtils::has_valid_mac(transmitter_mac_)) {
                    EspnowPeerManager::add_peer(transmitter_mac_, /*channel=*/0);
                }
                LOG_INFO("RX_CONN",
                         "L1 reinit OK (attempt %u/%d) -- broadcast+TX peers restored",
                         static_cast<unsigned>(no_mem_l1_count_),
                         RXCONN_NO_MEM_L1_THRESHOLD);
            } else {
                LOG_ERROR("RX_CONN", "L1 esp_now_init failed: %s", esp_err_to_name(l1_err));
            }

        } else if (no_mem_l2_count_ < RXCONN_NO_MEM_L2_THRESHOLD) {
            // ── Level 2: WiFi stop / start — flushes LMAC hardware DMA ───
            ++no_mem_l2_count_;
            LOG_WARN("RX_CONN",
                     "[STATE_CHANGE] RECOVERY -> L2_WIFI_RESTART  "
                     "reason=l1_exhausted  attempt=%u/%d",
                     static_cast<unsigned>(no_mem_l2_count_),
                     RXCONN_NO_MEM_L2_THRESHOLD);

            esp_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(200));   // allow LMAC DMA ring to drain
            esp_wifi_start();
            vTaskDelay(pdMS_TO_TICKS(1500));  // wait for AP re-association
            const esp_err_t l2_err = esp_now_init();
            if (l2_err == ESP_OK) {
                esp_wifi_set_ps(WIFI_PS_NONE);
                if (hooks_.reinstall_send_cb != nullptr) {
                    hooks_.reinstall_send_cb(hooks_.context);
                }
                EspnowPeerManager::add_broadcast_peer();
                if (EspNowMacUtils::has_valid_mac(transmitter_mac_)) {
                    EspnowPeerManager::add_peer(transmitter_mac_, /*channel=*/0);
                }
                // Reset L1 counter so Level 1 is tried again before the next L2.
                no_mem_l1_count_ = 0;
                LOG_INFO("RX_CONN",
                         "L2 WiFi restart OK (attempt %u/%d) -- AP rejoin in progress",
                         static_cast<unsigned>(no_mem_l2_count_),
                         RXCONN_NO_MEM_L2_THRESHOLD);
            } else {
                LOG_ERROR("RX_CONN",
                          "L2 esp_now_init after WiFi restart failed: %s",
                          esp_err_to_name(l2_err));
            }

        } else {
            // ── Level 3: all recovery exhausted — reboot ──────────────────
            LOG_ERROR("RX_CONN",
                      "[STATE_CHANGE] RECOVERY -> L3_REBOOT  "
                      "reason=l2_exhausted  l1_attempts=%u  l2_attempts=%u",
                      static_cast<unsigned>(no_mem_l1_count_),
                      static_cast<unsigned>(no_mem_l2_count_));
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
    }

    const uint32_t now = millis();
    auto& connection_manager = EspNowConnectionManager::instance();

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
