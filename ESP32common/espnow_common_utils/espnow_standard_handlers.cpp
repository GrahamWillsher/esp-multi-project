/**
 * @file espnow_standard_handlers.cpp
 * @brief Implementation of standard message handlers
 */

#include "espnow_standard_handlers.h"
#include "espnow_peer_manager.h"
#include "espnow_packet_utils.h"
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <logging_config.h>
#include <esp32common/espnow/tx_scheduler.h>

namespace EspnowStandardHandlers {

namespace {
AckSendStats g_ack_send_stats{};
portMUX_TYPE g_ack_send_stats_mux = portMUX_INITIALIZER_UNLOCKED;
}

void handle_probe(const espnow_queue_msg_t* msg, void* context) {
    if (!msg || msg->len < (int)sizeof(probe_t)) return;
    
    const probe_t* p = reinterpret_cast<const probe_t*>(msg->data);
    ProbeHandlerConfig* config = static_cast<ProbeHandlerConfig*>(context);
    
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             msg->mac[0], msg->mac[1], msg->mac[2], msg->mac[3], msg->mac[4], msg->mac[5]);
    
    LOG_DEBUG("PROBE", "Received announcement (seq=%u) from %s", p->seq, mac_str);

    // Always register (or re-register) the sender with channel=0.
    //
    // WHY channel=0 is mandatory here:
    //   channel=0 tells ESP-NOW to transmit on whatever WiFi channel the radio
    //   is currently on, rather than on the peer's stored channel field.  Using
    //   a specific non-zero channel causes esp_now_send() to silently queue the
    //   frame for a channel the radio may not be on (e.g. receiver STA is locked
    //   to the AP's channel 11 but the stale peer entry says channel 6).  The
    //   frame then sits in the ESP-NOW TX buffer with no path to transmit, the
    //   send callback never fires, the buffer slot is permanently consumed, and
    //   after a few probes all slots are exhausted -> every subsequent
    //   esp_now_send() returns ESP_ERR_ESPNOW_NO_MEM indefinitely.
    //
    // WHY we re-register on every probe:
    //   A stale non-zero channel registration persists across reconnect cycles
    //   because add_peer() is a no-op when the peer already exists.  Explicitly
    //   removing and re-adding forces a clean channel=0 entry each time.
    if (EspnowPeerManager::is_peer_registered(msg->mac)) {
        EspnowPeerManager::remove_peer(msg->mac);
    }
    if (!EspnowPeerManager::add_peer(msg->mac, 0)) {
        LOG_WARN("PROBE", "Failed to register peer %s (channel=0)", mac_str);
    } else {
        LOG_DEBUG("PROBE", "Registered peer %s (channel=0)", mac_str);
    }
    
    // Connection callback semantics are edge-triggered only.
    // If no connection_flag is provided, we cannot reliably detect a new
    // connection transition and therefore must not synthesize one.
    bool fire_connection_callback = false;
    if (config && config->connection_flag) {
        const bool was_connected = *config->connection_flag;
        if (!was_connected) {
            *config->connection_flag = true;
            LOG_INFO("PROBE", "Peer %s connected!", mac_str);
            fire_connection_callback = true;
        }
    }
    
    // Store peer MAC if provided
    if (config && config->peer_mac_storage) {
        memcpy(config->peer_mac_storage, msg->mac, 6);
    }
    
    // Call probe received callback (fires every time, regardless of connection state)
    if (config && config->on_probe_received) {
        config->on_probe_received(msg->mac, p->seq);
    }

    // Send ACK response if configured. This runs after on_probe_received so
    // receiver quiet mode can disable competing senders before the ACK path.
    if (config && config->send_ack_response) {
        send_ack_response(msg->mac, p->seq, WiFi.channel());
    }
    
    // Call connection callback only on explicit false->true transition.
    if (config && config->on_connection && fire_connection_callback) {
        config->on_connection(msg->mac, true);
    }
}

void handle_ack(const espnow_queue_msg_t* msg, void* context) {
    if (!msg || msg->len < (int)sizeof(ack_t)) return;
    
    const ack_t* a = reinterpret_cast<const ack_t*>(msg->data);
    AckHandlerConfig* config = static_cast<AckHandlerConfig*>(context);
    
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             msg->mac[0], msg->mac[1], msg->mac[2], msg->mac[3], msg->mac[4], msg->mac[5]);
    
    LOG_DEBUG("ACK", "Received (seq=%u, channel=%d) from %s", 
                  a->seq, a->channel, mac_str);
    
    // Validate sequence if provided (only if it's been set to a non-zero value during PROBE)
    if (config && config->expected_seq && *config->expected_seq != 0) {
        if (a->seq != *config->expected_seq) {
            LOG_WARN("ACK", "Sequence mismatch (expected=%u, got=%u)",
                           *config->expected_seq, a->seq);
            return;
        }
        LOG_DEBUG("ACK", "Sequence validated!");
    }
    
    // Update channel lock if provided
    if (config && config->lock_channel) {
        *config->lock_channel = a->channel;
        LOG_DEBUG("ACK", "Channel locked to %d", a->channel);
        
        // Actually set the WiFi channel if configured
        if (config->set_wifi_channel) {
            LOG_DEBUG("ACK", "Attempting to set WiFi channel to %d...", a->channel);
            esp_err_t result = esp_wifi_set_channel(a->channel, WIFI_SECOND_CHAN_NONE);
            if (result == ESP_OK) {
                LOG_INFO("ACK", "WiFi channel successfully set to %d", a->channel);
            } else {
                LOG_ERROR("ACK", "Failed to set WiFi channel: %s", esp_err_to_name(result));
            }
        } else {
            LOG_DEBUG("ACK", "set_wifi_channel is false, not changing channel");
        }
    } else {
        LOG_DEBUG("ACK", "No lock_channel configured");
    }
    
    // Set ACK received flag if provided (for discovery hopping)
    if (config && config->ack_received_flag) {
        *config->ack_received_flag = true;
        LOG_DEBUG("ACK", "ACK received flag set");
    }
    
    // Connection callback semantics are edge-triggered only.
    // If no connection_flag is provided, we cannot reliably detect a new
    // connection transition and therefore must not synthesize one.
    bool fire_connection_callback = false;
    if (config && config->connection_flag) {
        const bool was_connected = *config->connection_flag;
        if (!was_connected) {
            *config->connection_flag = true;
            LOG_INFO("ACK", "Peer %s connected!", mac_str);
            fire_connection_callback = true;
        }
    }
    
    // Store peer MAC if provided
    if (config && config->peer_mac_storage) {
        memcpy(config->peer_mac_storage, msg->mac, 6);
    }
    
    // Call connection callback only on explicit false->true transition.
    if (config && config->on_connection && fire_connection_callback) {
        config->on_connection(msg->mac, true);
    }
}

void handle_data(const espnow_queue_msg_t* msg, void* context) {
    if (!msg || msg->len < (int)sizeof(espnow_payload_t)) return;
    
    const espnow_payload_t* payload = reinterpret_cast<const espnow_payload_t*>(msg->data);
    
    if (!EspnowPacketUtils::verify_message_crc32(payload)) {
        LOG_WARN("DATA", "CRC32 mismatch (stored=0x%08lX)",
                 static_cast<unsigned long>(payload->checksum));
        return;
    }
    
    // Call user callback if provided
    using DataCallback = std::function<void(const espnow_payload_t*)>;
    if (context) {
        DataCallback* callback = static_cast<DataCallback*>(context);
        (*callback)(payload);
    }
}

bool send_ack_response(const uint8_t* peer_mac, uint32_t seq, uint8_t channel) {
    ack_t ack { msg_ack, seq, channel };

    uint32_t token_held_ms = 0;
    if (!EspnowTxScheduler::try_acquire_ack_token(peer_mac, &token_held_ms)) {
        LOG_DEBUG("ACK", "Suppressed duplicate discovery ACK (seq=%u, held=%lu ms)",
                  seq,
                  static_cast<unsigned long>(token_held_ms));
        return false;
    }

    // Route discovery ACKs through the shared scheduler so one sender owns
    // esp_now_send() during reconnect and control traffic can preempt stale work.
    portENTER_CRITICAL(&g_ack_send_stats_mux);
    g_ack_send_stats.direct_attempts++;
    portEXIT_CRITICAL(&g_ack_send_stats_mux);

    const esp_err_t queued_result = EspnowTxScheduler::send(peer_mac,
                                                            &ack,
                                                            sizeof(ack),
                                                            "DISCOVERY_ACK");
    if (queued_result == ESP_OK) {
        portENTER_CRITICAL(&g_ack_send_stats_mux);
        g_ack_send_stats.direct_success++;
        portEXIT_CRITICAL(&g_ack_send_stats_mux);
        LOG_DEBUG("ACK", "Queued response (seq=%u, channel=%d)", seq, channel);
        return true;
    }

    EspnowTxScheduler::release_ack_token(peer_mac, "enqueue_failed");

    portENTER_CRITICAL(&g_ack_send_stats_mux);
    if (queued_result == ESP_ERR_ESPNOW_NO_MEM) {
        g_ack_send_stats.direct_no_mem_failures++;
    } else {
        g_ack_send_stats.direct_other_failures++;
    }
    portEXIT_CRITICAL(&g_ack_send_stats_mux);

    LOG_WARN("ACK", "Scheduler enqueue failed (seq=%u channel=%d): %s",
             seq, channel, esp_err_to_name(queued_result));
    return false;
}

bool read_ack_send_stats(AckSendStats& out_stats) {
    portENTER_CRITICAL(&g_ack_send_stats_mux);
    out_stats = g_ack_send_stats;
    portEXIT_CRITICAL(&g_ack_send_stats_mux);
    return true;
}

void reset_ack_send_stats() {
    portENTER_CRITICAL(&g_ack_send_stats_mux);
    g_ack_send_stats = {};
    portEXIT_CRITICAL(&g_ack_send_stats_mux);
}

} // namespace EspnowStandardHandlers
