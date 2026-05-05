#pragma once

/**
 * @file discovery_task.h
 * @brief Pure ESP-NOW channel-scan utility.
 *
 * RESPONSIBILITIES (post-refactor)
 * ─────────────────────────────────
 *  • run_hop_scan()   — synchronous channel-hop scan called by the hop-worker
 *                       task.  No FreeRTOS task is created here.
 *  • validate_state() — steady-state channel/peer health check (CONNECTED only).
 *  • audit_peer_state() — diagnostic dump of all registered peers.
 *
 * WHAT IS NO LONGER HERE
 * ───────────────────────
 *  • active_hopping_running_ flag (was a cross-core cache-coherence hazard)
 *  • start/stop_active_channel_hopping() (task lifecycle owned by TxReconnectManager)
 *  • restart() / update_recovery() / RecoveryState machinery
 *  • All deferred-registration and backoff logic
 *
 * All reconnect orchestration now lives in TxReconnectManager.
 */

#include <cstdint>
#include <freertos/FreeRTOS.h>

// ─────────────────────────────────────────────────────────────────────────────
// Metrics (informational only — no operational decisions made from these)
// ─────────────────────────────────────────────────────────────────────────────
struct DiscoveryMetrics {
    uint32_t total_scans             {0};
    uint32_t successful_scans        {0};
    uint32_t failed_scans            {0};
    uint32_t channel_mismatches      {0};
    uint32_t last_success_channel    {0};
    uint32_t last_success_timestamp  {0};
    uint32_t longest_scan_ms         {0};

    void log_summary() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// DiscoveryTask
// ─────────────────────────────────────────────────────────────────────────────
class DiscoveryTask {
public:
    static DiscoveryTask& instance();

    /**
     * @brief Run a full two-phase channel-hop scan (blocking).
     *
     * Called from the hop-worker task on Core 1.
     * Phase 1: fast sweep of all 13 channels starting from start_channel_hint.
     * Phase 2: weighted dwell on last-known +/- 1 channel.
     *
     * Sets the WiFi channel to the found channel before returning.
     * Does NOT add the ESP-NOW peer -- that is done by TxReconnectManager.
     *
     * @param start_channel_hint  First channel to try (0 = start from ch 1).
     * @param out_channel         [out] Channel on which ACK was received.
     * @param out_mac             [out] Sender MAC (6 bytes).
     * @return true if receiver was found; false if full scan yielded no ACK.
     */
    bool run_hop_scan(uint8_t  start_channel_hint,
                      uint8_t* out_channel,
                      uint8_t* out_mac);

    /**
     * @brief Check steady-state channel/peer consistency.
     *        Only call when CONNECTED and no scan is in progress.
     * @return true if state is healthy.
     */
    bool validate_state() const;

    /**
     * @brief Log all registered ESP-NOW peers and current WiFi channel.
     */
    void audit_peer_state() const;

    /** @brief Access accumulated scan metrics. */
    const DiscoveryMetrics& get_metrics() const { return metrics_; }

    // Non-copyable singleton
    DiscoveryTask(const DiscoveryTask&) = delete;
    DiscoveryTask& operator=(const DiscoveryTask&) = delete;

private:
    DiscoveryTask() = default;

    // -- Internal helpers --------------------------------------------------
    bool   active_channel_hop_scan_impl(uint8_t  start_channel_hint,
                                        uint8_t* out_channel,
                                        uint8_t* out_mac);
    bool   scan_channel_for_ack(uint8_t ch,
                                uint32_t dwell_ms,
                                const char* phase_label,
                                uint8_t* out_mac);
    void   send_probe_on_channel(uint8_t channel);
    bool   force_and_verify_channel(uint8_t target_channel);

    // -- Instance state (metrics only -- never shared across tasks) --------
    DiscoveryMetrics metrics_;
};
