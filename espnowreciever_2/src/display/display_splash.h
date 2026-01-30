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

// Display splash screen with fade effect
void displaySplashWithFade();
