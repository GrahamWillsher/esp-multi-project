/**
 * @file espnow_peer_manager.cpp
 * @brief Implementation of ESP-NOW peer management utilities
 */

#include "espnow_peer_manager.h"

namespace EspnowPeerManager {

bool add_peer(const uint8_t* mac, uint8_t channel) {
    if (!mac) return false;
    
    // Check if already registered
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    
    // Create peer info structure
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = channel;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    
    // Add peer
    esp_err_t result = esp_now_add_peer(&peerInfo);
    if (result == ESP_OK) {
        char mac_str[18];
        format_mac(mac, mac_str);
        Serial.printf("[PEER_MGR] Added peer: %s (channel %d)\n", mac_str, channel);
        return true;
    } else {
        char mac_str[18];
        format_mac(mac, mac_str);
        Serial.printf("[PEER_MGR] Failed to add peer %s: %s\n", mac_str, esp_err_to_name(result));
        return false;
    }
}

bool remove_peer(const uint8_t* mac) {
    if (!mac) return false;
    
    // Check if peer exists
    if (!esp_now_is_peer_exist(mac)) {
        return true;  // Already removed
    }
    
    // Remove peer
    esp_err_t result = esp_now_del_peer(mac);
    if (result == ESP_OK) {
        char mac_str[18];
        format_mac(mac, mac_str);
        Serial.printf("[PEER_MGR] Removed peer: %s\n", mac_str);
        return true;
    } else {
        char mac_str[18];
        format_mac(mac, mac_str);
        Serial.printf("[PEER_MGR] Failed to remove peer %s: %s\n", mac_str, esp_err_to_name(result));
        return false;
    }
}

bool is_peer_registered(const uint8_t* mac) {
    if (!mac) return false;
    return esp_now_is_peer_exist(mac);
}

bool add_broadcast_peer() {
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    // Check if already registered
    if (esp_now_is_peer_exist(broadcast_mac)) {
        return true;
    }
    
    // Create broadcast peer
    esp_now_peer_info_t broadcast_peer = {};
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    broadcast_peer.channel = 0;  // Use current channel
    broadcast_peer.encrypt = false;
    broadcast_peer.ifidx = WIFI_IF_STA;
    
    esp_err_t result = esp_now_add_peer(&broadcast_peer);
    if (result == ESP_OK) {
        Serial.println("[PEER_MGR] Broadcast peer added");
        return true;
    } else {
        Serial.printf("[PEER_MGR] Failed to add broadcast peer: %s\n", esp_err_to_name(result));
        return false;
    }
}

bool update_peer_channel(const uint8_t* mac, uint8_t channel) {
    if (!mac) return false;
    
    // Remove existing peer
    if (esp_now_is_peer_exist(mac)) {
        esp_now_del_peer(mac);
    }
    
    // Re-add with new channel
    return add_peer(mac, channel);
}

void format_mac(const uint8_t* mac, char* buffer) {
    if (!mac || !buffer) return;
    snprintf(buffer, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

} // namespace EspnowPeerManager
