/*
 * LED Indicator Functions
 * Simulated LED on screen with fade animations
 */

#include "display_led.h"

// LED fade gradient arrays
static uint16_t led_red_gradient[LED_FADE_STEPS + 1];
static uint16_t led_green_gradient[LED_FADE_STEPS + 1];
static uint16_t led_orange_gradient[LED_FADE_STEPS + 1];
static bool led_gradients_initialized = false;
static uint16_t led_last_background_color = 0;

// Forward declaration of helper function
void pre_calculate_color_gradient(uint16_t start_color, uint16_t end_color, int steps, uint16_t* output);

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
    
    uint32_t delay_per_step = cycle_duration_ms / (LED_FADE_STEPS * 2);
    if (delay_per_step < 5) delay_per_step = 5;
    
    // Phase 1: Fade from color to background
    for (int step = 0; step <= LED_FADE_STEPS; step++) {
        tft.fillCircle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, gradient[step]);
        if (step < LED_FADE_STEPS) {
            smart_delay(delay_per_step);
        }
    }
    
    smart_delay(100);
    
    // Phase 2: Fade from background to color
    for (int step = LED_FADE_STEPS - 1; step >= 0; step--) {
        tft.fillCircle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, gradient[step]);
        if (step > 0) {
            smart_delay(delay_per_step);
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
    tft.fillCircle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, Display::tft_background);
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
        tft.fillCircle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, led_colors[color]);
    }
}
