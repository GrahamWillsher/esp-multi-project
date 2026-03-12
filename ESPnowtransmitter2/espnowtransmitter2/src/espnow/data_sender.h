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
     * @brief Send battery telemetry data
     *
     * Sends battery telemetry through the cache/transmission selector path.
     * LED publishing is owned by the event/status pipeline.
     */
    static void send_battery_data();
};
