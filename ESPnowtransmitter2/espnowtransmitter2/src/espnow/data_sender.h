#pragma once
#include <freertos/FreeRTOS.h>

/**
 * @brief Manages test data generation and transmission via ESP-NOW
 * 
 * Singleton class that generates simulated battery data and sends it
 * to the receiver when transmission is active.
 */
class DataSender {
public:
    static DataSender& instance();
    
    /**
     * @brief Start the data sender task
     */
    void start();
    
private:
    DataSender() = default;
    ~DataSender() = default;
    
    // Prevent copying
    DataSender(const DataSender&) = delete;
    DataSender& operator=(const DataSender&) = delete;
    
    /**
     * @brief Data sender task implementation
     * @param parameter Task parameter (unused)
     */
    static void task_impl(void* parameter);
    
    /**
     * @brief Send test data with SOC band-based LED flash control
     * 
     * Sends battery data and triggers LED flash on receiver when SOC band changes.
     * LED color corresponds to SOC level: red (low), orange (medium), green (high).
     */
    static void send_test_data_with_led_control();
};
