#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

// LED color enumeration (used by display_led.h and ESPNow state)
// Wire format: 0=red, 1=green, 2=orange (matches enum values)
enum LEDColor {
    LED_RED    = 0,
    LED_GREEN  = 1,
    LED_ORANGE = 2
};

// LED effect modes for simulated/status indicator
enum LEDEffect {
    LED_EFFECT_SOLID = 0,
    LED_EFFECT_FLASH = 1,
    LED_EFFECT_HEARTBEAT = 2
};

// Actual TFT RGB565 color values for LED display
namespace LEDColors {
    constexpr uint16_t RED    = TFT_RED;     // 0xF800
    constexpr uint16_t GREEN  = TFT_GREEN;   // 0x07E0
    constexpr uint16_t ORANGE = TFT_ORANGE;  // 0xFD20
}
