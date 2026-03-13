/*
 * Helper Functions
 * Common utility functions used across modules
 */

#include "helpers.h"

// Pre-calculate color gradient
void pre_calculate_color_gradient(uint16_t start_color, uint16_t end_color, int steps, uint16_t* output) {
    uint8_t start_r = (start_color >> 11) & 0x1F;
    uint8_t start_g = (start_color >> 5) & 0x3F;
    uint8_t start_b = start_color & 0x1F;
    
    uint8_t end_r = (end_color >> 11) & 0x1F;
    uint8_t end_g = (end_color >> 5) & 0x3F;
    uint8_t end_b = end_color & 0x1F;
    
    for (int i = 0; i <= steps; i++) {
        float ratio = (float)i / (float)steps;
        uint8_t r = start_r + (uint8_t)((end_r - start_r) * ratio);
        uint8_t g = start_g + (uint8_t)((end_g - start_g) * ratio);
        uint8_t b = start_b + (uint8_t)((end_b - start_b) * ratio);
        output[i] = (r << 11) | (g << 5) | b;
    }
}

// Get power bar color based on value
uint16_t get_power_color(int32_t power, int max_power) {
    if (power > 100) return TFT_RED;
    else if (power < -100) return TFT_GREEN;
    else return TFT_BLUE;
}

// Calculate checksum for ESP-NOW payload
uint16_t calculate_checksum(const espnow_payload_t *payload) {
    uint16_t sum = payload->soc;
    sum += (uint16_t)payload->power;
    return sum;
}

/**
 * @brief Task-aware delay implementation
 * 
 * Smart delay that automatically uses FreeRTOS's vTaskDelay() when available,
 * falling back to Arduino's delay() during early initialization when the
 * scheduler hasn't started yet.
 * 
 * **Algorithm**:
 * 1. Check if running in a valid FreeRTOS task context
 * 2. Check if FreeRTOS scheduler is running
 * 3. If both true: Use vTaskDelay() (task yields, others can run)
 * 4. If either false: Use Arduino delay() (blocks, but needed at startup)
 * 
 * **Key Logic Points**:
 * - xTaskGetCurrentTaskHandle() returns NULL if not in task context
 * - xTaskGetSchedulerState() returns taskSCHEDULER_RUNNING when ready
 * - pdMS_TO_TICKS() converts milliseconds to scheduler ticks
 * - Ensures minimum 1-tick delay for sub-tick ms values
 * 
 * **Performance Note**:
 * This function is called 30+ times throughout initialization and core loops.
 * The overhead check is negligible (< 1µs) compared to typical delay durations.
 * 
 * @see smart_delay() in common.h for user documentation
 */
void smart_delay(uint32_t ms) {
    // Check if we're in a valid FreeRTOS context with scheduler running
    if (xTaskGetCurrentTaskHandle() != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        // We're in FreeRTOS task context - yield to scheduler
        TickType_t ticks = pdMS_TO_TICKS(ms);
        
        // Ensure minimum 1 tick even for very small ms values
        // (prevents zero-delay busy loops)
        if (ticks == 0 && ms > 0) {
            ticks = 1;
        }
        
        vTaskDelay(ticks);
    } else {
        // Early initialization - scheduler not running yet, use blocking delay
        delay(ms);
    }
}
