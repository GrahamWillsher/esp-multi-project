/**
 * @file tx_connection_handler.h
 * @brief Transmitter connection handler -- thin bridge.
 *
 * Post-refactor responsibilities:
 *  - Register EspNowConnectionManager state-change callbacks.
 *  - Forward CONNECTING events to TxReconnectManager.
 *  - Cache receiver MAC/channel for use by other components.
 *  - Post PEER_REGISTERED to EspNowConnectionManager when called by
 *    TxReconnectManager after a successful scan.
 *
 * All reconnect orchestration is owned by TxReconnectManager.
 * No backoff, no deferred-registration, no cross-task state flags.
 */

#pragma once

#include <cstdint>

class TransmitterConnectionHandler {
public:
    static TransmitterConnectionHandler& instance();

    /** Register callbacks and configure heartbeat timeout. */
    void init();

    /**
     * @brief Kick off initial connection by posting CONNECTION_START.
     *        Called once from setup().
     */
    void start_discovery();

    /**
     * @brief Called by TxReconnectManager::handle_worker_found().
     *        Updates cached receiver_mac_ and receiver_channel_.
     */
    void on_ack_received(const uint8_t* receiver_mac, uint8_t channel);

    /**
     * @brief Called by TxReconnectManager::handle_worker_found().
     *        Posts PEER_REGISTERED to EspNowConnectionManager to drive
     *        CONNECTING -> CONNECTED transition.
     */
    void on_peer_registered(const uint8_t* receiver_mac);

    /** @brief No-op -- kept for call-site compatibility during migration. */
    void tick() {}

    /** @brief Receiver MAC set on last successful connection. */
    const uint8_t* get_receiver_mac()     const { return receiver_mac_; }

    /** @brief Receiver WiFi channel set on last successful connection. */
    uint8_t        get_receiver_channel() const { return receiver_channel_; }

private:
    TransmitterConnectionHandler();

    uint8_t  receiver_mac_[6];
    uint8_t  receiver_channel_{0};
};
