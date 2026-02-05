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

// Task-aware delay function
void smart_delay(uint32_t ms) {
    if (xTaskGetCurrentTaskHandle() != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        TickType_t ticks = pdMS_TO_TICKS(ms);
        if (ticks == 0 && ms > 0) {
            ticks = 1;
        }
        vTaskDelay(ticks);
    } else {
        delay(ms);
    }
}
