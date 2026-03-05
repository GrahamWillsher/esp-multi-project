/*
 * LED Indicator Functions
 * Simulated LED on screen with fade animations
 */

#include "display_led.h"
#include "display_manager.h"
#include "../helpers.h"

#ifdef USE_LVGL
#include "../hal/display/lvgl_driver.h"
#include <lvgl.h>
#endif

// LED fade gradient arrays
static uint16_t led_red_gradient[LED_FADE_STEPS + 1];
static uint16_t led_green_gradient[LED_FADE_STEPS + 1];
static uint16_t led_orange_gradient[LED_FADE_STEPS + 1];
static bool led_gradients_initialized = false;
static uint16_t led_last_background_color = 0;

// Forward declaration of helper function
void pre_calculate_color_gradient(uint16_t start_color, uint16_t end_color, int steps, uint16_t* output);

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

// Initialize LED fade gradients
void init_led_gradients() {
    if (led_gradients_initialized && led_last_background_color == Display::tft_background) {
        return;  // Already initialized with current background
    }

    // Pre-calculate gradients from each color to current background
    pre_calculate_color_gradient(LEDColors::RED, Display::tft_background, LED_FADE_STEPS, led_red_gradient);
    pre_calculate_color_gradient(LEDColors::GREEN, Display::tft_background, LED_FADE_STEPS, led_green_gradient);
    pre_calculate_color_gradient(LEDColors::ORANGE, Display::tft_background, LED_FADE_STEPS, led_orange_gradient);

    led_last_background_color = Display::tft_background;
    led_gradients_initialized = true;
    Serial.printf("[LED] Gradients initialized for background color 0x%04X (%d steps per color)\n",
                  Display::tft_background, LED_FADE_STEPS);
}

// Flash LED with fade effect
void flash_led(LEDColor color, uint32_t cycle_duration_ms) {
    if (!led_gradients_initialized) {
        init_led_gradients();
    }

    // Select appropriate gradient
    uint16_t* gradient = nullptr;
    switch (color) {
        case LED_RED:    gradient = led_red_gradient; break;
        case LED_GREEN:  gradient = led_green_gradient; break;
        case LED_ORANGE: gradient = led_orange_gradient; break;
        default: return;
    }

    // Distribute time evenly: total = fade_in + hold + fade_out
    // Array has LED_FADE_STEPS + 1 frames (51 frames = 50 transitions between them)
    uint32_t fade_time = cycle_duration_ms * 40 / 100;  // 40% for fade in
    uint32_t hold_time = cycle_duration_ms * 20 / 100;  // 20% hold at full brightness
    uint32_t fade_out_time = cycle_duration_ms * 40 / 100;  // 40% for fade out

    uint32_t fade_in_step_ms = fade_time / LED_FADE_STEPS;   // Delay BETWEEN frames, not after last frame
    uint32_t fade_out_step_ms = fade_out_time / LED_FADE_STEPS;
    if (fade_in_step_ms < 2) fade_in_step_ms = 2;
    if (fade_out_step_ms < 2) fade_out_step_ms = 2;

    // Phase 1: Fade from background to full color (reverse through gradient)
    for (int step = LED_FADE_STEPS; step >= 0; step--) {
#ifdef USE_LVGL
        apply_led_color_rgb565(gradient[step]);
#else
        auto* driver = Display::DisplayManager::get_driver();
        driver->fill_circle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, gradient[step]);
#endif
        if (step > 0) {  // Don't delay after final frame
            smart_delay(fade_in_step_ms);
        }
    }

    // Phase 2: Hold at full color
    smart_delay(hold_time);

    // Phase 3: Fade from full color back to background
    for (int step = 0; step <= LED_FADE_STEPS; step++) {
#ifdef USE_LVGL
        apply_led_color_rgb565(gradient[step]);
#else
        auto* driver = Display::DisplayManager::get_driver();
        driver->fill_circle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, gradient[step]);
#endif
        if (step < LED_FADE_STEPS) {  // Don't delay after final frame
            smart_delay(fade_out_step_ms);
        }
    }
}

// Heartbeat LED effect (brief pulse, longer idle)
void heartbeat_led(LEDColor color, uint32_t cycle_duration_ms) {
    if (cycle_duration_ms < 400) cycle_duration_ms = 400;

    uint32_t pulse_on_ms = cycle_duration_ms / 5;   // 20% on
    uint32_t pulse_off_ms = cycle_duration_ms - pulse_on_ms;

    set_led(color);
    smart_delay(pulse_on_ms);
    clear_led();
    smart_delay(pulse_off_ms);
}

// Clear LED
void clear_led() {
#ifdef USE_LVGL
    apply_led_color_rgb565(Display::tft_background);
#else
    auto* driver = Display::DisplayManager::get_driver();
    driver->fill_circle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, Display::tft_background);
#endif
}

// Set LED to solid color
void set_led(LEDColor color) {
    // Array lookup is more efficient than switch (enum values = array indices)
    static constexpr uint16_t led_colors[] = {
        LEDColors::RED,    // LED_RED = 0
        LEDColors::GREEN,  // LED_GREEN = 1
        LEDColors::ORANGE  // LED_ORANGE = 2
    };

    if (color <= LED_ORANGE) {
#ifdef USE_LVGL
        apply_led_color_rgb565(led_colors[color]);
#else
        auto* driver = Display::DisplayManager::get_driver();
        driver->fill_circle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, led_colors[color]);
#endif
    }
}
