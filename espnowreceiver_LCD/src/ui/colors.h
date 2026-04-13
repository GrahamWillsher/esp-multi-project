#pragma once

#include <cstdint>

namespace UI {
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace Colors {
constexpr uint16_t BLACK = rgb565(0, 0, 0);
constexpr uint16_t WHITE = rgb565(255, 255, 255);
constexpr uint16_t BLUE = rgb565(0, 121, 255);
constexpr uint16_t RED = rgb565(255, 0, 0);
constexpr uint16_t GREEN = rgb565(0, 255, 0);
constexpr uint16_t ORANGE = rgb565(255, 140, 0);
constexpr uint16_t TEAL = rgb565(0, 200, 200);
constexpr uint16_t DIM_GRAY = rgb565(35, 35, 35);
}  // namespace Colors
}  // namespace UI
