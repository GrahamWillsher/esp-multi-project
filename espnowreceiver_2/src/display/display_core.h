#pragma once

// Legacy compatibility header.
// Active application code should prefer `display.h`, which is the canonical
// backend-agnostic public API used by the compile-time display selection path.
// This header remains only for older compatibility code and alternate builds.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "../common.h"

// ════════════════════════════════════════════════════════════════════════════
// Display Hardware Abstraction Layer (HAL) Functions
// ════════════════════════════════════════════════════════════════════════════

// Initialize TFT display hardware via DisplayManager (HAL abstraction)
// This replaces legacy init_display() and centralizes hardware setup
void init_display();

// Get reference to global TFT_eSPI instance (for TFT library-specific operations)
// Generally prefer DisplayManager::get_driver() for portable code
TFT_eSPI& get_tft_hardware();

// ════════════════════════════════════════════════════════════════════════════
// Display Content Functions
// ════════════════════════════════════════════════════════════════════════════

// Display initial ready screen with backlight fade-in
void displayInitialScreen();

// Display a proportional font number with centered digits
void display_centered_proportional_number(const GFXfont* font, float number, uint16_t color, int center_x, int center_y);

// Display SOC percentage with color gradient
void display_soc(float newSoC);

// Display power with directional bar graph
void display_power(int32_t current_power_w);

// Show the main status page (switches active LVGL screen)
void show_status_page();
