#pragma once

#include "common.h"
#include "../config/display_config.h"

// LED position constants (from DisplayConfig)
#define LED_X_POSITION (DisplayConfig::DISPLAY_WIDTH - 2 - DisplayConfig::STATUS_INDICATOR_SIZE)
#define LED_Y_POSITION (Display::SCREEN_HEIGHT / 2)
#define LED_RADIUS (DisplayConfig::STATUS_INDICATOR_SIZE - 3)

// Clear LED
void clear_led();

// Set LED to solid color
void set_led(LEDColor color);
