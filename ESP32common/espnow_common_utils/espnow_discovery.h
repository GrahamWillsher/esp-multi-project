/**
 * @file espnow_discovery.h
 * @brief Common ESP-NOW bidirectional discovery task
 * 
 * Provides periodic announcement broadcasts for peer discovery.
 * Supports both transmitter and receiver roles with callback hooks
 * for project-specific connection tracking.
 * 
 * @note Uses OOP singleton pattern with function callbacks
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>

/**
 * @brief Manages periodic ESP-NOW announcement broadcasts
 * 
 * Singleton class that sends periodic PROBE messages to discover peers.
 * Automatically stops when peer connection is established (via callback).
 */
class EspnowDiscovery {
public:
    /**
     * @brief Get singleton instance
     */
    static EspnowDiscovery& instance();
    
    /**
     * @brief Start periodic announcement task
     * @param is_connected_callback Callback to check if peer is connected
     * @param interval_ms Announcement interval in milliseconds (default: 5000)
     * @param task_priority FreeRTOS task priority (default: 1)
     * @param stack_size Task stack size in bytes (default: 2048)
     */
    void start(std::function<bool()> is_connected_callback,
               uint32_t interval_ms = 5000,
               uint8_t task_priority = 1,
               uint32_t stack_size = 2048);
    
    /**
     * @brief Stop announcement task
     */
    void stop();
    
    /**
     * @brief Suspend announcement broadcasts (keep task alive)
     */
    void suspend();
    
    /**
     * @brief Resume announcement broadcasts
     */
    void resume();
    
    /**
     * @brief Restart announcement task (stop + start)
     */
    void restart();
    
    /**
     * @brief Check if announcement task is running
     * @return true if task is active
     */
    bool is_running() const { return task_handle_ != nullptr; }
    
    /**
     * @brief Check if announcements are suspended
     * @return true if suspended
     */
    bool is_suspended() const { return suspended_; }
    
    /**
     * @brief Get task handle
     * @return Task handle (nullptr if not running)
     */
    TaskHandle_t get_task_handle() const { return task_handle_; }
    
private:
    EspnowDiscovery() = default;
    ~EspnowDiscovery() = default;
    
    // Prevent copying
    EspnowDiscovery(const EspnowDiscovery&) = delete;
    EspnowDiscovery& operator=(const EspnowDiscovery&) = delete;
    
    /**
     * @brief Task implementation (static wrapper)
     */
    static void task_impl(void* parameter);
    
    /**
     * @brief Task configuration structure
     */
    struct TaskConfig {
        std::function<bool()> is_connected;
        uint32_t interval_ms;
    };
    
    TaskHandle_t task_handle_{nullptr};
    TaskConfig* config_{nullptr};
    volatile bool suspended_{false};
    
    // Store last configuration for restart
    uint32_t last_interval_ms_{5000};
    uint8_t last_task_priority_{1};
    uint32_t last_stack_size_{2048};
    std::function<bool()> last_is_connected_callback_;
};
