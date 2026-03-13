/**
 * @file lvgl_display.cpp
 * @brief LVGL Display Implementation
 *
 * Pure LVGL implementation using asynchronous, event-driven rendering.
 * Animations are processed by LVGL's message loop (lv_timer_handler).
 * task_handler() MUST be called regularly from main loop for animations to work.
 *
 * Architecture:
 * - LVGL objects and widgets for all UI elements
 * - Pre-calculated color gradients for SOC display
 * - LVGL animations for splash, LED, backlight
 * - Asynchronous message loop pumping
 */

#ifdef USE_LVGL

#include "lvgl_display.h"
#include "../common.h"
#include "../helpers.h"
#include "../layout/display_layout_spec.h"
#include "../display_splash_lvgl.h"
#include "../../hal/display/lvgl_driver.h"
#include <esp_heap_caps.h>
#include <cmath>
#include <algorithm>

namespace Display {

// ============================================================================
// LvglDisplay Constructor & Initialization
// ============================================================================

LvglDisplay::LvglDisplay()
        : status_page_lvgl_(nullptr),
            disp_(nullptr), buf1_(nullptr), buf2_(nullptr),
      status_page_(nullptr), error_page_(nullptr) {
    // TFT_eSPI initialized in-place by default constructor
}

bool LvglDisplay::init() {
    LOG_INFO("LVGL_DISPLAY", "Initializing LVGL Display...");

    // Initialize LVGL driver (handles TFT_eSPI hardware init)
    if (!HAL::Display::LvglDriver::init(tft_)) {
        LOG_ERROR("LVGL_DISPLAY", "Failed to initialize LVGL driver");
        return false;
    }
    LOG_INFO("LVGL_DISPLAY", "LVGL driver initialized");

    disp_ = HAL::Display::LvglDriver::get_display();
    if (!disp_) {
        LOG_ERROR("LVGL_DISPLAY", "Failed to get LVGL display object");
        return false;
    }

    LOG_INFO("LVGL_DISPLAY", "LVGL Display initialized successfully");
    return true;
}

// ============================================================================
// Display Lifecycle Methods
// ============================================================================

void LvglDisplay::display_splash_with_fade() {
    LOG_INFO("LVGL_DISPLAY", "=== Splash START ===");
    Display::display_splash_lvgl();
    LOG_INFO("LVGL_DISPLAY", "=== Splash END ===");
}

void LvglDisplay::display_initial_screen() {
    LOG_INFO("LVGL_DISPLAY", "Displaying initial Ready screen...");
    Display::display_initial_screen_lvgl();
}

void LvglDisplay::update_soc(float soc_percent) {
    if (!status_page_lvgl_) {
        status_page_lvgl_ = new Pages::StatusPageLvgl();
    }
    status_page_lvgl_->update_soc(soc_percent);
}

void LvglDisplay::update_power(int32_t power_w) {
    if (!status_page_lvgl_) {
        status_page_lvgl_ = new Pages::StatusPageLvgl();
    }
    status_page_lvgl_->update_power(power_w);
}

void LvglDisplay::show_status_page() {
    LOG_INFO("LVGL_DISPLAY", "Showing status page...");

    if (!status_page_lvgl_) {
        status_page_lvgl_ = new Pages::StatusPageLvgl();
    }
    status_page_lvgl_->show();
}

void LvglDisplay::show_error_state() {
    LOG_WARN("LVGL_DISPLAY", "Showing error state");
    
    lv_obj_t* error_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(error_scr, lv_color_hex(0xFF0000), 0);
    
    lv_obj_t* label = lv_label_create(error_scr);
    lv_label_set_text(label, "ERROR");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    
    lv_scr_load(error_scr);
}

void LvglDisplay::show_fatal_error(const char* component, const char* message) {
    LOG_ERROR("LVGL_DISPLAY", "Showing fatal error: [%s] %s", 
              component ? component : "unknown", message ? message : "");
    
    lv_obj_t* error_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(error_scr, lv_color_hex(0xFF0000), 0);
    
    // Title
    lv_obj_t* title = lv_label_create(error_scr);
    lv_label_set_text(title, "FATAL ERROR");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    
    // Component
    if (component) {
        lv_obj_t* comp_label = lv_label_create(error_scr);
        lv_label_set_text(comp_label, component);
        lv_obj_align(comp_label, LV_ALIGN_TOP_LEFT, 20, 60);
        lv_obj_set_style_text_color(comp_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(comp_label, &lv_font_montserrat_14, 0);
    }
    
    // Message
    if (message) {
        lv_obj_t* msg_label = lv_label_create(error_scr);
        lv_label_set_text(msg_label, message);
        lv_obj_align(msg_label, LV_ALIGN_TOP_LEFT, 20, 100);
        lv_obj_set_style_text_color(msg_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(msg_label, &lv_font_montserrat_14, 0);
    }
    
    lv_scr_load(error_scr);
}

void LvglDisplay::task_handler() {
    // Pump LVGL message loop
    // This processes timers, animations, rendering, events, etc.
    HAL::Display::LvglDriver::task_handler();
}

// ============================================================================
// Static Callbacks
// ============================================================================

void LvglDisplay::flush_cb(lv_disp_drv_t* disp, const lv_area_t* area,
                           lv_color_t* color_p) {
    // This is actually implemented in LvglDriver::flush_cb
    // We keep this signature for compatibility with IDisplay interface
}

} // namespace Display

#endif // USE_LVGL
