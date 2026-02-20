/**
 * @file channel_manager.cpp
 * @brief WiFi Channel Management Implementation
 */

#include "channel_manager.h"
#include <Arduino.h>
#include <Preferences.h>

// Singleton instance
static ChannelManager* g_channel_manager = nullptr;

ChannelManager& ChannelManager::instance() {
    if (g_channel_manager == nullptr) {
        g_channel_manager = new ChannelManager();
    }
    return *g_channel_manager;
}

ChannelManager::ChannelManager()
    : channel_mutex_(nullptr),
      current_channel_(0),
      channel_locked_(false),
      saved_channel_(0) {
}

bool ChannelManager::init() {
    Serial.printf("[CHANNEL_MGR] Initializing WiFi Channel Manager\n");
    
    // Create mutex
    channel_mutex_ = xSemaphoreCreateMutex();
    
    if (channel_mutex_ == nullptr) {
        Serial.printf("[CHANNEL_MGR] ERROR: Failed to create mutex!\n");
        return false;
    }
    
    // Load saved channel from NVS
    saved_channel_ = load_channel_from_nvs();
    
    // Get current WiFi channel
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_channel_, &second);
    
    // If we have a saved channel, try to start on it
    if (saved_channel_ > 0 && saved_channel_ <= 13) {
        Serial.printf("[CHANNEL_MGR] Found saved channel: %d, setting as starting channel\n", saved_channel_);
        esp_wifi_set_channel(saved_channel_, WIFI_SECOND_CHAN_NONE);
        current_channel_ = saved_channel_;
    }
    
    Serial.printf("[CHANNEL_MGR] ✓ Channel Manager initialized\n");
    Serial.printf("[CHANNEL_MGR]   Current channel: %d\n", current_channel_);
    Serial.printf("[CHANNEL_MGR]   Saved channel: %d\n", saved_channel_);
    Serial.printf("[CHANNEL_MGR]   Channel locked: %s\n", channel_locked_ ? "YES" : "NO");
    
    return true;
}

bool ChannelManager::set_channel(uint8_t channel, const char* source) {
    if (channel < 1 || channel > 13) {
        Serial.printf("[CHANNEL_MGR] ERROR: Invalid channel %d (must be 1-13)\n", channel);
        return false;
    }
    
    if (xSemaphoreTake(channel_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.printf("[CHANNEL_MGR] ERROR: Failed to acquire mutex\n");
        return false;
    }
    
    bool success = false;
    
    if (channel_locked_) {
        Serial.printf("[CHANNEL_MGR] Channel locked at %d - ignoring set to %d from %s\n",
                     current_channel_, channel, source);
    } else {
        if (channel != current_channel_) {
            Serial.printf("[CHANNEL_MGR] Setting channel: %d → %d (source: %s)\n",
                         current_channel_, channel, source);
            
            esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
            
            if (err == ESP_OK) {
                current_channel_ = channel;
                success = true;
                Serial.printf("[CHANNEL_MGR] ✓ Channel set successfully\n");
            } else {
                Serial.printf("[CHANNEL_MGR] ERROR: esp_wifi_set_channel failed: %d\n", err);
            }
        } else {
            success = true;  // Already on this channel
        }
    }
    
    xSemaphoreGive(channel_mutex_);
    return success;
}

void ChannelManager::lock_channel(uint8_t channel, const char* source) {
    if (xSemaphoreTake(channel_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.printf("[CHANNEL_MGR] ERROR: Failed to acquire mutex\n");
        return;
    }
    
    Serial.printf("[CHANNEL_MGR] Locking channel to %d (source: %s)\n", channel, source);
    
    // Set to the specified channel first
    if (channel != current_channel_) {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        current_channel_ = channel;
    }
    
    channel_locked_ = true;
    saved_channel_ = channel;
    
    // Save to NVS for persistence across reboots
    save_channel_to_nvs(channel);
    
    Serial.printf("[CHANNEL_MGR] ✓ Channel locked at %d (saved to NVS)\n", current_channel_);
    
    xSemaphoreGive(channel_mutex_);
}

void ChannelManager::unlock_channel(const char* source) {
    if (xSemaphoreTake(channel_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.printf("[CHANNEL_MGR] ERROR: Failed to acquire mutex\n");
        return;
    }
    
    Serial.printf("[CHANNEL_MGR] Unlocking channel (source: %s)\n", source);
    channel_locked_ = false;
    Serial.printf("[CHANNEL_MGR] ✓ Channel unlocked (current: %d)\n", current_channel_);
    
    xSemaphoreGive(channel_mutex_);
}

uint8_t ChannelManager::get_channel() const {
    return current_channel_;
}

bool ChannelManager::save_channel_to_nvs(uint8_t channel) {
    Preferences prefs;
    if (!prefs.begin("espnow", false)) {
        Serial.printf("[CHANNEL_MGR] ERROR: Failed to open NVS for writing\n");
        return false;
    }
    
    prefs.putUChar("channel", channel);
    prefs.end();
    
    Serial.printf("[CHANNEL_MGR] Channel %d saved to NVS\n", channel);
    return true;
}

uint8_t ChannelManager::load_channel_from_nvs() {
    Preferences prefs;
    if (!prefs.begin("espnow", true)) {  // Read-only
        Serial.printf("[CHANNEL_MGR] No saved channel found in NVS\n");
        return 0;
    }
    
    uint8_t channel = prefs.getUChar("channel", 0);
    prefs.end();
    
    if (channel > 0 && channel <= 13) {
        Serial.printf("[CHANNEL_MGR] Loaded channel %d from NVS\n", channel);
    } else {
        Serial.printf("[CHANNEL_MGR] Invalid channel %d in NVS, ignoring\n", channel);
        channel = 0;
    }
    
    return channel;
}
