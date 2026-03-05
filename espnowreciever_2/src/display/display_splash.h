#pragma once

#include <Arduino.h>
#include "../common.h"

// Splash screen functions

// Display JPEG file from LittleFS
void displaySplashJpeg2(const char* filename);

// Display splash screen content
void displaySplashScreenContent();

// Fade backlight to target brightness
void fadeBacklight(uint8_t targetBrightness, uint32_t durationMs);

/**
 * Display splash screen with fade effect
 * 
 * This is a DISPATCHER function that automatically calls the correct
 * implementation based on the active display backend:
 * - USE_LVGL: calls display_splash_lvgl() (LVGL-based splash)
 * - USE_TFT: calls TFT splash implementation (direct TFT rendering)
 * 
 * This allows littlefs_init.cpp and other code to use a single unified
 * API regardless of which display backend is compiled in.
 */
void displaySplashWithFade();
