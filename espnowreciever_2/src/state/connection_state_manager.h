#pragma once

/**
 * @file connection_state_manager.h
 * @brief Thread-safe management of receiver connection state
 * 
 * Encapsulates volatile connection-related state and provides
 * atomic updates with notification callbacks for state changes.
 * Replaces direct access to global volatile variables.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>

/**
 * @brief Manages connection state atomically
 * 
 * Provides thread-safe access to transmitter connection information
 * with optional change notifications.
 */
class ConnectionStateManager {
public:
    /**
     * @brief Initialize the connection state manager
     */
    static void init();
    
    /**
     * @brief Check if transmitter is connected
     * @return true if connected
     */
    static bool is_transmitter_connected();
    
    /**
     * @brief Set transmitter connection state
     * @param connected New connection state
     * @return true if state changed
     */
    static bool set_transmitter_connected(bool connected);
    
    /**
     * @brief Get current WiFi channel
     * @return WiFi channel (1-14)
     */
    static int get_wifi_channel();
    
    /**
     * @brief Set WiFi channel
     * @param channel WiFi channel
     * @return true if changed
     */
    static bool set_wifi_channel(int channel);
    
    /**
     * @brief Get transmitter MAC address
     * @param mac_out Buffer to fill with MAC address (must be 6 bytes)
     * @return true if MAC is set
     */
    static bool get_transmitter_mac(uint8_t* mac_out);
    
    /**
     * @brief Set transmitter MAC address
     * @param mac MAC address (6 bytes)
     * @return true if changed
     */
    static bool set_transmitter_mac(const uint8_t* mac);
    
    /**
     * @brief Lock state for read/write operations
     * @param timeout_ms Timeout in milliseconds
     * @return true if lock acquired
     */
    static bool lock(uint32_t timeout_ms = portMAX_DELAY);
    
    /**
     * @brief Unlock state
     */
    static void unlock();
    
    /**
     * @brief Get all connection state atomically
     * @param out_connected Connected state
     * @param out_channel WiFi channel
     * @param out_mac Transmitter MAC (6 bytes)
     */
    static void get_all_state(bool& out_connected, int& out_channel, uint8_t* out_mac);
    
private:
    ConnectionStateManager() = delete;  // Static class
    
    static bool transmitter_connected_;
    static int wifi_channel_;
    static uint8_t transmitter_mac_[6];
    static SemaphoreHandle_t state_mutex_;
};
