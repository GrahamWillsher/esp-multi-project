/**
 * @file lvgl_display.h
 * @brief LVGL Display Implementation
 * 
 * Pure LVGL implementation using asynchronous, event-driven rendering.
 * Uses LVGL's native animation and widget system for modern UI effects.
 * 
 * Used when USE_LVGL is defined during compilation.
 */

#pragma once

#ifdef USE_LVGL

#include "../display_interface.h"
#include "../pages/status_page_lvgl.h"
#include <lvgl.h>
#include <TFT_eSPI.h>

namespace Display {

/**
 * LVGL Display Implementation
 * 
 * Provides asynchronous, event-driven display operations using LVGL library.
 * Animations are queued and processed by LVGL's message loop.
 * task_handler() MUST be called regularly from main loop.
 * 
 * Architecture:
 * - LVGL objects and widgets for UI
 * - Asynchronous animations via lv_scr_load_anim() and lv_anim_t
 * - Message loop pumping via lv_timer_handler()
 * - Professional animations and transitions
 */
class LvglDisplay : public IDisplay {
public:
    /**
     * Constructor - initialize LVGL and TFT_eSPI instances
     */
    LvglDisplay();
    
    /**
     * Destructor - cleanup LVGL
     */
    virtual ~LvglDisplay() = default;
    
    // =========================================================================
    // IDisplay Implementation
    // =========================================================================
    
    /**
     * Initialize LVGL core, display driver, and hardware
     */
    bool init() override;
    
    /**
     * Display splash screen with fade-in animation
     * 
     * LVGL approach:
     * 1. Create splash screen as LVGL object
     * 2. Load image into LVGL image widget
     * 3. Queue fade-in animation via lv_scr_load_anim()
     * 4. Queue backlight animation via lv_anim_t
     * 5. Pump message loop until animations complete
     * 6. Hold splash while continuously pumping
     * 7. Return (fade-out transition follows)
     */
    void display_splash_with_fade() override;
    
    /**
     * Display Ready screen after splash
     */
    void display_initial_screen() override;
    
    /**
     * Update SOC display value
     */
    void update_soc(float soc_percent) override;
    
    /**
     * Update power display value
     */
    void update_power(int32_t power_w) override;
    
    /**
     * Show status page
     */
    void show_status_page() override;
    
    /**
     * Show error state (red screen)
     */
    void show_error_state() override;
    
    /**
     * Show fatal error
     */
    void show_fatal_error(const char* component, const char* message) override;
    
    /**
     * Process LVGL tasks - CRITICAL for animations to work
     * 
     * Pumps LVGL message loop, processing animations, rendering updates, etc.
     * Must be called regularly from main loop (ideally every 10-20ms).
     * 
     * For TFT (non-LVGL) this is a no-op, but for LVGL this is essential.
     */
    void task_handler() override;
    
    // =========================================================================
    // Static Callbacks (for LVGL)
    // =========================================================================
    
    /**
     * Flush callback - called by LVGL when frame buffer ready
     * Transfers pixels from LVGL buffer to display via TFT_eSPI
     */
    static void flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p);
    
private:
    TFT_eSPI tft_;
    lv_disp_t* disp_;
    lv_disp_drv_t disp_drv_;
    lv_disp_draw_buf_t disp_buf_;
    lv_color_t* buf1_;
    lv_color_t* buf2_;
    
    // Pages and widgets
    Pages::StatusPageLvgl* status_page_lvgl_;
    lv_obj_t* status_page_;
    lv_obj_t* error_page_;
    
    // =========================================================================
    // Initialization Helpers
    // =========================================================================
    
    /**
     * Initialize LVGL core library
     */
    void init_lvgl_core();
    
    /**
     * Initialize LVGL display driver (callbacks, buffers, resolution)
     */
    void init_lvgl_driver();
    
    /**
     * Initialize TFT hardware (GPIO, SPI, display controller)
     */
    void init_hardware();
    
    /**
     * Allocate LVGL display buffers in PSRAM
     */
    bool allocate_buffers();
    
    // =========================================================================
    // Animation Helpers
    // =========================================================================
    
    /**
     * Animate backlight from current to target brightness
     * Uses LVGL's animation system (non-blocking)
     * 
     * @param target Target brightness (0-255)
     * @param duration_ms Animation duration in milliseconds
     */
    void animate_backlight_lvgl(uint8_t target, uint32_t duration_ms);
    
    /**
     * Set backlight brightness immediately (not animated)
     * 
     * @param brightness Brightness value (0-255)
     */
    void set_backlight(uint8_t brightness);
    
    /**
     * Wait for all animations to complete
     * 
     * Pumps LVGL message loop until all queued animations finish.
     * Timeout prevents infinite loops if animation never completes.
     * 
     * @param timeout_ms Maximum time to wait
     */
    void wait_for_animation(uint32_t timeout_ms);
    
    /**
     * Hold screen visible for specified duration
     * 
     * Keeps pumping LVGL message loop while holding time.
     * Allows LVGL to continue processing events and rendering.
     * 
     * @param duration_ms Time to hold in milliseconds
     */
    void hold_screen_for(uint32_t duration_ms);
    
    // =========================================================================
    // Splash Screen Helpers
    // =========================================================================
    
    /**
     * Create splash screen object with image
     * 
     * @return lv_obj_t* pointer to splash screen object
     */
    lv_obj_t* create_splash_screen();
    
    /**
     * Load splash image from LittleFS and create LVGL descriptor
     * 
     * @return lv_img_dsc_t* pointer to image descriptor, nullptr on error
     */
    lv_img_dsc_t* load_splash_image();
    
    // =========================================================================
    // Page Creation Helpers
    // =========================================================================
    
    /**
     * Create status page with SOC and power widgets
     */
    void create_status_page();
    
    /**
     * Create error state page (red screen)
     */
    void create_error_page();
};

} // namespace Display

#endif // USE_LVGL
