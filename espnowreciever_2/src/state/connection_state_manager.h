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
#include <cstddef>

/**
 * @brief Manages connection state atomically
 * 
 * Provides thread-safe access to transmitter connection information
 * with optional change notifications.
 */
class ConnectionStateManager {
public:
    using ConnectionChangedCallback = void(*)(bool connected);

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
     * @brief Register callback invoked when connection state changes
     * @param callback Function pointer to invoke
     * @return true if registered, false if callback list full/invalid
     */
    static bool register_connection_changed_callback(ConnectionChangedCallback callback);

    /**
     * @brief Poll legacy global state and emit callback/stat updates if changed
     * @return true if state changed
     */
    static bool poll_connection_change();
    
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
     * @brief Get latest received SOC
     */
    static uint8_t get_soc();

    /**
     * @brief Set SOC value
     * @return true if changed
     */
    static bool set_soc(uint8_t soc);

    /**
     * @brief Get latest received power (W)
     */
    static int32_t get_power();

    /**
     * @brief Set power value (W)
     * @return true if changed
     */
    static bool set_power(int32_t power);

    /**
     * @brief Get latest estimated voltage (mV)
     */
    static uint32_t get_voltage_mv();

    /**
     * @brief Set voltage value (mV)
     * @return true if changed
     */
    static bool set_voltage_mv(uint32_t voltage_mv);

    /**
     * @brief Check if any data has been received
     */
    static bool has_data_received();

    /**
     * @brief Set data-received flag
     * @return true if changed
     */
    static bool set_data_received(bool received);

    /**
     * @brief Update all receiver data atomically
     * @param soc SOC percent
     * @param power Power in watts
     * @param voltage_mv Voltage in millivolts
     * @param out_soc_changed Optional out flag for SOC change
     * @param out_power_changed Optional out flag for power change
     */
    static void update_received_data(
        uint8_t soc,
        int32_t power,
        uint32_t voltage_mv,
        bool* out_soc_changed = nullptr,
        bool* out_power_changed = nullptr
    );

    /**
     * @brief Timestamp when data was last received
     */
    static uint32_t last_data_received_ms();

    /**
     * @brief Check if received data is stale
     */
    static bool is_data_stale(uint32_t timeout_ms);

    /**
     * @brief Current connection uptime (0 when disconnected)
     */
    static uint32_t get_connection_uptime_ms();

    /**
     * @brief Number of connection transitions to connected state
     */
    static uint32_t get_connection_count();
    
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
    
    static SemaphoreHandle_t state_mutex_;

    static bool tracked_connection_state_;
    static uint32_t connection_start_time_ms_;
    static uint32_t connection_count_;
    static uint32_t last_data_received_ms_;

    static constexpr size_t MAX_CALLBACKS = 8;
    static ConnectionChangedCallback callbacks_[MAX_CALLBACKS];
    static size_t callback_count_;

    static void notify_connection_changed(bool connected);
};
