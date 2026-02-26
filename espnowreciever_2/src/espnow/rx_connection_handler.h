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
     * @brief Called when connection is lost
     * Resets first_data_received flag to allow re-initialization on reconnect
     */
    void on_connection_lost();

    /**
     * @brief Get last receive timestamp
     * @return Milliseconds since boot of last received message
     */
    uint32_t get_last_rx_time_ms() const { return last_rx_time_ms_; }

    /**
     * @brief Get transmitter MAC
     */
    const uint8_t* get_transmitter_mac() const { return transmitter_mac_; }

private:
    ReceiverConnectionHandler();

    uint8_t transmitter_mac_[6];
    uint32_t last_rx_time_ms_;
    bool first_data_received_ = false;  // Gate for initialization requests

    // Send initialization requests when connection state is confirmed
    // Called by state machine callback when entering CONNECTED state
    void send_initialization_requests(const uint8_t* transmitter_mac);
};
