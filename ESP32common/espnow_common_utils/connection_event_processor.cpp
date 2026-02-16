/**
 * @file connection_event_processor.cpp
 * @brief Event Processor Task Implementation (IDENTICAL for Both TX and RX)
 * 
 * This is COMMON CODE that both projects include and use without modification.
 */

#include "connection_event_processor.h"
#include "connection_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>

void connection_event_processor_task(void* param) {
    Serial.printf("[PROC] Event processor task started\n");
    
    // Give system time to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));
    
    while (true) {
        // Process all pending connection events
        EspNowConnectionManager::instance().process_events();
        
        // Sleep briefly to prevent hogging CPU
        // Adjust delay based on event frequency needs
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

TaskHandle_t create_connection_event_processor(uint8_t priority, uint8_t core) {
    // Calculate stack size (use 3KB for safety)
    const uint16_t STACK_SIZE = 3072 / sizeof(StackType_t);
    
    TaskHandle_t handle = nullptr;
    
    BaseType_t result = xTaskCreatePinnedToCore(
        connection_event_processor_task,  // Task function
        "ConnEvents",                      // Task name
        STACK_SIZE,                        // Stack size
        nullptr,                           // Task parameter
        priority,                          // Priority
        &handle,                           // Task handle
        core                               // Core
    );
    
    if (result != pdPASS) {
        Serial.printf("[PROC] ERROR: Failed to create event processor task!\n");
        return nullptr;
    }
    
    Serial.printf("[PROC] Event processor task created (priority=%d, core=%d)\n", priority, core);
    return handle;
}
