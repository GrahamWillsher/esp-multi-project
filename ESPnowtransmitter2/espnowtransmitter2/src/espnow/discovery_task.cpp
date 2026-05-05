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
#include "../queue/espnow_queue_manager.h"
#include <Arduino.h>
#include <espnow_transmitter.h>   // set_channel(), g_lock_channel
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
    metrics_.total_scans++;

    const bool found = active_channel_hop_scan_impl(
        start_channel_hint, out_channel, out_mac);

    const uint32_t duration_ms = millis() - scan_start_ms;
    if (duration_ms > metrics_.longest_scan_ms) {
        metrics_.longest_scan_ms = duration_ms;
    }

    if (found) {
        metrics_.successful_scans++;
        metrics_.last_success_channel   = *out_channel;
        metrics_.last_success_timestamp = millis();
        LOG_INFO("DISCOVERY",
                 "Scan succeeded: ch=%d, duration=%lu ms (total=%lu/%lu success/fail)",
                 *out_channel,
                 static_cast<unsigned long>(duration_ms),
                 static_cast<unsigned long>(metrics_.successful_scans),
                 static_cast<unsigned long>(metrics_.failed_scans));
    } else {
        metrics_.failed_scans++;
        LOG_WARN("DISCOVERY",
                 "Scan failed: duration=%lu ms (total=%lu/%lu success/fail)",
                 static_cast<unsigned long>(duration_ms),
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
        if (scan_channel_for_ack(ch, BASE_DWELL_MS, "phase1-fast", ack_mac)) {
            ack_received = true;
            ack_channel  = ch;
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

            if (scan_channel_for_ack(candidates[idx], dwell, "phase2-weighted", ack_mac)) {
                ack_received = true;
                ack_channel  = candidates[idx];
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
                                          uint8_t*    out_mac) {
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
                    const ack_t* a = reinterpret_cast<const ack_t*>(msg.data);
                    LOG_INFO("DISCOVERY",
                             "[%s] ✓ ACK from %02X:%02X:%02X:%02X:%02X:%02X "
                             "ch=%d seq=%u",
                             phase_label,
                             msg.mac[0], msg.mac[1], msg.mac[2],
                             msg.mac[3], msg.mac[4], msg.mac[5],
                             a->channel, a->seq);
                    if (out_mac) memcpy(out_mac, msg.mac, 6);
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
    const esp_err_t rc = esp_now_send(broadcast_mac,
                                      reinterpret_cast<const uint8_t*>(&probe),
                                      sizeof(probe));
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
    if (current_ch != g_lock_channel) {
        LOG_ERROR("DISCOVERY",
                  "Channel mismatch: WiFi=%d locked=%d",
                  current_ch, g_lock_channel);
        valid = false;
    }

    // Check broadcast peer exists with correct channel
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_peer_info_t peer{};
        if (esp_now_get_peer(broadcast_mac, &peer) == ESP_OK) {
            if (peer.channel != g_lock_channel && peer.channel != 0) {
                LOG_ERROR("DISCOVERY",
                          "Broadcast peer channel mismatch: peer=%d locked=%d",
                          peer.channel, g_lock_channel);
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
    LOG_INFO("PEER_AUDIT", "WiFi ch=%d locked=%d", current_ch, g_lock_channel);

    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_peer_info_t p{};
        if (esp_now_get_peer(broadcast_mac, &p) == ESP_OK) {
            LOG_INFO("PEER_AUDIT",
                     "Broadcast: ch=%d encrypt=%d if=%d %s",
                     p.channel, p.encrypt, p.ifidx,
                     (p.channel != 0 && p.channel != g_lock_channel) ? "✗ CHAN MISMATCH" : "✓");
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
    LOG_INFO("DISCOVERY", "Last success   : ch=%lu at t=%lu ms",
             static_cast<unsigned long>(last_success_channel),
             static_cast<unsigned long>(last_success_timestamp));
    LOG_INFO("DISCOVERY", "Longest scan   : %lu ms", static_cast<unsigned long>(longest_scan_ms));
    if (total_scans > 0) {
        const float rate = 100.0f *
            static_cast<float>(successful_scans) / static_cast<float>(total_scans);
        LOG_INFO("DISCOVERY", "Success rate   : %.1f%%", rate);
    }
    LOG_INFO("DISCOVERY", "════════════════════════");
}
