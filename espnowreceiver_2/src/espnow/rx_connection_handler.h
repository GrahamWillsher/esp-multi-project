/**
 * @file rx_connection_handler.h
 * @brief Receiver-specific connection handler (DEVICE-SPECIFIC)
 * 
 * Responsibilities (RX only):
 * - Track transmitter MAC
 * - Update last receive timestamp
 * - Post events to common connection manager
 * 
 * This class contains NO state machine logic.
 * All state transitions are handled by EspNowConnectionManager (common code).
 */

#pragma once

#include <cstdint>

class ReceiverConnectionHandler {
public:
    static ReceiverConnectionHandler& instance();

    /**
     * @brief Initialize handler state
     */
    void init();

    /**
     * @brief Called when PROBE received from transmitter
     * @param transmitter_mac Transmitter MAC address
     */
    void on_probe_received(const uint8_t* transmitter_mac);

    /**
     * @brief Called when peer registration is complete
     * @param transmitter_mac Transmitter MAC address
     */
    void on_peer_registered(const uint8_t* transmitter_mac);

    /**
     * @brief Called when any data message is received
     * @param transmitter_mac Transmitter MAC address
     */
    void on_data_received(const uint8_t* transmitter_mac);

    /**
     * @brief Called on any ESP-NOW traffic from the transmitter.
     * Updates link-activity timing and caches the latest transmitter MAC even
     * when the message is only a heartbeat or version beacon.
     */
    void on_link_activity(const uint8_t* transmitter_mac);

    /**
     * @brief Called when connection is lost
     * Resets first_data_received flag to allow re-initialization on reconnect
     */
    void on_connection_lost();

    /**
     * @brief Periodic tick — retries REQUEST_DATA if the power-profile stream has
     * not started within RETRY_REQUEST_TIMEOUT_MS of connecting.
     * Call from loop() at ~10 ms cadence.
     */
    void tick();

    /**
     * @brief Called by data consumer to signal that power-profile data has arrived.
     * Stops the retry timer.
     */
    void on_power_data_received();

    /**
     * @brief Called when TX reboot is detected from heartbeat sequence regression.
     * Re-arms REQUEST_DATA retries immediately so stream recovery does not wait
     * for full stale-state timeout.
     */
    void on_transmitter_reboot_detected();

    /**
     * @brief Mark catalog version response as received.
     */
    void on_type_catalog_versions_received();

    /**
     * @brief Get last receive timestamp
     * @return Milliseconds since boot of last received message
     */
    uint32_t get_last_rx_time_ms() const { return last_rx_time_ms_; }

    /**
     * @brief Get transmitter MAC
     */
    const uint8_t* get_transmitter_mac() const { return transmitter_mac_; }

    /**
     * @brief Called when sending config updates back to transmitter.
     * Signals RxStateMachine to extend stale detection grace window.
     */
    void on_config_update_sent();

private:
    ReceiverConnectionHandler();

    uint8_t transmitter_mac_[6];
    uint32_t last_rx_time_ms_;
    bool first_data_received_ = false;  // Gate for initialization requests

    // REQUEST_DATA retry state
    bool power_data_confirmed_ = false; // True once first power-profile packet received
    uint32_t connected_at_ms_   = 0;    // millis() when CONNECTED was entered
    uint32_t last_retry_ms_     = 0;    // millis() of last REQUEST_DATA retry send
    bool peer_registered_event_posted_ = false;  // Deduplicate PEER_REGISTERED posts while CONNECTING
    bool peer_registered_deferred_ = false;      // Latch deferred PEER_REGISTERED until CONNECTING
    uint8_t deferred_peer_mac_[6] = {0};
    uint32_t deferred_peer_registered_ms_ = 0;
    static constexpr uint32_t RETRY_REQUEST_TIMEOUT_MS = 3000;  // Retry if no data in 3 s
    static constexpr uint32_t RETRY_INTERVAL_MS        = 2000;  // Re-send every 2 s while retrying
    static constexpr uint32_t POWER_DATA_FRESHNESS_MS  = 8000;  // Consider stream active only if data seen recently
    static constexpr uint32_t DEFERRED_PEER_TTL_MS     = 5000;  // Drop stale deferred PEER_REGISTERED

    // Catalog retry policy
    uint32_t last_catalog_retry_ms_ = 0;
    bool catalog_versions_received_ = false;
    uint8_t versions_retry_count_ = 0;
    uint8_t battery_catalog_retry_count_ = 0;
    uint8_t inverter_catalog_retry_count_ = 0;
    uint8_t inverter_interface_retry_count_ = 0;
    static constexpr uint32_t CATALOG_RETRY_INITIAL_DELAY_MS = 2500;
    static constexpr uint32_t CATALOG_RETRY_INTERVAL_MS = 3000;
    static constexpr uint8_t CATALOG_MAX_RETRIES = 8;

    // Send initialization requests when connection state is confirmed
    // Called by state machine callback when entering CONNECTED state
    void send_initialization_requests(const uint8_t* transmitter_mac);

    // Drain deferred PEER_REGISTERED when CONNECTING state is active
    void flush_deferred_peer_registered();
};
