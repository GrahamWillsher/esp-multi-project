#pragma once

#include "common.h"

// LED position constants
#define LED_X_POSITION (Display::SCREEN_WIDTH - 2 - 8)
#define LED_Y_POSITION (Display::SCREEN_HEIGHT / 2)
#define LED_RADIUS 8

// LED configuration
#define LED_FADE_STEPS 50

// Initialize LED gradients
void init_led_gradients();

// Flash LED with fade effect
void flash_led(LEDColor color, uint32_t cycle_duration_ms = 1000);

// Heartbeat LED effect (brief pulse, longer idle)
void heartbeat_led(LEDColor color, uint32_t cycle_duration_ms = 1200);

// Clear LED
void clear_led();

// Set LED to solid color
void set_led(LEDColor color);
