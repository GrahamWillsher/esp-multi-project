/**
 * @file espnow_peer_manager.h
 * @brief Common ESP-NOW peer management utilities
 * 
 * Provides centralized peer add/remove/query operations for ESP-NOW
 * communications. Used by both transmitter and receiver projects.
 * 
 * @note Thread-safe for use from multiple FreeRTOS tasks
 */

#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>

namespace EspnowPeerManager {
    
    /**
     * @brief Add a peer to ESP-NOW
     * @param mac MAC address of peer to add
     * @param channel WiFi channel (0 = use current channel)
     * @return true if peer added successfully or already exists
     */
    bool add_peer(const uint8_t* mac, uint8_t channel = 0);
    
    /**
     * @brief Remove a peer from ESP-NOW
     * @param mac MAC address of peer to remove
     * @return true if peer removed successfully or doesn't exist
     */
    bool remove_peer(const uint8_t* mac);
    
    /**
     * @brief Check if a peer is registered
     * @param mac MAC address to check
     * @return true if peer is already registered
     */
    bool is_peer_registered(const uint8_t* mac);
    
    /**
     * @brief Add broadcast peer for discovery
     * @return true if broadcast peer added successfully
     */
    bool add_broadcast_peer();
    
    /**
     * @brief Update peer's WiFi channel
     * @param mac MAC address of peer
     * @param channel New WiFi channel
     * @return true if peer updated successfully
     */
    bool update_peer_channel(const uint8_t* mac, uint8_t channel);
    
    /**
     * @brief Format MAC address for logging
     * @param mac MAC address (6 bytes)
     * @param buffer Output buffer (must be at least 18 bytes)
     */
    void format_mac(const uint8_t* mac, char* buffer);
    
} // namespace EspnowPeerManager
