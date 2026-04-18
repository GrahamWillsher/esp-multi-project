/**
 * @file rx_catalog_retry_policy.h
 * @brief Per-category bounded retry policy for ESP-NOW receiver type-catalog requests.
 *
 * On connection, the receiver requests the transmitter's type-catalog version
 * numbers, then selectively fetches the battery, inverter, and inverter-interface
 * catalogs if stale or empty.  This policy manages the retry state for all four
 * categories and enforces a bounded retry count to prevent indefinite spamming.
 *
 * Design:
 *  - Header-only, no dynamic memory, no FreeRTOS dependencies.
 *  - Uses raw function pointers — compatible with embedded targets that restrict
 *    dynamic allocation (i.e. avoid std::function on ISR-touching code paths).
 *  - Caller supplies time (millis()) and evaluates freshness predicates inline.
 *  - Thread safety is the caller's responsibility.
 */

#pragma once

#include <cstdint>

/**
 * @class RxCatalogRetryPolicy
 *
 * Usage (inside the connection-handler tick):
 *
 *   if (catalog_retry_.is_due(now, connected_at_ms_)) {
 *       catalog_retry_.tick_item("versions",
 *           !catalog_retry_.versions_received(),
 *           &send_type_catalog_versions_request,
 *           catalog_retry_.versions_retry_count, now);
 *       catalog_retry_.tick_item("battery",
 *           !TypeCatalogCache::has_battery_entries() || TypeCatalogCache::battery_refresh_required(),
 *           &send_battery_types_request,
 *           catalog_retry_.battery_retry_count, now);
 *       // … inverter, interfaces …
 *       catalog_retry_.mark_ticked(now);
 *   }
 */
class RxCatalogRetryPolicy {
public:
    /**
     * @brief Returns true when the retry interval has elapsed and requests
     *        should be evaluated.
     *
     * @param now_ms        Current time in milliseconds.
     * @param connected_ms  Timestamp when CONNECTED was entered.
     */
    bool is_due(uint32_t now_ms, uint32_t connected_ms) const {
        if (connected_ms == 0) {
            return false;
        }
        if ((now_ms - connected_ms) < INITIAL_DELAY_MS) {
            return false;
        }
        if (last_retry_ms_ != 0 && (now_ms - last_retry_ms_) < INTERVAL_MS) {
            return false;
        }
        return true;
    }

    /**
     * @brief Record that a retry pass was just executed.
     * @param now_ms  Current time in milliseconds.
     */
    void mark_ticked(uint32_t now_ms) {
        last_retry_ms_ = now_ms;
    }

    /**
     * @brief Evaluate and optionally retry a single catalog item.
     *
     * @param label         Human-readable label for logging.
     * @param needs_request True if this item should be requested/re-requested.
     * @param send_fn       Free function that sends the request; returns true on success.
     * @param retry_count   Reference to the per-item retry counter (stored in caller).
     * @param now_ms        Current time (unused here; reserved for future per-item timing).
     * @return true if send_fn was called and succeeded.
     */
    bool tick_item(const char* /*label*/, bool needs_request,
                   bool (*send_fn)(), uint8_t& retry_count, uint32_t /*now_ms*/) {
        if (!needs_request || retry_count >= MAX_RETRIES) {
            return false;
        }
        if (!send_fn()) {
            return false;
        }
        ++retry_count;
        return true;
    }

    /**
     * @brief Mark that catalog version numbers have been received from the TX.
     *        Stops the versions-request retry item.
     */
    void on_versions_received() {
        versions_received_ = true;
    }

    /**
     * @brief Reset all state. Call on connection lost.
     */
    void reset() {
        versions_received_    = false;
        last_retry_ms_        = 0;
        versions_retry_count  = 0;
        battery_retry_count   = 0;
        inverter_retry_count  = 0;
        interface_retry_count = 0;
    }

    bool versions_received() const { return versions_received_; }

    /** Delay after CONNECTED before first catalog retry pass. */
    static constexpr uint32_t INITIAL_DELAY_MS = 2500;
    /** Interval between retry passes. */
    static constexpr uint32_t INTERVAL_MS       = 3000;
    /** Maximum retry attempts per catalog item. */
    static constexpr uint8_t  MAX_RETRIES        = 8;

    // Per-item retry counters — public so tick_item() can receive them by reference
    // without requiring heap allocation or std::function captures.
    uint8_t versions_retry_count  = 0;
    uint8_t battery_retry_count   = 0;
    uint8_t inverter_retry_count  = 0;
    uint8_t interface_retry_count = 0;

private:
    bool     versions_received_ = false;
    uint32_t last_retry_ms_     = 0;
};
