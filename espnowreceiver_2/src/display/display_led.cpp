/*
 * LED Indicator primitives
 * Runtime animation sequencing is owned by task_led_renderer in main.cpp.
 */

#include "display_led.h"

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
        LEDColors::ORANGE, // LED_ORANGE = 2
        LEDColors::BLUE    // LED_BLUE = 3
    };

    if (color <= LED_BLUE) {
        tft.fillCircle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, led_colors[color]);
    }
}
