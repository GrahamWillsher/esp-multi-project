#pragma once

/**
 * @file received_data_manager.h
 * @brief Thread-safe management of received sensor data from transmitter
 * 
 * Encapsulates volatile received data (SOC, power, voltage) and provides
 * atomic updates with change detection for display optimization.
 * Replaces direct access to volatile globals.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>

/**
 * @brief Manages received sensor data atomically
 * 
 * Provides thread-safe access to transmitter sensor readings
 * with change detection and dirty flag support.
 */
class ReceivedDataManager {
public:
    /**
     * @brief Initialize the received data manager
     */
    static void init();
    
    /**
     * @brief Get current SOC (State of Charge)
     * @return SOC percentage (0-100)
     */
    static uint8_t get_soc();
    
    /**
     * @brief Set SOC value
     * @param soc New SOC percentage
     * @return true if value changed
     */
    static bool set_soc(uint8_t soc);
    
    /**
     * @brief Get current power
     * @return Power in watts (negative=charging, positive=discharging)
     */
    static int32_t get_power();
    
    /**
     * @brief Set power value
     * @param power_w New power in watts
     * @return true if value changed
     */
    static bool set_power(int32_t power_w);
    
    /**
     * @brief Get current voltage
     * @return Voltage in millivolts
     */
    static uint32_t get_voltage_mv();
    
    /**
     * @brief Set voltage value
     * @param voltage_mv New voltage in millivolts
     * @return true if value changed
     */
    static bool set_voltage_mv(uint32_t voltage_mv);
    
    /**
     * @brief Check if data was received flag
     * @return true if new data has been received
     */
    static bool is_data_received();
    
    /**
     * @brief Set data received flag
     * @param received New flag state
     * @return true if flag changed
     */
    static bool set_data_received(bool received);
    
    /**
     * @brief Clear data received flag (call after processing)
     */
    static void clear_data_received();
    
    /**
     * @brief Get SOC dirty flag (for display optimization)
     * @return true if SOC has changed since last check
     */
    static bool soc_changed();
    
    /**
     * @brief Clear SOC dirty flag
     */
    static void clear_soc_changed();
    
    /**
     * @brief Get power dirty flag (for display optimization)
     * @return true if power has changed since last check
     */
    static bool power_changed();
    
    /**
     * @brief Clear power dirty flag
     */
    static void clear_power_changed();
    
    /**
     * @brief Get all received data atomically
     * @param out_soc SOC percentage
     * @param out_power Power in watts
     * @param out_voltage_mv Voltage in millivolts
     * @param out_data_received Data received flag
     */
    static void get_all_data(uint8_t& out_soc, int32_t& out_power, 
                             uint32_t& out_voltage_mv, bool& out_data_received);
    
    /**
     * @brief Lock data for read/write operations
     * @param timeout_ms Timeout in milliseconds
     * @return true if lock acquired
     */
    static bool lock(uint32_t timeout_ms = portMAX_DELAY);
    
    /**
     * @brief Unlock data
     */
    static void unlock();
    
private:
    ReceivedDataManager() = delete;  // Static class
    
    static uint8_t soc_;
    static int32_t power_w_;
    static uint32_t voltage_mv_;
    static bool data_received_;
    static bool soc_changed_;
    static bool power_changed_;
    static SemaphoreHandle_t data_mutex_;
};
