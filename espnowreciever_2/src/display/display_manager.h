#pragma once

/**
 * @file display_manager.h
 * @brief Central display management with HAL abstraction
 * 
 * Manages display driver lifecycle and provides thread-safe access
 * to display operations. Replaces legacy global tft object with
 * clean HAL-based architecture.
 */

#include "../../ESP32common/hal/display/idisplay_driver.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace Display {

/**
 * @brief Display manager with HAL abstraction
 * 
 * Singleton managing display driver and coordinating access.
 * Provides thread-safe display operations and backlight control.
 */
class DisplayManager {
public:
    /**
     * @brief Initialize display manager with specific driver
     * @param driver Display HAL implementation (takes ownership)
     * @return true if initialization successful
     */
    static bool init(HAL::IDisplayDriver* driver);
    
    /**
     * @brief Get display driver instance
     * @return Pointer to display driver (nullptr if not initialized)
     */
    static HAL::IDisplayDriver* get_driver();
    
    /**
     * @brief Check if display is initialized and available
     */
    static bool is_available();
    
    /**
     * @brief Lock display for exclusive access (thread-safe)
     * @param timeout_ms Timeout in milliseconds (portMAX_DELAY for infinite)
     * @return true if lock acquired
     */
    static bool lock(uint32_t timeout_ms = portMAX_DELAY);
    
    /**
     * @brief Unlock display (release exclusive access)
     */
    static void unlock();
    
    /**
     * @brief Set backlight brightness (hardware-specific)
     * @param brightness 0-255 (0=off, 255=full brightness)
     */
    static void set_backlight(uint8_t brightness);
    
    /**
     * @brief Get current backlight brightness
     * @return Current brightness (0-255)
     */
    static uint8_t get_backlight();
    
    /**
     * @brief Fade backlight to target brightness
     * @param target_brightness 0-255
     * @param duration_ms Fade duration in milliseconds
     */
    static void fade_backlight(uint8_t target_brightness, uint32_t duration_ms);
    
private:
    DisplayManager() = delete;  // Static class - no instances
    
    static HAL::IDisplayDriver* driver_;
    static SemaphoreHandle_t mutex_;
    static uint8_t backlight_brightness_;
    static bool initialized_;
};

} // namespace Display
