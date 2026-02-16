/**
 * @file channel_manager.h
 * @brief Centralized WiFi Channel Management for ESP-NOW
 * 
 * Coordinates WiFi channel changes to prevent conflicts between:
 * - Discovery task (channel hopping)
 * - Peer registration (setting to discovered channel)
 * - Connection manager (locking to peer's channel)
 * 
 * Key features:
 * - Mutex-protected channel setting
 * - Channel locking mechanism (prevents changes)
 * - Thread-safe from ISR and task contexts
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>
#include <esp_wifi.h>

class ChannelManager {
public:
    /**
     * @brief Get singleton instance
     */
    static ChannelManager& instance();
    
    /**
     * @brief Initialize the channel manager
     * 
     * Must be called once during setup.
     * 
     * @return true if initialization successful
     */
    bool init();
    
    /**
     * @brief Set WiFi channel (with mutex protection)
     * 
     * Only sets the channel if not locked. If locked, returns false.
     * 
     * @param channel Channel number (1-13)
     * @param source Description of who is requesting the change (for logging)
     * @return true if channel was set
     */
    bool set_channel(uint8_t channel, const char* source = "unknown");
    
    /**
     * @brief Lock channel to prevent changes
     * 
     * Used when connection is established to prevent discovery from
     * changing the channel.
     * 
     * @param channel Channel to lock to
     * @param source Description of who is locking (for logging)
     */
    void lock_channel(uint8_t channel, const char* source = "unknown");
    
    /**
     * @brief Unlock channel to allow changes
     * 
     * Used when connection is lost to allow discovery to resume.
     * 
     * @param source Description of who is unlocking (for logging)
     */
    void unlock_channel(const char* source = "unknown");
    
    /**
     * @brief Get current WiFi channel
     * @return Current channel number (1-13)
     */
    uint8_t get_channel() const;
    
    /**
     * @brief Check if channel is locked
     * @return true if locked
     */
    bool is_locked() const { return channel_locked_; }

private:
    SemaphoreHandle_t channel_mutex_;  // Mutex for thread-safe access
    uint8_t current_channel_;          // Current WiFi channel
    bool channel_locked_;              // Whether channel is locked
    
    // Private constructor (singleton)
    ChannelManager();
    
    // Prevent copying
    ChannelManager(const ChannelManager&) = delete;
    ChannelManager& operator=(const ChannelManager&) = delete;
};
