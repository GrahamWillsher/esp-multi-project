/**
 * @file rx_deferred_peer_policy.h
 * @brief Deferred PEER_REGISTERED event policy for ESP-NOW receivers.
 *
 * Handles the race condition where a PEER_REGISTERED event arrives before the
 * connection state machine has entered the CONNECTING state. The event is latched
 * with a TTL and flushed when CONNECTING is subsequently entered.
 *
 * Design:
 *  - Header-only, no dynamic memory, no FreeRTOS dependencies.
 *  - Caller is responsible for all mutex/critical section protection if needed.
 *  - All time values are in milliseconds from Arduino millis() / FreeRTOS tick.
 */

#pragma once

#include <cstdint>
#include <cstring>

/**
 * @class RxDeferredPeerPolicy
 *
 * Usage:
 *   1. Call on_peer_registered() each time a PEER_REGISTERED signal arrives.
 *      Returns true if the event should be posted to the connection manager now.
 *   2. Call try_flush() periodically (e.g. in tick()) while CONNECTING.
 *      Returns true and fills out_mac if a deferred event is ready to post.
 *   3. Call on_event_posted() immediately after posting the event.
 *   4. Call reset() on connection established or lost.
 */
class RxDeferredPeerPolicy {
public:
    /**
     * @brief Called when a PEER_REGISTERED event arrives.
     *
     * @param mac            Source MAC of the registering peer.
     * @param is_connecting  True if the connection manager is currently CONNECTING.
     * @param now_ms         Current time in milliseconds.
     * @return true if the caller should post PEER_REGISTERED to the connection manager now.
     *         false if the event was deferred or dropped.
     */
    bool on_peer_registered(const uint8_t* mac, bool is_connecting, uint32_t now_ms) {
        if (event_posted_) {
            return false;  // Deduplicate — only one PEER_REGISTERED per CONNECTING window
        }

        if (is_connecting) {
            deferred_ = false;
            return true;  // Post immediately
        }

        // Not yet CONNECTING — latch with TTL if the MAC is non-zero
        if (mac && has_valid_mac(mac)) {
            memcpy(deferred_mac_, mac, 6);
            deferred_ms_ = now_ms;
            deferred_ = true;
        }
        return false;
    }

    /**
     * @brief Attempt to flush a deferred PEER_REGISTERED event.
     *
     * @param is_connecting  True if the connection manager is currently CONNECTING.
     * @param now_ms         Current time in milliseconds.
     * @param out_mac        Output buffer (6 bytes) to receive the deferred MAC.
     * @return true if a deferred event should be posted now (out_mac is populated).
     */
    bool try_flush(bool is_connecting, uint32_t now_ms, uint8_t out_mac[6]) {
        if (!deferred_) {
            return false;
        }

        // Expire stale deferred events
        if (deferred_ms_ > 0 && (now_ms - deferred_ms_) > TTL_MS) {
            deferred_ = false;
            deferred_ms_ = 0;
            memset(deferred_mac_, 0, sizeof(deferred_mac_));
            return false;
        }

        if (!is_connecting || event_posted_) {
            return false;
        }

        memcpy(out_mac, deferred_mac_, 6);
        return true;
    }

    /**
     * @brief Must be called immediately after successfully posting the event.
     *        Clears the deferred latch and marks the event as posted for this window.
     */
    void on_event_posted() {
        event_posted_ = true;
        deferred_ = false;
        deferred_ms_ = 0;
        memset(deferred_mac_, 0, sizeof(deferred_mac_));
    }

    /**
     * @brief Reset all state. Call on connection established, lost, or re-start.
     */
    void reset() {
        event_posted_ = false;
        deferred_ = false;
        deferred_ms_ = 0;
        memset(deferred_mac_, 0, sizeof(deferred_mac_));
    }

    bool is_deferred() const { return deferred_; }
    bool event_posted() const { return event_posted_; }

    /** TTL for a deferred PEER_REGISTERED event — drop if not flushed within this window. */
    static constexpr uint32_t TTL_MS = 5000;

private:
    static bool has_valid_mac(const uint8_t* mac) {
        for (int i = 0; i < 6; ++i) {
            if (mac[i] != 0) return true;
        }
        return false;
    }

    bool    event_posted_ = false;
    bool    deferred_     = false;
    uint8_t deferred_mac_[6] = {};
    uint32_t deferred_ms_   = 0;
};
