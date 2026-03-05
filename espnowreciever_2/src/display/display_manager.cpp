/**
 * @file display_manager.cpp
 * @brief Implementation of display manager
 */

#include "display_manager.h"
#include "../hal/hardware_config.h"
#include <logging.h>
#include <Arduino.h>

namespace Display {

// Static member initialization
HAL::IDisplayDriver* DisplayManager::driver_ = nullptr;
SemaphoreHandle_t DisplayManager::mutex_ = nullptr;
uint8_t DisplayManager::backlight_brightness_ = 0;
bool DisplayManager::initialized_ = false;

bool DisplayManager::init(HAL::IDisplayDriver* driver) {
    if (initialized_) {
        LOG_WARN("Display manager already initialized");
        return true;
    }
    
    if (!driver) {
        LOG_ERROR("Cannot initialize display manager with nullptr driver");
        return false;
    }
    
    driver_ = driver;
    
    // Create mutex for thread-safe access
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        LOG_ERROR("Failed to create display mutex");
        return false;
    }
    
    // Turn on display power (CRITICAL for T-Display-S3!)
    pinMode(HardwareConfig::GPIO_DISPLAY_POWER, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_DISPLAY_POWER, HIGH);
    LOG_INFO("Display power enabled (GPIO%d)", HardwareConfig::GPIO_DISPLAY_POWER);
    
    delay(100);  // Wait for power to stabilize
    
    // Initialize driver
    if (!driver_->init()) {
        LOG_ERROR("Failed to initialize display driver");
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
        return false;
    }
    
    // Set rotation for landscape mode
    driver_->set_rotation(1);  // 320x170 landscape
    
    // Setup backlight pin - keep OFF initially to prevent flash
    pinMode(HardwareConfig::GPIO_BACKLIGHT, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_BACKLIGHT, LOW);
    
    // Configure backlight PWM (but keep it OFF - splash fade will turn it on)
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcSetup(HardwareConfig::BACKLIGHT_PWM_CHANNEL, HardwareConfig::BACKLIGHT_FREQUENCY_HZ, HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
    ledcAttachPin(HardwareConfig::GPIO_BACKLIGHT, HardwareConfig::BACKLIGHT_PWM_CHANNEL);
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, 0);
    #else
    ledcAttach(HardwareConfig::GPIO_BACKLIGHT, HardwareConfig::BACKLIGHT_FREQUENCY_HZ, HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
    ledcWrite(HardwareConfig::GPIO_BACKLIGHT, 0);
    #endif
    
    backlight_brightness_ = 0;
    initialized_ = true;
    
    LOG_INFO("Display manager initialized (backlight OFF, awaiting splash)");
    return true;
}

HAL::IDisplayDriver* DisplayManager::get_driver() {
    return driver_;
}

bool DisplayManager::is_available() {
    return initialized_ && driver_ && driver_->is_available();
}

bool DisplayManager::lock(uint32_t timeout_ms) {
    if (!mutex_) {
        LOG_ERROR("Display mutex not initialized");
        return false;
    }
    
    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(mutex_, ticks) == pdTRUE;
}

void DisplayManager::unlock() {
    if (!mutex_) {
        LOG_ERROR("Display mutex not initialized");
        return;
    }
    xSemaphoreGive(mutex_);
}

void DisplayManager::set_backlight(uint8_t brightness) {
    if (!initialized_) {
        LOG_ERROR("Cannot set backlight - display not initialized");
        return;
    }
    
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, brightness);
    backlight_brightness_ = brightness;
}

uint8_t DisplayManager::get_backlight() {
    return backlight_brightness_;
}

void DisplayManager::fade_backlight(uint8_t target_brightness, uint32_t duration_ms) {
    if (!initialized_) {
        LOG_ERROR("Cannot fade backlight - display not initialized");
        return;
    }
    
    uint8_t current_brightness = backlight_brightness_;
    
    if (current_brightness == target_brightness) {
        return;  // Already at target
    }
    
    int32_t steps = duration_ms / 10;  // 10ms per step
    if (steps <= 0) steps = 1;
    
    int32_t brightness_diff = target_brightness - current_brightness;
    float step_size = (float)brightness_diff / (float)steps;
    
    LOG_DEBUG("Fading backlight %d -> %d over %dms (%d steps, %.2f per step)",
              current_brightness, target_brightness, duration_ms, steps, step_size);
    
    for (int32_t i = 0; i < steps; i++) {
        uint8_t new_brightness = current_brightness + (uint8_t)(step_size * (i + 1));
        set_backlight(new_brightness);
        delay(10);
    }
    
    // Ensure we hit the exact target
    set_backlight(target_brightness);
    
    LOG_DEBUG("Backlight fade complete (final brightness: %d)", target_brightness);
}

void DisplayManager::fill_screen(uint16_t color) {
    if (!is_available()) {
        LOG_WARN("Cannot fill screen - display not available");
        return;
    }
    
    if (!lock(pdMS_TO_TICKS(100))) {
        LOG_WARN("Cannot fill screen - failed to acquire display lock");
        return;
    }
    
    driver_->fill_screen(color);
    unlock();
}

void DisplayManager::clear_screen() {
    // Default to HAL-defined background color
    fill_screen(0x0000);  // Black
}

} // namespace Display
