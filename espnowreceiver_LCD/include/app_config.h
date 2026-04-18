#pragma once

#include <Arduino.h>

namespace AppConfig {
constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 480;

constexpr uint32_t STATE_UPDATE_MS = 2000;           // data/model updates every 2 seconds

constexpr int MAX_POWER_W = 5000;
constexpr int BAR_SEGMENTS_PER_SIDE = 30;
constexpr int BAR_SEGMENT_H = 18;
constexpr int BAR_EDGE_MARGIN = 0;                  // extend bar to full display width
constexpr int BAR_CENTER_GAP = 2;

constexpr int LED_RADIUS = 17;                      // ~2/3 of previous size (25)
constexpr int LED_MARGIN_RIGHT = 0;                 // flush to RHS edge

constexpr float SOC_MIN = 0.0f;
constexpr float SOC_MAX = 100.0f;
}  // namespace AppConfig
