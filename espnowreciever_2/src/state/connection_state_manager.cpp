/**
 * @file connection_state_manager.cpp
 * @brief Implementation of thread-safe connection state management
 */

#include "connection_state_manager.h"
#include <logging.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Static member initialization
bool ConnectionStateManager::transmitter_connected_ = false;
int ConnectionStateManager::wifi_channel_ = 0;
uint8_t ConnectionStateManager::transmitter_mac_[6] = {0};
SemaphoreHandle_t ConnectionStateManager::state_mutex_ = nullptr;

void ConnectionStateManager::init() {
    if (state_mutex_) {
        LOG_WARN("CONN_STATE", "ConnectionStateManager already initialized");
        return;
    }
    
    state_mutex_ = xSemaphoreCreateMutex();
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "Failed to create connection state mutex");
        return;
    }
    
    LOG_INFO("CONN_STATE", "ConnectionStateManager initialized");
}

bool ConnectionStateManager::is_transmitter_connected() {
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return false;
    }
    
    bool result = transmitter_connected_;
    xSemaphoreGive(state_mutex_);
    return result;
}

bool ConnectionStateManager::set_transmitter_connected(bool connected) {
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return false;
    }
    
    bool changed = (transmitter_connected_ != connected);
    if (changed) {
        transmitter_connected_ = connected;
        LOG_INFO("CONN_STATE", "Transmitter connection state changed to: %s", connected ? "CONNECTED" : "DISCONNECTED");
    }
    
    xSemaphoreGive(state_mutex_);
    return changed;
}

int ConnectionStateManager::get_wifi_channel() {
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return 0;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return 0;
    }
    
    int result = wifi_channel_;
    xSemaphoreGive(state_mutex_);
    return result;
}

bool ConnectionStateManager::set_wifi_channel(int channel) {
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return false;
    }
    
    bool changed = (wifi_channel_ != channel);
    if (changed) {
        wifi_channel_ = channel;
        LOG_DEBUG("CONN_STATE", "WiFi channel changed to: %d", channel);
    }
    
    xSemaphoreGive(state_mutex_);
    return changed;
}

bool ConnectionStateManager::get_transmitter_mac(uint8_t* mac_out) {
    if (!mac_out) {
        LOG_ERROR("CONN_STATE", "MAC output buffer is nullptr");
        return false;
    }
    
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return false;
    }
    
    // Check if MAC is set (not all zeros)
    bool is_set = false;
    for (int i = 0; i < 6; i++) {
        if (transmitter_mac_[i] != 0) {
            is_set = true;
            break;
        }
    }
    
    if (is_set) {
        memcpy(mac_out, transmitter_mac_, 6);
    }
    
    xSemaphoreGive(state_mutex_);
    return is_set;
}

bool ConnectionStateManager::set_transmitter_mac(const uint8_t* mac) {
    if (!mac) {
        LOG_ERROR("CONN_STATE", "MAC input is nullptr");
        return false;
    }
    
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return false;
    }
    
    bool changed = (memcmp(transmitter_mac_, mac, 6) != 0);
    if (changed) {
        memcpy(transmitter_mac_, mac, 6);
        LOG_INFO("CONN_STATE", "Transmitter MAC set to: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    
    xSemaphoreGive(state_mutex_);
    return changed;
}

bool ConnectionStateManager::lock(uint32_t timeout_ms) {
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }
    
    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(state_mutex_, ticks) == pdTRUE;
}

void ConnectionStateManager::unlock() {
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return;
    }
    
    xSemaphoreGive(state_mutex_);
}

void ConnectionStateManager::get_all_state(bool& out_connected, int& out_channel, uint8_t* out_mac) {
    if (!out_mac) {
        LOG_ERROR("CONN_STATE", "MAC output buffer is nullptr");
        return;
    }
    
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return;
    }
    
    out_connected = transmitter_connected_;
    out_channel = wifi_channel_;
    memcpy(out_mac, transmitter_mac_, 6);
    
    xSemaphoreGive(state_mutex_);
}
