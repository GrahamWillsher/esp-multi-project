/**
 * @file rx_led_sync_policy.h
 * @brief Bounded retry policy for ESP-NOW receiver LED state synchronisation.
 *
 * On connection, the receiver requests the transmitter's current LED state.
 * If no response arrives, the request is retried up to MAX_ATTEMPTS times at
 * RETRY_INTERVAL_MS cadence.  Once a response is received or retries are
 * exhausted the pending flag is cleared.
 *
 * Design:
 *  - Header-only, no dynamic memory, no FreeRTOS dependencies.
 *  - Caller supplies time (millis()) and the send callback.
 *  - Thread safety is the caller's responsibility.
 */

#pragma once

#include <cstdint>

/**
 * @class RxLedSyncPolicy
 *
 * Usage:
 *   1. Call arm() when the initial LED state request has been sent (or attempted).
 *   2. Call tick() periodically — it returns true when a retry should be sent.
 *      After calling tick() and sending the retry, call mark_sent().
 *   3. Call on_response_received() when a flash_led message arrives from the TX.
 *   4. Call reset() on connection lost.
 */
class RxLedSyncPolicy {
public:
    /**
     * @brief Arm the retry engine after the first request.
     *
     * @param initial_sent  True if the first request was successfully sent.
     * @param now_ms        Current time in milliseconds.
     */
    void arm(bool initial_sent, uint32_t now_ms) {
        pending_      = true;
        attempts_     = initial_sent ? 1u : 0u;
        last_sent_ms_ = initial_sent ? now_ms : 0u;
    }

    /**
     * @brief Periodic tick — check whether a retry should be attempted.
     *
     * Returns true when the caller should send another LED state request.
     * The caller must then call mark_sent() to record the attempt.
     *
     * @param now_ms  Current time in milliseconds.
     * @return true   if a retry request should be sent now.
     */
    bool tick(uint32_t now_ms) {
        if (!pending_) {
            return false;
        }

        if (attempts_ >= MAX_ATTEMPTS) {
            pending_ = false;  // Give up silently — caller may log the exhaustion
            return false;
        }

        if (last_sent_ms_ != 0 && (now_ms - last_sent_ms_) < RETRY_INTERVAL_MS) {
            return false;  // Too soon
        }

        return true;  // Caller should send and then call mark_sent()
    }

    /**
     * @brief Record that a retry request was just sent.
     * @param now_ms  Current time in milliseconds.
     */
    void mark_sent(uint32_t now_ms) {
        ++attempts_;
        last_sent_ms_ = now_ms;
    }

    /**
     * @brief Call when an authoritative LED state message is received from the TX.
     *        Clears the pending flag.
     */
    void on_response_received() {
        pending_ = false;
    }

    /**
     * @brief Reset all state. Call on connection lost.
     */
    void reset() {
        pending_      = false;
        attempts_     = 0;
        last_sent_ms_ = 0;
    }

    bool    is_pending()     const { return pending_; }
    uint8_t attempt_count()  const { return attempts_; }

    /** Interval between retry attempts. */
    static constexpr uint32_t RETRY_INTERVAL_MS = 500;
    /** Maximum number of send attempts before giving up. */
    static constexpr uint8_t  MAX_ATTEMPTS       = 3;

private:
    bool     pending_      = false;
    uint8_t  attempts_     = 0;
    uint32_t last_sent_ms_ = 0;
};
