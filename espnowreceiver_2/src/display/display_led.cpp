/*
 * LED Indicator primitives
 * Runtime animation sequencing is owned by task_led_renderer in main.cpp.
 */

#include "display_led.h"

#ifdef USE_LVGL
#include "../hal/display/lvgl_driver.h"
#include <lvgl.h>
#endif

#ifdef USE_LVGL
static lv_obj_t* s_led_obj = nullptr;

static lv_color_t rgb565_to_lv(uint16_t c) {
    const uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
    const uint8_t g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
    const uint8_t b = (uint8_t)((c & 0x1F) * 255 / 31);
    return lv_color_make(r, g, b);
}

static void ensure_led_obj() {
    lv_obj_t* scr = lv_scr_act();
    if (!scr) return;

    if (!s_led_obj || !lv_obj_is_valid(s_led_obj) || lv_obj_get_parent(s_led_obj) != scr) {
        s_led_obj = lv_obj_create(scr);
        lv_obj_set_size(s_led_obj, LED_RADIUS * 2, LED_RADIUS * 2);
        lv_obj_align(s_led_obj, LV_ALIGN_CENTER, (Display::SCREEN_WIDTH / 2) - (LED_RADIUS + 2), 0);
        lv_obj_set_style_radius(s_led_obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_led_obj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_led_obj, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_led_obj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_led_obj, rgb565_to_lv(Display::tft_background), LV_PART_MAIN);
    }
}

static void apply_led_color_rgb565(uint16_t color565) {
    ensure_led_obj();
    if (!s_led_obj) return;
    lv_obj_set_style_bg_color(s_led_obj, rgb565_to_lv(color565), LV_PART_MAIN);
    lv_obj_invalidate(s_led_obj);
    HAL::Display::LvglDriver::task_handler();
}
#endif

// Clear LED
void clear_led() {
#ifdef USE_LVGL
    apply_led_color_rgb565(Display::tft_background);
#else
    tft.fillCircle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, Display::tft_background);
#endif
}

// Set LED to solid color
void set_led(LEDColor color) {
    // Array lookup is more efficient than switch (enum values = array indices)
    static constexpr uint16_t led_colors[] = {
        LEDColors::RED,    // LED_RED = 0
        LEDColors::GREEN,  // LED_GREEN = 1
        LEDColors::ORANGE, // LED_ORANGE = 2
        LEDColors::BLUE    // LED_BLUE = 3
    };

    if (color <= LED_BLUE) {
#ifdef USE_LVGL
        apply_led_color_rgb565(led_colors[color]);
#else
        tft.fillCircle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, led_colors[color]);
#endif
    }
}
