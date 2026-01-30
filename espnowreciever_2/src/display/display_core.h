#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "../common.h"

// Display core functions

// Initialize TFT display hardware and backlight
void init_display();

// Display a proportional font number with centered digits
void display_centered_proportional_number(const GFXfont* font, float number, uint16_t color, int center_x, int center_y);

// Display SOC percentage with color gradient
void display_soc(float newSoC);

// Display power with directional bar graph
void display_power(int32_t current_power_w);
