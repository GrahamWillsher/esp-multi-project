#pragma once

/**
 * LilyGo T-Display-S3 Hardware Configuration
 * Single source of truth for all GPIO assignments and display configuration
 * 
 * This file centralizes all hardware-specific definitions to:
 * - Eliminate duplicate GPIO definitions across the codebase
 * - Enable compile-time pin conflict detection
 * - Simplify board variant support
 * - Provide clear hardware documentation
 */

#include <stdint.h>

namespace HardwareConfig {
    
    // ═══════════════════════════════════════════════════════════════════════
    // GPIO Pin Assignments - LilyGo T-Display-S3
    // ═══════════════════════════════════════════════════════════════════════
    
    // Display power and backlight control
    constexpr uint8_t GPIO_DISPLAY_POWER = 15;  // Display power enable (Active HIGH)
    constexpr uint8_t GPIO_BACKLIGHT = 38;      // Backlight PWM control
    
    // TFT_eSPI control pins (ST7789 controller)
    constexpr uint8_t GPIO_TFT_DC = 7;          // Data/Command select
    constexpr uint8_t GPIO_TFT_RST = 5;         // Reset (Active LOW)
    constexpr uint8_t GPIO_TFT_CS = 6;          // Chip Select (Active LOW)
    constexpr uint8_t GPIO_TFT_WR = 8;          // Write strobe (Active LOW)
    constexpr uint8_t GPIO_TFT_RD = 9;          // Read strobe (Active LOW)
    
    // TFT_eSPI 8-bit parallel data bus
    constexpr uint8_t GPIO_TFT_D0 = 39;
    constexpr uint8_t GPIO_TFT_D1 = 40;
    constexpr uint8_t GPIO_TFT_D2 = 41;
    constexpr uint8_t GPIO_TFT_D3 = 42;
    constexpr uint8_t GPIO_TFT_D4 = 45;
    constexpr uint8_t GPIO_TFT_D5 = 46;
    constexpr uint8_t GPIO_TFT_D6 = 47;
    constexpr uint8_t GPIO_TFT_D7 = 48;
    
    // ═══════════════════════════════════════════════════════════════════════
    // Display Specifications - ST7789 Controller
    // ═══════════════════════════════════════════════════════════════════════
    
    // Physical display dimensions (native orientation)
    constexpr uint16_t DISPLAY_PHYSICAL_WIDTH = 170;   // Physical width in portrait
    constexpr uint16_t DISPLAY_PHYSICAL_HEIGHT = 320;  // Physical height in portrait
    
    // Logical display dimensions (after rotation)
    constexpr uint16_t DISPLAY_WIDTH = 320;   // Width in landscape (rotation=1)
    constexpr uint16_t DISPLAY_HEIGHT = 170;  // Height in landscape (rotation=1)
    constexpr uint8_t DISPLAY_ROTATION = 1;   // Landscape mode
    
    // ═══════════════════════════════════════════════════════════════════════
    // Backlight PWM Configuration
    // ═══════════════════════════════════════════════════════════════════════
    
    constexpr uint16_t BACKLIGHT_FREQUENCY_HZ = 2000;     // PWM frequency
    constexpr uint8_t BACKLIGHT_RESOLUTION_BITS = 8;      // 8-bit resolution (0-255)
    constexpr uint8_t BACKLIGHT_PWM_CHANNEL = 0;          // LEDC channel
    
    // ═══════════════════════════════════════════════════════════════════════
    // Compile-Time Pin Conflict Validation
    // ═══════════════════════════════════════════════════════════════════════
    
    // Ensure critical pins don't overlap
    static_assert(GPIO_DISPLAY_POWER != GPIO_BACKLIGHT, 
                  "GPIO conflict: Display power and backlight cannot use same pin");
    
    static_assert(GPIO_TFT_DC != GPIO_TFT_RST && 
                  GPIO_TFT_DC != GPIO_TFT_CS && 
                  GPIO_TFT_DC != GPIO_TFT_WR && 
                  GPIO_TFT_DC != GPIO_TFT_RD,
                  "GPIO conflict: TFT control pins must be unique");
    
    static_assert(GPIO_TFT_D0 != GPIO_TFT_D1 && 
                  GPIO_TFT_D0 != GPIO_TFT_D2 && 
                  GPIO_TFT_D0 != GPIO_TFT_D3 && 
                  GPIO_TFT_D0 != GPIO_TFT_D4 && 
                  GPIO_TFT_D0 != GPIO_TFT_D5 && 
                  GPIO_TFT_D0 != GPIO_TFT_D6 && 
                  GPIO_TFT_D0 != GPIO_TFT_D7,
                  "GPIO conflict: TFT data bus pins must be unique");
    
    // Validate data bus pins are in reasonable GPIO range for ESP32-S3
    static_assert(GPIO_TFT_D0 >= 0 && GPIO_TFT_D7 <= 48,
                  "TFT data bus pins must be within valid ESP32-S3 GPIO range");
    
    // ═══════════════════════════════════════════════════════════════════════
    // Board Information
    // ═══════════════════════════════════════════════════════════════════════
    
    constexpr const char* BOARD_NAME = "LilyGo T-Display-S3";
    constexpr const char* DISPLAY_CONTROLLER = "ST7789";
    constexpr const char* DISPLAY_INTERFACE = "8-bit Parallel";
    
} // namespace HardwareConfig
