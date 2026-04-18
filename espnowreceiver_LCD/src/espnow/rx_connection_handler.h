/**
 * @file rx_connection_handler.h
 * @brief Receiver-specific connection handler (LCD device — Waveshare ESP32-S3 7")
 *
 * Responsibilities (RX only):
 * - Track transmitter MAC
 * - Update last receive timestamp
 * - Post events to common connection manager
 *
 * This class contains NO state machine logic.
 * All state transitions are handled by EspNowConnectionManager (common code).
 *
 * Policy objects from esp32common are used for:
 *  - RxDeferredPeerPolicy   — deferred PEER_REGISTERED latch/flush with TTL
 *  - RxLedSyncPolicy        — bounded LED state sync retry
 *  - RxCatalogRetryPolicy   — per-category bounded catalog retry
 */

#pragma once

#include <cstdint>
#include <esp32common/espnow/rx_deferred_peer_policy.h>
#include <esp32common/espnow/rx_led_sync_policy.h>
#include <esp32common/espnow/rx_catalog_retry_policy.h>

class ReceiverConnectionHandler {
public:
    static ReceiverConnectionHandler& instance();

    /** @brief Initialize handler state and register state callbacks. */
    void init();

    /** @brief Called when PROBE received from transmitter. */
    void on_probe_received(const uint8_t* transmitter_mac);

    /** @brief Called when peer registration is complete. */
    void on_peer_registered(const uint8_t* transmitter_mac);

    /** @brief Called when any data message is received. */
    void on_data_received(const uint8_t* transmitter_mac);

    /**
     * @brief Called on any ESP-NOW traffic from the transmitter.
     * Updates link-activity timing and caches the latest transmitter MAC.
     */
    void on_link_activity(const uint8_t* transmitter_mac);

    /** @brief Called when connection is lost. Resets all retry state. */
    void on_connection_lost();

    /**
     * @brief Periodic tick — retries REQUEST_DATA, catalog requests, and LED sync.
     * Call from loop() at ~10 ms cadence.
     */
    void tick();

    /** @brief Called by data consumer to signal that power-profile data has arrived. */
    void on_power_data_received();

    /**
     * @brief Called when TX reboot is detected from heartbeat sequence regression.
     * Re-arms REQUEST_DATA retries immediately.
     */
    void on_transmitter_reboot_detected();

    /** @brief Mark catalog version response as received. */
    void on_type_catalog_versions_received();

    /** @brief Mark LED sync as satisfied when authoritative LED state is received. */
    void on_led_state_received();

    /**
     * @brief Called when sending config updates back to transmitter.
     * Signals RxStateMachine to extend stale detection grace window.
     */
    void on_config_update_sent();

    /** @brief Get last receive timestamp (ms since boot). */
    uint32_t get_last_rx_time_ms() const { return last_rx_time_ms_; }

    /** @brief Get current transmitter MAC. */
    const uint8_t* get_transmitter_mac() const { return transmitter_mac_; }

private:
    ReceiverConnectionHandler();

    /** Send all required initialization messages after CONNECTED. */
    void send_initialization_requests(const uint8_t* transmitter_mac);

    /** Drain deferred PEER_REGISTERED when CONNECTING state is active. */
    void flush_deferred_peer_registered();

    // ---- Identity ----
    uint8_t  transmitter_mac_[6];
    uint32_t last_rx_time_ms_ = 0;

    // ---- Connection gate ----
    bool     first_data_received_ = false;

    // ---- REQUEST_DATA retry ----
    bool     power_data_confirmed_ = false;
    uint32_t connected_at_ms_      = 0;
    uint32_t last_retry_ms_        = 0;
    uint32_t last_config_retry_ms_ = 0;

    static constexpr uint32_t RETRY_REQUEST_TIMEOUT_MS = 3000;
    static constexpr uint32_t RETRY_INTERVAL_MS        = 2000;
    static constexpr uint32_t POWER_DATA_FRESHNESS_MS  = 8000;
    static constexpr uint32_t CONFIG_RETRY_INTERVAL_MS = 5000;

    // ---- Shared policy objects ----
    RxDeferredPeerPolicy  deferred_peer_;
    RxLedSyncPolicy       led_sync_;
    RxCatalogRetryPolicy  catalog_retry_;
};
