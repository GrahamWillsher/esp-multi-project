#include "tx_send_guard.h"

#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <espnow_transmitter.h>
#include <connection_manager.h>
#include "../config/logging_config.h"

namespace {
constexpr uint32_t MISMATCH_LOG_THROTTLE_MS = 5000;
constexpr uint32_t SUMMARY_LOG_PERIOD_MS = 60000;
constexpr uint8_t MAX_CONSECUTIVE_FAILURES = 10;
constexpr uint32_t BASE_BACKOFF_MS = 2000;
constexpr uint32_t MAX_BACKOFF_MS = 30000;

bool g_recovery_active = false;
bool g_recovery_triggered = false;
uint32_t g_last_mismatch_log_ms = 0;
uint32_t g_last_summary_log_ms = 0;

uint8_t g_consecutive_failures = 0;
uint32_t g_send_paused_until_ms = 0;

uint32_t g_channel_mismatch_detected = 0;
uint32_t g_channel_mismatch_recovered_quick = 0;
uint32_t g_channel_mismatch_reconnect_triggered = 0;
uint32_t g_espnow_arg_send_failures = 0;

void log_summary_if_due() {
    const uint32_t now = millis();
    if (now - g_last_summary_log_ms < SUMMARY_LOG_PERIOD_MS) {
        return;
    }
    g_last_summary_log_ms = now;

    LOG_INFO("TX_SEND_GUARD",
             "summary mismatch=%lu quick_recover=%lu reconnect_triggered=%lu arg_failures=%lu paused=%s",
             g_channel_mismatch_detected,
             g_channel_mismatch_recovered_quick,
             g_channel_mismatch_reconnect_triggered,
             g_espnow_arg_send_failures,
             (g_send_paused_until_ms > now) ? "YES" : "NO");
}

void trigger_recovery_once(const uint8_t* mac, const char* reason) {
    if (g_recovery_triggered) {
        return;
    }

    g_recovery_active = true;
    g_recovery_triggered = true;
    g_channel_mismatch_reconnect_triggered++;

    LOG_ERROR("TX_SEND_GUARD", "Channel mismatch recovery triggered: %s", reason);

    if (!post_connection_event(EspNowEvent::CONNECTION_LOST, mac)) {
        LOG_WARN("TX_SEND_GUARD", "Failed to post CONNECTION_LOST event");
    }
}

void apply_local_backoff() {
    if (g_consecutive_failures < MAX_CONSECUTIVE_FAILURES) {
        return;
    }

    const uint8_t tier = (g_consecutive_failures / MAX_CONSECUTIVE_FAILURES);
    uint32_t pause_ms = BASE_BACKOFF_MS;
    if (tier > 1) {
        pause_ms <<= (tier - 1);
    }
    if (pause_ms > MAX_BACKOFF_MS) {
        pause_ms = MAX_BACKOFF_MS;
    }

    g_send_paused_until_ms = millis() + pause_ms;
    LOG_WARN("TX_SEND_GUARD", "Send backoff active for %lu ms (consecutive_failures=%u)", pause_ms, g_consecutive_failures);
}

} // namespace

namespace TxSendGuard {

bool is_peer_channel_coherent(const uint8_t* peer_mac, uint8_t* out_home, uint8_t* out_peer) {
    uint8_t home_channel = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&home_channel, &second) != ESP_OK) {
        return true;  // Don't block sends if channel read fails
    }

    uint8_t peer_channel = 0;
    esp_now_peer_info_t peer_info{};
    if (esp_now_get_peer(peer_mac, &peer_info) == ESP_OK) {
        peer_channel = peer_info.channel;
    }

    if (out_home) *out_home = home_channel;
    if (out_peer) *out_peer = peer_channel;

    const bool peer_ok = (peer_channel == 0) || (peer_channel == home_channel);
    const bool lock_ok = (g_lock_channel == 0) || (home_channel == g_lock_channel);
    return peer_ok && lock_ok;
}

esp_err_t send_to_receiver_guarded(const uint8_t* mac, const uint8_t* data, size_t len, const char* tag) {
    log_summary_if_due();

    if (!mac || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t now = millis();
    if (g_send_paused_until_ms > now) {
        return ESP_ERR_TIMEOUT;
    }

    if (g_send_paused_until_ms != 0 && now >= g_send_paused_until_ms) {
        g_send_paused_until_ms = 0;
        g_consecutive_failures = 0;
        LOG_INFO("TX_SEND_GUARD", "Send backoff window elapsed - resuming sends");
    }

    uint8_t home = 0;
    uint8_t peer = 0;
    const bool coherent = is_peer_channel_coherent(mac, &home, &peer);

    if (!coherent) {
        g_channel_mismatch_detected++;

        if (now - g_last_mismatch_log_ms >= MISMATCH_LOG_THROTTLE_MS) {
            g_last_mismatch_log_ms = now;
            LOG_ERROR("TX_SEND_GUARD", "%s blocked: channel mismatch (home=%u peer=%u lock=%u)",
                      tag ? tag : "send", home, peer, g_lock_channel);
        }

        trigger_recovery_once(mac, "preflight mismatch");
        return ESP_ERR_ESPNOW_ARG;
    }

    // Recovery ends only when we are connected again and channel coherent.
    if (g_recovery_active && EspNowConnectionManager::instance().is_connected()) {
        g_recovery_active = false;
        g_recovery_triggered = false;
        g_channel_mismatch_recovered_quick++;
        g_consecutive_failures = 0;
        g_send_paused_until_ms = 0;
        LOG_INFO("TX_SEND_GUARD", "Recovery cleared - channel coherence restored");
    }

    if (g_recovery_active) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = esp_now_send(mac, data, len);
    if (result == ESP_OK) {
        g_consecutive_failures = 0;
        return ESP_OK;
    }

    g_consecutive_failures++;

    if (result == ESP_ERR_ESPNOW_ARG) {
        g_espnow_arg_send_failures++;
        trigger_recovery_once(mac, "esp_now_send returned ESP_ERR_ESPNOW_ARG");
    }

    if (g_consecutive_failures == 1 || (g_consecutive_failures % 5) == 0) {
        LOG_WARN("TX_SEND_GUARD", "%s send failed: %s (consecutive=%u)",
                 tag ? tag : "send", esp_err_to_name(result), g_consecutive_failures);
    }

    apply_local_backoff();
    return result;
}

void notify_connection_state(bool connected) {
    if (connected) {
        if (g_recovery_active || g_recovery_triggered) {
            g_channel_mismatch_recovered_quick++;
        }
        g_recovery_active = false;
        g_recovery_triggered = false;
        g_consecutive_failures = 0;
        g_send_paused_until_ms = 0;
        return;
    }

    // Disconnected state keeps recovery guard active until connection is restored.
    g_recovery_active = true;
}

} // namespace TxSendGuard
