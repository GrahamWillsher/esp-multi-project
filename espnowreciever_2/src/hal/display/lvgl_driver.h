/**
 * LVGL Display Driver for ESP32-S3 T-Display
 * 
 * Implements LVGL display driver using TFT_eSPI as the low-level backend.
 * Provides:
 * - Display buffer management (PSRAM-backed)
 * - Flush callback for LVGL
 * - Backlight control integration
 * - Custom PSRAM memory allocators
 */

#pragma once

#ifdef USE_LVGL

#include <lvgl.h>
#include <TFT_eSPI.h>
#include "hardware_config.h"

namespace HAL {
namespace Display {

/**
 * LVGL Display Driver
 * Bridges LVGL rendering to TFT_eSPI hardware
 */
class LvglDriver {
public:
    /**
     * Initialize LVGL and display driver
     * @param tft Reference to TFT_eSPI instance
     * @return true if initialization successful
     */
    static bool init(TFT_eSPI& tft);
    
    /**
     * Process LVGL tasks (call periodically from loop)
     * Handles rendering, animations, input, etc.
     */
    static void task_handler();
    
    /**
     * Animate backlight brightness over specified duration
     * @param target Target brightness (0-255)
     * @param duration_ms Duration of animation in milliseconds
     */
    static void animate_backlight_to(uint8_t target, uint32_t duration_ms);
    
    /**
     * Set backlight brightness (0-255) immediately
     */
    static void set_backlight(uint8_t brightness);
    
    /**
     * Get current backlight brightness
     */
    static uint8_t get_backlight();
    
    /**
     * Get LVGL display object (for loading screens)
     */
    static lv_disp_t* get_display();

private:
    static TFT_eSPI* tft_;
    static lv_disp_drv_t disp_drv_;
    static lv_disp_draw_buf_t disp_buf_;
    static lv_disp_t* disp_;
    static lv_color_t* buf1_;
    static lv_color_t* buf2_;
    static uint8_t current_backlight_;
    
    /**
     * LVGL flush callback - transfers buffer to display
     */
    static void flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p);
    
    /**
     * Initialize hardware (GPIO, PWM for backlight)
     */
    static void init_hardware();
    
    /**
     * Allocate display buffers in PSRAM
     */
    static bool allocate_buffers();
};

/**
 * PSRAM Memory Allocators for LVGL
 * These are called by LVGL for all dynamic allocations
 */
void* lv_custom_mem_alloc(size_t size);
void lv_custom_mem_free(void* ptr);
void* lv_custom_mem_realloc(void* ptr, size_t new_size);

/**
 * Custom logging function for LVGL
 */
void lv_custom_log(const char* buf);

} // namespace Display
} // namespace HAL

#endif // USE_LVGL
