#pragma once

#include <Arduino.h>

#include "ui/colors.h"

enum class LEDColor : uint8_t {
    RED = 0,
    GREEN = 1,
    ORANGE = 2,
    BLUE = 3,
    TEAL = 4,
};

enum class LEDEffect : uint8_t {
    CONTINUOUS = 0,
    FLASH = 1,
    HEARTBEAT = 2,
    PULSE = 3,
};

class LedWidget {
public:
    void set_color(LEDColor color) { color_ = color; }
    void set_effect(LEDEffect effect) { effect_ = effect; }

    bool is_on(uint32_t now_ms) const;
    uint16_t color565() const;
    uint16_t color565(uint32_t now_ms) const;

private:
    LEDColor color_ = LEDColor::GREEN;
    LEDEffect effect_ = LEDEffect::CONTINUOUS;
};
