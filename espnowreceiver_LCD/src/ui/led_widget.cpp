#include "ui/led_widget.h"

#include <cmath>

namespace {
uint16_t scale_rgb565(uint16_t c, float factor) {
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;

    uint8_t r = static_cast<uint8_t>((c >> 11) & 0x1F);
    uint8_t g = static_cast<uint8_t>((c >> 5) & 0x3F);
    uint8_t b = static_cast<uint8_t>(c & 0x1F);

    r = static_cast<uint8_t>(r * factor);
    g = static_cast<uint8_t>(g * factor);
    b = static_cast<uint8_t>(b * factor);

    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}
}  // namespace

bool LedWidget::is_on(uint32_t now_ms) const {
    switch (effect_) {
        case LEDEffect::CONTINUOUS:
            return true;

        case LEDEffect::FLASH: {
            constexpr uint32_t cycle_ms = 1000;  // 500 on, 500 off
            return (now_ms % cycle_ms) < 500;
        }

        case LEDEffect::HEARTBEAT: {
            constexpr uint32_t beat1_on = 120;
            constexpr uint32_t interbeat_off = 100;
            constexpr uint32_t beat2_on = 120;
            constexpr uint32_t pause_off = 760;
            constexpr uint32_t cycle_ms = beat1_on + interbeat_off + beat2_on + pause_off;

            const uint32_t p = now_ms % cycle_ms;
            if (p < beat1_on) return true;
            if (p < beat1_on + interbeat_off) return false;
            if (p < beat1_on + interbeat_off + beat2_on) return true;
            return false;
        }

        case LEDEffect::PULSE:
            return true;
    }

    return true;
}

uint16_t LedWidget::color565() const {
    switch (color_) {
        case LEDColor::RED:
            return UI::Colors::RED;
        case LEDColor::GREEN:
            return UI::Colors::GREEN;
        case LEDColor::ORANGE:
            return UI::Colors::ORANGE;
        case LEDColor::BLUE:
            return UI::Colors::BLUE;
        case LEDColor::TEAL:
            return UI::Colors::TEAL;
    }

    return UI::Colors::GREEN;
}

uint16_t LedWidget::color565(uint32_t now_ms) const {
    const uint16_t base = color565();
    if (effect_ != LEDEffect::PULSE) {
        return base;
    }

    // Smooth breathing pulse for "energy mode"
    constexpr float pulse_period_ms = 850.0f;
    constexpr float two_pi = 6.28318530718f;
    const float phase = (two_pi * static_cast<float>(now_ms % static_cast<uint32_t>(pulse_period_ms))) / pulse_period_ms;
    const float f = 0.35f + (0.65f * (0.5f + 0.5f * sinf(phase)));
    return scale_rgb565(base, f);
}
