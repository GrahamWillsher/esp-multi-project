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
     * 
     * PHASE 0: Tries cached channel first for fast reconnection,
     * then falls back to full channel hopping if needed.
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
     * @brief Periodic update for deferred/backoff-aware discovery starts
     *
     * Call from main loop. Keeps reconnect behavior progressing even when
     * immediate discovery start is deferred by backoff policy.
     */
    void tick();

    /**
     * @brief Get receiver MAC
     */
    const uint8_t* get_receiver_mac() const { return receiver_mac_; }

    /**
     * @brief Get receiver channel
     */
    uint8_t get_receiver_channel() const { return receiver_channel_; }
    
    /**
     * @brief Get last known channel (for fast reconnection)
     * @return Last successful channel, or 0 if unknown
     */
    uint8_t get_last_known_channel() const { return last_known_channel_; }

private:
    TransmitterConnectionHandler();

    uint8_t receiver_mac_[6];
    uint8_t receiver_channel_;
    uint8_t last_known_channel_;      // PHASE 0: Cache for fast reconnection
    bool has_cached_channel_;          // PHASE 0: Valid cache flag

    // Deferred discovery start when backoff indicates "not yet"
    bool deferred_discovery_start_{false};
    uint32_t deferred_discovery_due_ms_{0};
    bool peer_register_event_posted_{false};
    bool peer_registered_deferred_{false};
    uint8_t deferred_peer_mac_[6]{0};
    uint32_t deferred_peer_registered_ms_{0};
    static constexpr uint32_t DEFERRED_PEER_TTL_MS = 5000;

    // Start hopping task only (no state event posting)
    void start_discovery_hopping_only();

    // Drain deferred PEER_REGISTERED when CONNECTING state is active
    void flush_deferred_peer_registered();
};
