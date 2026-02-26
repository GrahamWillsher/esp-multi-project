#pragma once

/**
 * TFT_eSPI User Setup for LilyGo T-Display-S3
 * 
 * This file configures the TFT_eSPI library with hardware pin assignments.
 * It replaces the need for build flags in platformio.ini.
 * 
 * NOTE: Pin values are duplicated from hardware_config.h as literals because
 * TFT_eSPI's preprocessor directives require compile-time constants, not C++
 * namespace constants. See hardware_config.h for authoritative GPIO definitions.
 * 
 * DO NOT include hardware_config.h here - it contains C++ code incompatible
 * with C compilation (TFT_eSPI processes some C files).
 */

// ═══════════════════════════════════════════════════════════════════════
// Driver Selection
// ═══════════════════════════════════════════════════════════════════════

#define USER_SETUP_LOADED 1         // Prevent library from using default setup
#define ST7789_DRIVER 1             // ST7789 display controller

// ═══════════════════════════════════════════════════════════════════════
// Display Configuration
// ═══════════════════════════════════════════════════════════════════════

#define TFT_PARALLEL_8_BIT 1        // 8-bit parallel interface (not SPI)
#define TFT_RGB_ORDER TFT_BGR       // RGB order: BGR (swap red and blue)
#define TFT_INVERSION_ON 1          // Enable display inversion

// Display dimensions (physical, before rotation)
// NOTE: These must be literals for preprocessor compatibility
#define TFT_WIDTH  170  // HardwareConfig::DISPLAY_PHYSICAL_WIDTH
#define TFT_HEIGHT 320  // HardwareConfig::DISPLAY_PHYSICAL_HEIGHT

// ═══════════════════════════════════════════════════════════════════════
// GPIO Pin Configuration
// ═══════════════════════════════════════════════════════════════════════
// NOTE: Preprocessor macros require literal values, not namespace constants.
// Values duplicated from hardware_config.h for TFT_eSPI compatibility.

// Control signals (low GPIOs: 5-9)
#define TFT_DC     7   // Data/Command    (HardwareConfig::GPIO_TFT_DC)
#define TFT_RST    5   // Reset          (HardwareConfig::GPIO_TFT_RST)
#define TFT_CS     6   // Chip Select    (HardwareConfig::GPIO_TFT_CS)
#define TFT_WR     8   // Write strobe   (HardwareConfig::GPIO_TFT_WR)
#define TFT_RD     9   // Read strobe    (HardwareConfig::GPIO_TFT_RD)

// 8-bit parallel data bus (high GPIOs: 39-48)
#define TFT_D0     39  // HardwareConfig::GPIO_TFT_D0
#define TFT_D1     40  // HardwareConfig::GPIO_TFT_D1
#define TFT_D2     41  // HardwareConfig::GPIO_TFT_D2
#define TFT_D3     42  // HardwareConfig::GPIO_TFT_D3
#define TFT_D4     45  // HardwareConfig::GPIO_TFT_D4
#define TFT_D5     46  // HardwareConfig::GPIO_TFT_D5
#define TFT_D6     47  // HardwareConfig::GPIO_TFT_D6
#define TFT_D7     48  // HardwareConfig::GPIO_TFT_D7

// Backlight control (managed separately by application, not TFT_eSPI)
#define TFT_BL     38  // HardwareConfig::GPIO_BACKLIGHT
#define TFT_BACKLIGHT_ON HIGH  // Backlight active state

// ═══════════════════════════════════════════════════════════════════════
// Font Configuration
// ═══════════════════════════════════════════════════════════════════════

#define LOAD_GLCD   1               // Font 1: Original Adafruit 8 pixel font
#define LOAD_FONT2  1               // Font 2: Small 16 pixel high font
#define LOAD_FONT4  1               // Font 4: Medium 26 pixel high font
#define LOAD_FONT6  1               // Font 6: Large 48 pixel font
#define LOAD_FONT7  1               // Font 7: 7-segment 48 pixel font
#define LOAD_FONT8  1               // Font 8: Large 75 pixel font
#define LOAD_GFXFF  1               // FreeFonts
#define SMOOTH_FONT 1               // Enable anti-aliased fonts

// ═══════════════════════════════════════════════════════════════════════
// Performance & Debug
// ═══════════════════════════════════════════════════════════════════════

// SPI frequency not applicable for parallel interface
// #define SPI_FREQUENCY  27000000  // Not used (parallel interface)
