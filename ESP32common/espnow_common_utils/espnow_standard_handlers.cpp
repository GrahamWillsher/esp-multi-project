/**
 * @file espnow_standard_handlers.cpp
 * @brief Implementation of standard message handlers
 */

#include "espnow_standard_handlers.h"
#include "espnow_peer_manager.h"
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <mqtt_logger.h>

namespace EspnowStandardHandlers {

void handle_probe(const espnow_queue_msg_t* msg, void* context) {
    if (!msg || msg->len < (int)sizeof(probe_t)) return;
    
    const probe_t* p = reinterpret_cast<const probe_t*>(msg->data);
    ProbeHandlerConfig* config = static_cast<ProbeHandlerConfig*>(context);
    
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             msg->mac[0], msg->mac[1], msg->mac[2], msg->mac[3], msg->mac[4], msg->mac[5]);
    
    MQTT_LOG_DEBUG("PROBE", "Received announcement (seq=%u) from %s", p->seq, mac_str);
    
    // Add peer if not already registered (use current WiFi channel explicitly)
    if (!EspnowPeerManager::is_peer_registered(msg->mac)) {
        uint8_t current_channel = WiFi.channel();
        EspnowPeerManager::add_peer(msg->mac, current_channel);
        MQTT_LOG_DEBUG("PROBE", "Registered peer %s on channel %d", mac_str, current_channel);
    }
    
    // Update connection flag if provided
    bool was_connected = config && config->connection_flag ? *config->connection_flag : false;
    if (config && config->connection_flag && !*config->connection_flag) {
        *config->connection_flag = true;
        MQTT_LOG_INFO("PROBE", "Peer %s connected!", mac_str);
    }
    
    // Store peer MAC if provided
    if (config && config->peer_mac_storage) {
        memcpy(config->peer_mac_storage, msg->mac, 6);
    }
    
    // Send ACK response if configured
    if (config && config->send_ack_response) {
        send_ack_response(msg->mac, p->seq, WiFi.channel());
    }
    
    // Call probe received callback (fires every time, regardless of connection state)
    if (config && config->on_probe_received) {
        config->on_probe_received(msg->mac, p->seq);
    }
    
    // Call connection callback if provided and this is a new connection
    if (config && config->on_connection && !was_connected) {
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
    
    MQTT_LOG_DEBUG("ACK", "Received (seq=%u, channel=%d) from %s", 
                  a->seq, a->channel, mac_str);
    
    // Validate sequence if provided
    if (config && config->expected_seq) {
        if (*config->expected_seq == 0 || a->seq != *config->expected_seq) {
            MQTT_LOG_WARNING("ACK", "Sequence mismatch (expected=%u, got=%u)",
                           config->expected_seq ? *config->expected_seq : 0, a->seq);
            return;
        }
        MQTT_LOG_DEBUG("ACK", "Sequence validated!");
    }
    
    // Update channel lock if provided
    if (config && config->lock_channel) {
        *config->lock_channel = a->channel;
        MQTT_LOG_DEBUG("ACK", "Channel locked to %d", a->channel);
        
        // Actually set the WiFi channel if configured
        if (config->set_wifi_channel) {
            MQTT_LOG_DEBUG("ACK", "Attempting to set WiFi channel to %d...", a->channel);
            esp_err_t result = esp_wifi_set_channel(a->channel, WIFI_SECOND_CHAN_NONE);
            if (result == ESP_OK) {
                MQTT_LOG_INFO("ACK", "WiFi channel successfully set to %d", a->channel);
            } else {
                MQTT_LOG_ERROR("ACK", "Failed to set WiFi channel: %s", esp_err_to_name(result));
            }
        } else {
            MQTT_LOG_DEBUG("ACK", "set_wifi_channel is false, not changing channel");
        }
    } else {
        MQTT_LOG_DEBUG("ACK", "No lock_channel configured");
    }
    
    // Set ACK received flag if provided (for discovery hopping)
    if (config && config->ack_received_flag) {
        *config->ack_received_flag = true;
        MQTT_LOG_DEBUG("ACK", "ACK received flag set");
    }
    
    // Update connection flag if provided
    bool was_connected = config && config->connection_flag ? *config->connection_flag : false;
    if (config && config->connection_flag && !*config->connection_flag) {
        *config->connection_flag = true;
        MQTT_LOG_INFO("ACK", "Peer %s connected!", mac_str);
    }
    
    // Store peer MAC if provided
    if (config && config->peer_mac_storage) {
        memcpy(config->peer_mac_storage, msg->mac, 6);
    }
    
    // Call connection callback if provided and this is a new connection
    if (config && config->on_connection && !was_connected) {
        config->on_connection(msg->mac, true);
    }
}

void handle_data(const espnow_queue_msg_t* msg, void* context) {
    if (!msg || msg->len < (int)sizeof(espnow_payload_t)) return;
    
    const espnow_payload_t* payload = reinterpret_cast<const espnow_payload_t*>(msg->data);
    
    // Validate checksum
    uint16_t calc_checksum = payload->soc + (uint16_t)payload->power;
    if (calc_checksum != payload->checksum) {
        MQTT_LOG_WARNING("DATA", "Checksum mismatch (calc=%u, recv=%u)",
                       calc_checksum, payload->checksum);
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
    esp_err_t result = esp_now_send(peer_mac, 
                                    reinterpret_cast<const uint8_t*>(&ack), 
                                    sizeof(ack));
    
    if (result == ESP_OK) {
        MQTT_LOG_DEBUG("ACK", "Sent response (seq=%u, channel=%d)", seq, channel);
        return true;
    } else {
        MQTT_LOG_WARNING("ACK", "Send failed: %s", esp_err_to_name(result));
        return false;
    }
}

bool send_probe_announcement(uint32_t seq) {
    probe_t probe { msg_probe, seq };
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    esp_err_t result = esp_now_send(broadcast_mac,
                                    reinterpret_cast<const uint8_t*>(&probe),
                                    sizeof(probe));
    
    if (result == ESP_OK) {
        MQTT_LOG_DEBUG("PROBE", "Sent announcement (seq=%u) on channel %d", 
                     seq, WiFi.channel());
        return true;
    } else {
        MQTT_LOG_WARNING("PROBE", "Send failed: %s", esp_err_to_name(result));
        return false;
    }
}

} // namespace EspnowStandardHandlers
