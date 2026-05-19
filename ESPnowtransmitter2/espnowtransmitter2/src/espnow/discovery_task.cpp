/**
 * @file discovery_task.cpp
 * @brief Pure ESP-NOW channel-scan implementation.
 *
 * This file contains only the mechanics of scanning channels and detecting
 * an ACK from the receiver.  All reconnect orchestration (task lifecycle,
 * backoff, state-machine events) lives in TxReconnectManager.
 *
 * run_hop_scan() is called synchronously from the hop-worker task (Core 1).
 * It blocks until the receiver ACKs or all channels are exhausted.
 */

#include "discovery_task.h"
#include "../config/task_config.h"
#include "../config/logging_config.h"
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/tx_scheduler.h>
#include "../queue/espnow_queue_manager.h"
#include <Arduino.h>
#include <espnow_transmitter.h>   // set_channel()
#include <esp32common/espnow/channel_authority.h>
#include <espnow_peer_manager.h>
#include <esp_wifi.h>
#include <WiFi.h>

// ============================================================================
// Module-level constants
// ============================================================================
namespace {

constexpr uint32_t kDiscoveryLoopPollMs    = 10;
constexpr uint32_t kPostChannelSettleDelayMs = 50;
constexpr uint32_t kMsPerSecond            = 1000;
constexpr uint32_t kSignalExtensionMs      = 500; ///< Dwell extension on probe activity

constexpr uint8_t kDiscoveryChannels[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
};
constexpr uint8_t kDiscoveryChannelCount =
    sizeof(kDiscoveryChannels) / sizeof(kDiscoveryChannels[0]);

}  // namespace

// ============================================================================
// Singleton
// ============================================================================
DiscoveryTask& DiscoveryTask::instance() {
    static DiscoveryTask inst;
    return inst;
}

// ============================================================================
// run_hop_scan — public entry point called by hop-worker task
// ============================================================================
bool DiscoveryTask::run_hop_scan(uint8_t  start_channel_hint,
                                  uint8_t* out_channel,
                                  uint8_t* out_mac) {
    const uint32_t scan_start_ms = millis();
    const uint32_t scan_probe_attempts_before = metrics_.probe_send_attempts;
    const uint32_t scan_probe_success_before = metrics_.probe_send_success;
    const uint32_t scan_probe_no_mem_before = metrics_.probe_send_no_mem_fail;
    const uint32_t scan_probe_other_fail_before = metrics_.probe_send_other_fail;
    const uint32_t scan_ack_received_before = metrics_.ack_frames_received;
    metrics_.total_scans++;

    const bool found = active_channel_hop_scan_impl(
        start_channel_hint, out_channel, out_mac);

    const uint32_t duration_ms = millis() - scan_start_ms;
    const uint32_t scan_probe_attempts =
        metrics_.probe_send_attempts - scan_probe_attempts_before;
    const uint32_t scan_probe_success =
        metrics_.probe_send_success - scan_probe_success_before;
    const uint32_t scan_probe_no_mem =
        metrics_.probe_send_no_mem_fail - scan_probe_no_mem_before;
    const uint32_t scan_probe_other_fail =
        metrics_.probe_send_other_fail - scan_probe_other_fail_before;
    const uint32_t scan_ack_received =
        metrics_.ack_frames_received - scan_ack_received_before;

    const uint32_t probes_per_second =
        (duration_ms > 0)
            ? static_cast<uint32_t>((static_cast<uint64_t>(scan_probe_success) * kMsPerSecond) / duration_ms)
            : 0;

    const uint32_t ack_per_100_probe =
        (scan_probe_success > 0)
            ? static_cast<uint32_t>((static_cast<uint64_t>(scan_ack_received) * 100U) / scan_probe_success)
            : 0;

    if (duration_ms > metrics_.longest_scan_ms) {
        metrics_.longest_scan_ms = duration_ms;
    }

    if (found) {
        metrics_.successful_scans++;
        metrics_.last_success_channel   = *out_channel;
        metrics_.last_success_timestamp = millis();
        LOG_INFO("DISCOVERY",
                 "Scan succeeded: ch=%d, duration=%lu ms, probes_ok=%lu, probes_no_mem=%lu, probes_other_fail=%lu, ack=%lu, probe_rate=%lu/s, ack_per_100_probe=%lu (total=%lu/%lu success/fail)",
                 *out_channel,
                 static_cast<unsigned long>(duration_ms),
                 static_cast<unsigned long>(scan_probe_success),
                 static_cast<unsigned long>(scan_probe_no_mem),
                 static_cast<unsigned long>(scan_probe_other_fail),
                 static_cast<unsigned long>(scan_ack_received),
                 static_cast<unsigned long>(probes_per_second),
                 static_cast<unsigned long>(ack_per_100_probe),
                 static_cast<unsigned long>(metrics_.successful_scans),
                 static_cast<unsigned long>(metrics_.failed_scans));
    } else {
        metrics_.failed_scans++;
        LOG_WARN("DISCOVERY",
                 "Scan failed: duration=%lu ms, probes_ok=%lu, probes_no_mem=%lu, probes_other_fail=%lu, ack=%lu, probe_rate=%lu/s, ack_per_100_probe=%lu (total=%lu/%lu success/fail)",
                 static_cast<unsigned long>(duration_ms),
                 static_cast<unsigned long>(scan_probe_success),
                 static_cast<unsigned long>(scan_probe_no_mem),
                 static_cast<unsigned long>(scan_probe_other_fail),
                 static_cast<unsigned long>(scan_ack_received),
                 static_cast<unsigned long>(probes_per_second),
                 static_cast<unsigned long>(ack_per_100_probe),
                 static_cast<unsigned long>(metrics_.successful_scans),
                 static_cast<unsigned long>(metrics_.failed_scans));
    }

    return found;
}

// ============================================================================
// active_channel_hop_scan_impl — two-phase channel sweep
// ============================================================================
bool DiscoveryTask::active_channel_hop_scan_impl(uint8_t  start_channel_hint,
                                                  uint8_t* out_channel,
                                                  uint8_t* out_mac) {
    // Resolve start index from hint
    uint8_t saved_ch = start_channel_hint;
    if (saved_ch < kDiscoveryChannels[0] ||
        saved_ch > kDiscoveryChannels[kDiscoveryChannelCount - 1]) {
        saved_ch = 0;
    }

    uint8_t start_index = 0;
    if (saved_ch >= kDiscoveryChannels[0] &&
        saved_ch <= kDiscoveryChannels[kDiscoveryChannelCount - 1]) {
        start_index = saved_ch - 1;
        LOG_INFO("DISCOVERY",
                 "═══ Scan: starting from hint ch=%d ═══", saved_ch);
    } else {
        LOG_INFO("DISCOVERY",
                 "═══ Scan: no hint — starting from ch=1 ═══");
    }

    const uint32_t BASE_DWELL_MS             = TimingConfig::TRANSMIT_DURATION_PER_CHANNEL_MS;
    const uint32_t WEIGHTED_CENTER_DWELL_MS  = BASE_DWELL_MS * 2U;
    const uint32_t WEIGHTED_NEIGHBOR_DWELL_MS = BASE_DWELL_MS + (BASE_DWELL_MS / 2U);

    bool    ack_received = false;
    uint8_t ack_channel  = 0;
    uint8_t ack_mac[6]   = {};

    // ── Phase 1: fast circular sweep of all 13 channels ──────────────────
    for (uint8_t offset = 0; offset < kDiscoveryChannelCount && !ack_received; ++offset) {
        const uint8_t i  = (start_index + offset) % kDiscoveryChannelCount;
        const uint8_t ch = kDiscoveryChannels[i];
        uint8_t reported_ch = 0;
        if (scan_channel_for_ack(ch, BASE_DWELL_MS, "phase1-fast", ack_mac, &reported_ch)) {
            ack_received = true;
            // Use the channel the receiver reported in its ACK payload (WiFi.channel()
            // on the receiver) as the authoritative connection channel.  This is the
            // receiver's real STA home channel, which may differ from the scan channel
            // (e.g. receiver briefly hopped to ch=7 to receive the probe but its STA
            // WiFi is locked to ch=6 by its router).
            ack_channel = (reported_ch >= 1 && reported_ch <= 13) ? reported_ch : ch;
            if (reported_ch != ch) {
                LOG_WARN("DISCOVERY",
                         "[phase1-fast] ACK reported ch=%d (scan was on ch=%d) — "
                         "using receiver home ch=%d",
                         reported_ch, ch, ack_channel);
            }
        }
    }

    // ── Phase 2: weighted sweep ± 1 around last-known channel ─────────────
    if (!ack_received && saved_ch != 0) {
        LOG_INFO("DISCOVERY",
                 "Phase 2: weighted sweep around ch=%d", saved_ch);

        const uint8_t candidates[3] = {
            saved_ch,
            static_cast<uint8_t>((saved_ch > 1)  ? (saved_ch - 1) : saved_ch),
            static_cast<uint8_t>((saved_ch < 13) ? (saved_ch + 1) : saved_ch)
        };

        for (uint8_t idx = 0; idx < 3 && !ack_received; ++idx) {
            // Skip boundary duplicates (ch=1 or ch=13)
            if (idx > 0 && candidates[idx] == candidates[idx - 1]) continue;

            const uint32_t dwell =
                (idx == 0) ? WEIGHTED_CENTER_DWELL_MS : WEIGHTED_NEIGHBOR_DWELL_MS;

            uint8_t reported_ch2 = 0;
            if (scan_channel_for_ack(candidates[idx], dwell, "phase2-weighted", ack_mac, &reported_ch2)) {
                ack_received = true;
                ack_channel = (reported_ch2 >= 1 && reported_ch2 <= 13) ? reported_ch2 : candidates[idx];
                if (reported_ch2 != candidates[idx]) {
                    LOG_WARN("DISCOVERY",
                             "[phase2-weighted] ACK reported ch=%d (scan was on ch=%d) — "
                             "using receiver home ch=%d",
                             reported_ch2, candidates[idx], ack_channel);
                }
            }
        }
    }

    if (!ack_received) {
        return false;
    }

    // ── Found: set WiFi channel and return ─────────────────────────────────
    // Setting the channel here (inside the worker task on Core 1) is safe;
    // the WiFi driver is internally locked.  The manager will subsequently
    // call ChannelManager::lock_channel() to record the change.
    esp_wifi_set_channel(ack_channel, WIFI_SECOND_CHAN_NONE);
    vTaskDelay(pdMS_TO_TICKS(kPostChannelSettleDelayMs));

    *out_channel = ack_channel;
    if (out_mac) memcpy(out_mac, ack_mac, 6);

    return true;
}

// ============================================================================
// scan_channel_for_ack — dwell on one channel, return true on ACK
// ============================================================================
bool DiscoveryTask::scan_channel_for_ack(uint8_t     ch,
                                          uint32_t    dwell_ms,
                                          const char* phase_label,
                                          uint8_t*    out_mac,
                                          uint8_t*    out_ack_channel) {
    LOG_INFO("DISCOVERY",
             "[%s] ch=%d dwell=%lu ms",
             phase_label, ch, static_cast<unsigned long>(dwell_ms));

    if (!force_and_verify_channel(ch)) {
        LOG_ERROR("DISCOVERY", "Channel set failed for ch=%d — skipping", ch);
        return false;
    }

    EspnowQueueManager::instance().flush_discovery_queue();

    const uint32_t PROBE_INTERVAL_MS = TimingConfig::PROBE_INTERVAL_MS;
    uint32_t start_time     = millis();
    uint32_t last_probe_ms  = 0;
    uint32_t effective_dwell = dwell_ms;

    while (millis() - start_time < effective_dwell) {
        // Periodic probe broadcast
        if (millis() - last_probe_ms >= PROBE_INTERVAL_MS) {
            send_probe_on_channel(ch);
            last_probe_ms = millis();
        }

        espnow_queue_msg_t msg;
        if (EspnowQueueManager::instance().receive_from_discovery_queue(
                msg, kDiscoveryLoopPollMs)) {
            if (msg.len >= 1) {
                const uint8_t msg_type = msg.data[0];

                // Extend dwell slightly if any discovery traffic is seen —
                // the ACK may arrive fractionally after the probe echo.
                if (msg_type == msg_probe &&
                    effective_dwell < (dwell_ms + kSignalExtensionMs)) {
                    effective_dwell = dwell_ms + kSignalExtensionMs;
                    LOG_DEBUG("DISCOVERY",
                              "[%s] Traffic on ch=%d — dwell extended to %lu ms",
                              phase_label, ch,
                              static_cast<unsigned long>(effective_dwell));
                }

                if (msg_type == msg_ack &&
                    msg.len >= static_cast<int>(sizeof(ack_t))) {
                    metrics_.ack_frames_received++;
                    const ack_t* a = reinterpret_cast<const ack_t*>(msg.data);
                    LOG_INFO("DISCOVERY",
                             "[%s] ✓ ACK from %02X:%02X:%02X:%02X:%02X:%02X "
                             "ch=%d seq=%u",
                             phase_label,
                             msg.mac[0], msg.mac[1], msg.mac[2],
                             msg.mac[3], msg.mac[4], msg.mac[5],
                             a->channel, a->seq);
                    if (out_mac) memcpy(out_mac, msg.mac, 6);
                    // Return the receiver's reported home channel (WiFi.channel() on
                    // the receiver).  This is the channel we must use for all
                    // subsequent sends — it may differ from the scan channel if the
                    // receiver was briefly hopping when it received our probe.
                    if (out_ack_channel) *out_ack_channel = a->channel;
                    return true;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(kDiscoveryLoopPollMs));
    }

    LOG_DEBUG("DISCOVERY", "[%s] ch=%d: no ACK", phase_label, ch);
    return false;
}

// ============================================================================
// send_probe_on_channel
// ============================================================================
void DiscoveryTask::send_probe_on_channel(uint8_t channel) {
    metrics_.probe_send_attempts++;

    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Refresh broadcast peer to current channel (channel=0 → use WiFi channel)
    if (esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_del_peer(broadcast_mac);
    }
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, broadcast_mac, 6);
    peer.channel = 0;  // use current WiFi channel
    peer.encrypt = false;
    peer.ifidx   = WIFI_IF_STA;
    const esp_err_t add_rc = esp_now_add_peer(&peer);
    if (add_rc != ESP_OK && add_rc != ESP_ERR_ESPNOW_EXIST) {
        LOG_ERROR("DISCOVERY",
                  "Failed to add broadcast peer on ch=%d: %s",
                  channel, esp_err_to_name(add_rc));
        return;
    }

    probe_t probe{};
    probe.type = msg_probe;
    probe.seq  = millis();
    const esp_err_t rc = EspnowTxScheduler::send(
        broadcast_mac,
        &probe,
        sizeof(probe),
        "DISCOVERY_PROBE_HOP");
    if (rc == ESP_OK) {
        metrics_.probe_send_success++;
        return;
    }

    if (rc == ESP_ERR_ESPNOW_NO_MEM) {
        metrics_.probe_send_no_mem_fail++;
    } else {
        metrics_.probe_send_other_fail++;
    }

    if (rc != ESP_OK) {
        LOG_DEBUG("DISCOVERY", "PROBE send on ch=%d: %s", channel, esp_err_to_name(rc));
    }
}

// ============================================================================
// force_and_verify_channel
// ============================================================================
bool DiscoveryTask::force_and_verify_channel(uint8_t target_channel) {
    if (!set_channel(target_channel)) {
        LOG_ERROR("DISCOVERY", "set_channel(%d) failed", target_channel);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::CHANNEL_STABILIZATION_MS));

    uint8_t actual = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&actual, &second);
    if (actual != target_channel) {
        LOG_ERROR("DISCOVERY",
                  "Channel verify failed: requested=%d actual=%d",
                  target_channel, actual);
        metrics_.channel_mismatches++;
        return false;
    }
    return true;
}

// ============================================================================
// validate_state — steady-state health check (call only when CONNECTED)
// ============================================================================
bool DiscoveryTask::validate_state() const {
    bool valid = true;

    // Check WiFi channel matches locked channel
    uint8_t current_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_ch, &second);
    const uint8_t locked_ch = esp32common::espnow::ChannelAuthority::instance().operating_channel();
    if (current_ch != locked_ch) {
        LOG_ERROR("DISCOVERY",
                  "Channel mismatch: WiFi=%d locked=%d",
                  current_ch, locked_ch);
        valid = false;
    }

    // Check broadcast peer exists with correct channel
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_peer_info_t peer{};
        if (esp_now_get_peer(broadcast_mac, &peer) == ESP_OK) {
            if (peer.channel != locked_ch && peer.channel != 0) {
                LOG_ERROR("DISCOVERY",
                          "Broadcast peer channel mismatch: peer=%d locked=%d",
                          peer.channel, locked_ch);
                valid = false;
            }
        }
    } else {
        LOG_WARN("DISCOVERY", "Broadcast peer not present during validation");
        valid = false;
    }

    return valid;
}

// ============================================================================
// audit_peer_state — diagnostic dump
// ============================================================================
void DiscoveryTask::audit_peer_state() const {
    uint8_t current_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_ch, &second);

    LOG_INFO("PEER_AUDIT", "═══ ESP-NOW Peer Audit ═══");
    const uint8_t locked_ch = esp32common::espnow::ChannelAuthority::instance().operating_channel();
    LOG_INFO("PEER_AUDIT", "WiFi ch=%d locked=%d", current_ch, locked_ch);

    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_peer_info_t p{};
        if (esp_now_get_peer(broadcast_mac, &p) == ESP_OK) {
            LOG_INFO("PEER_AUDIT",
                     "Broadcast: ch=%d encrypt=%d if=%d %s",
                     p.channel, p.encrypt, p.ifidx,
                     (p.channel != 0 && p.channel != locked_ch) ? "✗ CHAN MISMATCH" : "✓");
        }
    } else {
        LOG_WARN("PEER_AUDIT", "Broadcast peer: NOT PRESENT");
    }
    LOG_INFO("PEER_AUDIT", "═══════════════════════");
}

// ============================================================================
// DiscoveryMetrics::log_summary
// ============================================================================
void DiscoveryMetrics::log_summary() const {
    LOG_INFO("DISCOVERY", "═══ Discovery Metrics ═══");
    LOG_INFO("DISCOVERY", "Total scans    : %lu", static_cast<unsigned long>(total_scans));
    LOG_INFO("DISCOVERY", "  Successful   : %lu", static_cast<unsigned long>(successful_scans));
    LOG_INFO("DISCOVERY", "  Failed       : %lu", static_cast<unsigned long>(failed_scans));
    LOG_INFO("DISCOVERY", "Ch mismatches  : %lu", static_cast<unsigned long>(channel_mismatches));
    LOG_INFO("DISCOVERY", "Probe sends    : attempts=%lu ok=%lu no_mem=%lu other_fail=%lu",
             static_cast<unsigned long>(probe_send_attempts),
             static_cast<unsigned long>(probe_send_success),
             static_cast<unsigned long>(probe_send_no_mem_fail),
             static_cast<unsigned long>(probe_send_other_fail));
    LOG_INFO("DISCOVERY", "ACK frames rx  : %lu", static_cast<unsigned long>(ack_frames_received));
    LOG_INFO("DISCOVERY", "Last success   : ch=%lu at t=%lu ms",
             static_cast<unsigned long>(last_success_channel),
             static_cast<unsigned long>(last_success_timestamp));
    LOG_INFO("DISCOVERY", "Longest scan   : %lu ms", static_cast<unsigned long>(longest_scan_ms));
    if (total_scans > 0) {
        const float rate = 100.0f *
            static_cast<float>(successful_scans) / static_cast<float>(total_scans);
        LOG_INFO("DISCOVERY", "Success rate   : %.1f%%", rate);
    }
    if (probe_send_attempts > 0) {
        const float no_mem_rate = 100.0f *
            static_cast<float>(probe_send_no_mem_fail) / static_cast<float>(probe_send_attempts);
        LOG_INFO("DISCOVERY", "Probe NO_MEM   : %.1f%%", no_mem_rate);
    }
    if (probe_send_success > 0) {
        const float ack_per_probe = 100.0f *
            static_cast<float>(ack_frames_received) / static_cast<float>(probe_send_success);
        LOG_INFO("DISCOVERY", "ACK/probe(ok)  : %.1f%%", ack_per_probe);
    }
    LOG_INFO("DISCOVERY", "════════════════════════");
}
