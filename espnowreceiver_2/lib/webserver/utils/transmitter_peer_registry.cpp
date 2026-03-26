#include "transmitter_peer_registry.h"

#include <Arduino.h>
#include <esp_now.h>
#include <string.h>

#include "../logging.h"

namespace TransmitterPeerRegistry {

bool ensure_peer_registered(const uint8_t* mac) {
    if (mac == nullptr) {
        return false;
    }

    if (esp_now_is_peer_exist(mac)) {
        return true;
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    peer.ifidx = WIFI_IF_STA;

    if (esp_now_add_peer(&peer) == ESP_OK) {
        LOG_INFO("TX_PEER", "Added as ESP-NOW peer");
        return true;
    }

    LOG_ERROR("TX_PEER", "Failed to add as ESP-NOW peer");
    return false;
}

} // namespace TransmitterPeerRegistry
