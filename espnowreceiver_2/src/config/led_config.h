#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

// LED color enumeration (used by display_led.h and ESPNow state)
// Wire format: 0=red, 1=green, 2=orange, 3=blue (matches enum values)
enum LEDColor {
    LED_RED    = 0,
    LED_GREEN  = 1,
    LED_ORANGE = 2,
    LED_BLUE   = 3
};

// LED effect modes for simulated/status indicator
enum LEDEffect {
    LED_EFFECT_CONTINUOUS = 0,
    LED_EFFECT_FLASH = 1,
    LED_EFFECT_HEARTBEAT = 2
};

struct LedPatternTimingConfig {
    struct {
        uint32_t on_ms;
        uint32_t off_ms;
    } flash;

    struct {
        uint32_t beat1_on_ms;
        uint32_t interbeat_off_ms;
        uint32_t beat2_on_ms;
        uint32_t pause_off_ms;
    } heartbeat;
};

namespace LedPatternTiming {
    // Profile 1 (balanced): 120 / 100 / 120 / 760 ms
    constexpr LedPatternTimingConfig kConfig{
        .flash{500, 500},
        .heartbeat{120, 100, 120, 760}
    };
}

// Backward compatibility alias
constexpr LEDEffect LED_EFFECT_SOLID = LED_EFFECT_CONTINUOUS;

// Actual TFT RGB565 color values for LED display
namespace LEDColors {
    constexpr uint16_t RED    = TFT_RED;     // 0xF800
    constexpr uint16_t GREEN  = TFT_GREEN;   // 0x07E0
    constexpr uint16_t ORANGE = TFT_ORANGE;  // 0xFD20
    constexpr uint16_t BLUE   = TFT_BLUE;    // 0x001F
}
