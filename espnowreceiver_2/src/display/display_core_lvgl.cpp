/**
 * @file display_core_lvgl.cpp
 * @brief LVGL display core — initialization, SOC/power update, screen management
 *
 * SCREEN TRANSITION RULE:
 *   Always use lv_scr_load_anim(new_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, auto_del).
 *   NEVER call lv_obj_del() on the currently active screen directly.
 *   auto_del=true lets LVGL update disp->act_scr before freeing the old screen.
 */

#ifdef USE_LVGL

#include "../common.h"
#include "../helpers.h"
#include "../hal/display/lvgl_driver.h"
#include "display_splash_lvgl.h"
#include "pages/status_page_lvgl.h"
#include <TFT_eSPI.h>

// TFT object — passed to LvglDriver::init() only. Never called directly here.
extern TFT_eSPI tft;

namespace Display {

static Pages::StatusPageLvgl* status_page_lvgl = nullptr;

// Pointer to the error screen when one is showing.
// nullptr means no error screen is currently active.
static lv_obj_t* s_error_scr = nullptr;

Pages::StatusPageLvgl* get_status_page_lvgl() {
    if (!status_page_lvgl) {
        status_page_lvgl = new Pages::StatusPageLvgl();
    }
    return status_page_lvgl;
}

} // namespace Display

// =============================================================================

void init_display() {
    LOG_INFO("DISPLAY", "Initializing LVGL display system...");

    if (!HAL::Display::LvglDriver::init(tft)) {
        LOG_ERROR("DISPLAY", "Failed to initialize LVGL driver");
        return;
    }

    lv_disp_t* disp = HAL::Display::LvglDriver::get_display();
    if (!disp || !lv_disp_get_scr_act(disp)) {
        LOG_ERROR("DISPLAY", "LVGL display registered but not ready");
        return;
    }
    LOG_INFO("DISPLAY", "LVGL display ready (disp=0x%08X)", (uint32_t)disp);
}

void displayInitialScreen() {
    Display::display_initial_screen_lvgl();
}

void displaySplashWithFade() {
    Display::display_splash_lvgl();
}

void display_soc(float newSoC) {
    Display::Pages::StatusPageLvgl* page = Display::get_status_page_lvgl();
    if (page) {
        page->update_soc(newSoC);
    }
}

void display_power(int32_t current_power_w) {
    Display::Pages::StatusPageLvgl* page = Display::get_status_page_lvgl();
    if (page) {
        page->update_power(current_power_w);
    }
}

void show_status_page() {
    // If an error screen is currently shown, it is the active screen.
    // Calling page->show() with auto_del=true will delete it and load
    // the status screen safely.  Clear our tracker first.
    Display::s_error_scr = nullptr;

    Display::Pages::StatusPageLvgl* page = Display::get_status_page_lvgl();
    if (page) {
        page->show();
    }
}

/**
 * Show a full-screen red error state via LVGL.
 * Uses auto_del=false so the StatusPage screen (if active) is preserved
 * in memory and can be restored when show_status_page() is called.
 */
void display_error_state_lvgl() {
    // If already showing an error screen, nothing to do
    if (Display::s_error_scr && lv_scr_act() == Display::s_error_scr) {
        return;
    }
    // Delete any stale error screen that is no longer active
    if (Display::s_error_scr) {
        lv_obj_del(Display::s_error_scr);
        Display::s_error_scr = nullptr;
    }

    lv_obj_t* err = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(err, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(err,   LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_border_width(err, 0,                  LV_PART_MAIN);
    lv_obj_set_style_pad_all(err,  0,                      LV_PART_MAIN);

    lv_obj_t* lbl = lv_label_create(err);
    lv_label_set_text(lbl, "ERROR");
    lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl,  &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    // auto_del=false: keeps the previous active screen (status page) alive
    // in memory so recovery via show_status_page() can reload it.
    lv_scr_load_anim(err, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    lv_timer_handler();

    Display::s_error_scr = err;
    LOG_WARN("DISPLAY", "Error screen displayed via LVGL");
}

/**
 * Show a full-screen fatal error screen with text via LVGL.
 * auto_del=true is safe here because we enter an infinite loop immediately
 * after and never need to recover the previous screen.
 */
void display_fatal_error_lvgl(const char* component, const char* message) {
    lv_obj_t* err = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(err, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(err,   LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_border_width(err, 0,                  LV_PART_MAIN);
    lv_obj_set_style_pad_all(err,  0,                      LV_PART_MAIN);

    lv_obj_t* title = lv_label_create(err);
    lv_label_set_text(title, "FATAL ERROR");
    lv_obj_set_style_text_color(title, lv_color_white(),       LV_PART_MAIN);
    lv_obj_set_style_text_font(title,  &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t* comp = lv_label_create(err);
    lv_label_set_text(comp, component ? component : "");
    lv_obj_set_style_text_color(comp, lv_color_white(),       LV_PART_MAIN);
    lv_obj_set_style_text_font(comp,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(comp, LV_ALIGN_CENTER, 0, 5);

    lv_obj_t* msg = lv_label_create(err);
    lv_label_set_text(msg, message ? message : "");
    lv_obj_set_style_text_color(msg, lv_color_white(),       LV_PART_MAIN);
    lv_obj_set_style_text_font(msg,  &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 25);

    // auto_del=true: previous screen will be deleted — acceptable because
    // we enter an infinite LED-flash loop immediately after this call.
    lv_scr_load_anim(err, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);

    // Force render before we enter the infinite loop
    for (int i = 0; i < 20; i++) {
        lv_timer_handler();
        delay(5);
    }
    LOG_ERROR("DISPLAY", "Fatal error screen: [%s] %s", component, message);
}

void lvgl_task_handler() {
    HAL::Display::LvglDriver::task_handler();
}

#endif // USE_LVGL


