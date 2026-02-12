#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief Section 11: Background Transmission Task
 * 
 * Reads data from EnhancedCache and transmits via ESP-NOW.
 * Non-blocking architecture - doesn't interfere with Battery Emulator control code.
 * 
 * Features:
 * - Rate limiting: 50ms intervals (20 msg/sec max)
 * - Low priority (Priority 2): Yields to Battery Emulator (Priority 5)
 * - Core pinning: Core 1 (isolated from control code on Core 0)
 * - Non-blocking: Fire-and-forget from cache
 * 
 * Handles both transient data (telemetry) and state data (config sync).
 */
class TransmissionTask {
public:
    static TransmissionTask& instance();
    
    /**
     * @brief Start the background transmission task
     * @param priority Task priority (default: Priority 2 - LOW)
     * @param core Core affinity (default: Core 1)
     */
    void start(uint8_t priority = 2, uint8_t core = 1);
    
    /**
     * @brief Stop the transmission task
     */
    void stop();
    
    /**
     * @brief Check if task is running
     */
    bool is_running() const { return task_handle_ != nullptr; }
    
    /**
     * @brief Get task handle
     */
    TaskHandle_t get_task_handle() const { return task_handle_; }
    
private:
    TransmissionTask() = default;
    ~TransmissionTask() = default;
    
    // Prevent copying
    TransmissionTask(const TransmissionTask&) = delete;
    TransmissionTask& operator=(const TransmissionTask&) = delete;
    
    // Task implementation
    static void task_impl(void* parameter);
    
    // Helper methods
    void transmit_next_transient();
    void transmit_next_state();
    
    // State
    TaskHandle_t task_handle_{nullptr};
    
    // Rate limiting
    static constexpr uint32_t TRANSMIT_INTERVAL_MS = 50;  // 20 msg/sec max
};
