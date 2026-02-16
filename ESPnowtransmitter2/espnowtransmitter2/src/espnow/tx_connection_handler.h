/**
 * @file tx_connection_handler.h
 * @brief Transmitter-specific connection handler (DEVICE-SPECIFIC)
 * 
 * Responsibilities (TX only):
 * - Start active discovery (channel hopping)
 * - Track receiver MAC/channel
 * - Post events to common connection manager
 * 
 * This class contains NO state machine logic.
 * All state transitions are handled by EspNowConnectionManager (common code).
 */

#pragma once

#include <cstdint>

class TransmitterConnectionHandler {
public:
    static TransmitterConnectionHandler& instance();

    /**
     * @brief Initialize handler and register state callbacks
     */
    void init();

    /**
     * @brief Start discovery (active channel hopping)
     * 
     * Posts CONNECTION_START event to common manager and
     * starts DiscoveryTask background hopping.
     */
    void start_discovery();

    /**
     * @brief Notify that ACK was received from receiver
     * @param receiver_mac Receiver MAC address
     * @param channel Receiver channel
     */
    void on_ack_received(const uint8_t* receiver_mac, uint8_t channel);

    /**
     * @brief Notify that receiver peer was registered
     * @param receiver_mac Receiver MAC address
     */
    void on_peer_registered(const uint8_t* receiver_mac);

    /**
     * @brief Get receiver MAC
     */
    const uint8_t* get_receiver_mac() const { return receiver_mac_; }

    /**
     * @brief Get receiver channel
     */
    uint8_t get_receiver_channel() const { return receiver_channel_; }

private:
    TransmitterConnectionHandler();

    uint8_t receiver_mac_[6];
    uint8_t receiver_channel_;
};
