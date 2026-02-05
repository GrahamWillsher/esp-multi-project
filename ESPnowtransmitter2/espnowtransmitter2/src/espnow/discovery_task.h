#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief Manages periodic ESP-NOW announcement broadcasts for discovery
 * 
 * Singleton wrapper around common EspnowDiscovery component.
 * Provides project-specific interface while using shared implementation.
 */
class DiscoveryTask {
public:
    static DiscoveryTask& instance();
    
    /**
     * @brief Start the periodic announcement task
     */
    void start();
    
    /**
     * @brief Get the task handle
     * @return Task handle (NULL if not running)
     */
    TaskHandle_t get_task_handle() const { return task_handle_; }
    
private:
    DiscoveryTask() = default;
    ~DiscoveryTask() = default;
    
    // Prevent copying
    DiscoveryTask(const DiscoveryTask&) = delete;
    DiscoveryTask& operator=(const DiscoveryTask&) = delete;
    
    TaskHandle_t task_handle_{nullptr};
};
