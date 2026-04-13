#pragma once

#include <Arduino.h>

namespace AppConfig {
constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 480;

constexpr uint32_t FRAME_INTERVAL_MS = 120;          // UI animation refresh (pulse)
constexpr uint32_t STATE_UPDATE_MS = 2000;           // data/model updates every 2 seconds
constexpr uint32_t LED_MODE_ROTATE_MS = 9000;       // rotate LED mode every 9s
constexpr uint32_t LED_COLOR_ROTATE_MS = 12000;     // rotate LED color every 12s

constexpr int MAX_POWER_W = 5000;
constexpr int BAR_SEGMENTS_PER_SIDE = 30;
constexpr int BAR_SEGMENT_W = 14;
constexpr int BAR_SEGMENT_H = 18;
constexpr int BAR_SEGMENT_GAP = 4;
constexpr int BAR_EDGE_MARGIN = 0;                  // extend bar to full display width
constexpr int BAR_CENTER_GAP = 2;

constexpr int LED_RADIUS = 17;                      // ~2/3 of previous size (25)
constexpr int LED_MARGIN_RIGHT = 20;

constexpr float SOC_MIN = 0.0f;
constexpr float SOC_MAX = 100.0f;
}  // namespace AppConfig
