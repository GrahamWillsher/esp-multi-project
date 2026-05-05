#pragma once

#include "connection_event.h"
#include "connection_manager.h"
#include "rx_catalog_retry_policy.h"
#include "rx_deferred_peer_policy.h"
#include "rx_led_sync_policy.h"
#include <cstdint>

class ReceiverConnectionHandler;

struct ReceiverConnectionHandlerHooks {
    void* context = nullptr;
    void (*on_connected)(void* context) = nullptr;
    void (*on_connection_lost)(void* context) = nullptr;
    void (*disconnect_mqtt)(void* context) = nullptr;
    void (*on_config_update_sent)(void* context) = nullptr;
    bool (*send_initialization_burst)(void* context, const uint8_t* transmitter_mac) = nullptr;
    bool (*has_recent_power_data)(void* context,
                                  const ReceiverConnectionHandler& handler,
                                  uint32_t now_ms,
                                  uint32_t freshness_ms) = nullptr;
    void (*run_project_specific_tick)(void* context,
                                      ReceiverConnectionHandler& handler,
                                      uint32_t now_ms) = nullptr;
    /**
     * @brief Called after ESP-NOW stack reinitialisation to re-register the
     *        esp_now_register_send_cb() callback.  esp_now_deinit() removes all
     *        registered callbacks; the project must re-install its own.
     *        If nullptr, the send callback is not re-registered (ACK tracking
     *        will not work until the next full boot).
     */
    void (*reinstall_send_cb)(void* context) = nullptr;
};

struct ReceiverConnectionHandlerConfig {
    uint32_t heartbeat_timeout_ms = 32000;
    uint32_t quiet_exit_heartbeat_fresh_ms = 2500;
    uint32_t quiet_exit_min_hold_ms = 2000;
    uint32_t request_retry_timeout_ms = 3000;
    uint32_t request_retry_interval_ms = 15000;
    uint32_t power_data_freshness_ms = 8000;
    uint32_t post_connect_init_grace_ms = 2000;
    uint32_t config_retry_interval_ms = 30000;
};

class ReceiverConnectionHandler {
public:
    static ReceiverConnectionHandler& instance();

    void configure_hooks(const ReceiverConnectionHandlerHooks& hooks);
    void configure(const ReceiverConnectionHandlerConfig& config);
    void init();

    void on_probe_received(const uint8_t* transmitter_mac);
    void on_peer_registered(const uint8_t* transmitter_mac);
    void on_data_received(const uint8_t* transmitter_mac);
    void on_link_activity(const uint8_t* transmitter_mac);
    void on_connection_lost();
    void tick();

    void on_power_data_received();
    void on_transmitter_reboot_detected();
    void on_ack_send_pressure(const char* reason);
    void on_type_catalog_versions_received();
    void on_led_state_received();
    void on_config_update_sent();

    uint32_t get_last_rx_time_ms() const { return last_rx_time_ms_; }
    const uint8_t* get_transmitter_mac() const { return transmitter_mac_; }

    uint32_t connected_at_ms() const { return connected_at_ms_; }
    uint32_t last_config_retry_ms() const { return last_config_retry_ms_; }
    void set_last_config_retry_ms(uint32_t value) { last_config_retry_ms_ = value; }

    bool power_data_confirmed() const { return power_data_confirmed_; }
    void set_power_data_confirmed(bool value) { power_data_confirmed_ = value; }

    bool quiet_mode_active() const { return !EspNowConnectionManager::instance().is_connected(); }
    uint32_t power_data_freshness_ms() const { return config_.power_data_freshness_ms; }
    uint32_t config_retry_interval_ms() const { return config_.config_retry_interval_ms; }

    RxDeferredPeerPolicy& deferred_peer_policy() { return deferred_peer_; }
    RxLedSyncPolicy& led_sync_policy() { return led_sync_; }
    RxCatalogRetryPolicy& catalog_retry_policy() { return catalog_retry_; }

private:
    ReceiverConnectionHandler();

    void send_initialization_requests(const uint8_t* transmitter_mac);
    void flush_deferred_peer_registered();
    void begin_reconnect_diagnostics(uint32_t now_ms);
    void end_reconnect_diagnostics(uint32_t now_ms, const char* outcome);

    ReceiverConnectionHandlerHooks hooks_{};
    ReceiverConnectionHandlerConfig config_{};

    uint8_t transmitter_mac_[6];
    uint32_t last_rx_time_ms_ = 0;
    uint8_t pending_peer_cleanup_mac_[6] = {0};
    bool pending_peer_cleanup_ = false;

    bool first_data_received_ = false;
    bool init_pending_ = false;

    bool power_data_confirmed_ = false;
    uint32_t connected_at_ms_ = 0;
    uint32_t last_retry_ms_ = 0;
    uint32_t last_config_retry_ms_ = 0;

    bool had_connected_session_ = false;

    bool reconnect_diag_active_ = false;
    uint32_t reconnect_diag_start_ms_ = 0;
    uint32_t reconnect_diag_first_probe_ms_ = 0;
    uint32_t reconnect_diag_quiet_enter_ms_ = 0;

    uint32_t reconnect_diag_base_discovery_ack_attempts_ = 0;
    uint32_t reconnect_diag_base_discovery_ack_no_mem_failures_ = 0;
    uint32_t reconnect_diag_base_discovery_ack_other_failures_ = 0;

    uint32_t reconnect_diag_base_hb_ack_attempts_ = 0;
    uint32_t reconnect_diag_base_hb_ack_no_mem_failures_ = 0;
    uint32_t reconnect_diag_base_hb_ack_other_failures_ = 0;

    uint32_t reconnect_diag_base_sched_fail_control_ = 0;
    uint32_t reconnect_diag_base_sched_fail_discovery_ = 0;
    uint32_t reconnect_diag_base_sched_fail_data_ = 0;
    uint32_t reconnect_diag_base_sched_fail_monitoring_ = 0;

    RxDeferredPeerPolicy deferred_peer_;
    RxLedSyncPolicy led_sync_;
    RxCatalogRetryPolicy catalog_retry_;

    // NO_MEM recovery level counters (reset on CONNECTED)
    uint8_t  no_mem_l1_count_        = 0;
    uint8_t  no_mem_l2_count_        = 0;
    uint32_t no_mem_last_reinit_ms_  = 0;
};
