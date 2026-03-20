#pragma once

#include "../../common.h"

namespace Display {
namespace LayoutSpec {

// -----------------------------------------------------------------------------
// Future-LVGL-aware shared display tokens
// -----------------------------------------------------------------------------
// These constants are backend-agnostic and intentionally renderer-neutral.
// TFT consumes them now; future renderers can reuse the same semantic contract.

namespace Timing {
constexpr uint32_t SPLASH_FADE_IN_MS = 3000;
constexpr uint32_t SPLASH_HOLD_MS = 2000;
constexpr uint32_t SPLASH_FADE_OUT_MS = 3000;
constexpr uint32_t READY_FADE_IN_MS = 300;
constexpr uint32_t ANIMATION_FRAME_TIME_MS = 16;  // ~60 FPS
constexpr uint32_t POWER_BAR_PULSE_DELAY_MS = 30;

constexpr uint32_t BACKLIGHT_INIT_DELAY_MS = 5;
constexpr uint32_t PANEL_POWER_ENABLE_DELAY_MS = 100;
}  // namespace Timing

namespace Backlight {
constexpr float GAMMA = 2.0f;
constexpr uint8_t PWM_MIN = 0;
constexpr uint8_t PWM_MAX = 255;
}  // namespace Backlight

namespace Layout {
constexpr int TOP_REGION_NUM = 2;
constexpr int TOP_REGION_DEN = 3;

constexpr int TOP_REGION_HEIGHT = (SCREEN_HEIGHT * TOP_REGION_NUM) / TOP_REGION_DEN;
constexpr int BOTTOM_REGION_HEIGHT = SCREEN_HEIGHT - TOP_REGION_HEIGHT;

// Ready screen
constexpr int READY_TEXT_X = SCREEN_WIDTH / 2;
constexpr int READY_TEXT_Y = SCREEN_HEIGHT / 2;

// SOC area
constexpr int SOC_CENTER_X = SCREEN_WIDTH / 2;
constexpr int SOC_CENTER_Y = TOP_REGION_HEIGHT / 2;
constexpr int SOC_CLEAR_LEFT = 20;
constexpr int SOC_CLEAR_TOP = 10;
// Keep extra right-side margin so SOC clear area does not clip LED indicator.
constexpr int SOC_CLEAR_WIDTH = SCREEN_WIDTH - 44;
constexpr int SOC_CLEAR_HEIGHT = TOP_REGION_HEIGHT - 20;

// Error display
constexpr int ERROR_TEXT_X = 130;
constexpr int ERROR_TEXT_Y = 75;
constexpr int FATAL_ERROR_TEXT_X = 90;
constexpr int FATAL_ERROR_TITLE_Y = 20;
constexpr int FATAL_ERROR_COMPONENT_Y = 60;
constexpr int FATAL_ERROR_MESSAGE_Y = 100;
constexpr int FATAL_ERROR_LEFT_MARGIN = 20;
}  // namespace Layout

namespace PowerBar {
constexpr int32_t MAX_POWER_W = MAX_POWER;

// ── 21-segment rectangular bar geometry ─────────────────────────────────────
// Total = 21 segments (10 left + 1 centre + 10 right), 2 px gap, full width.
// Maths: W=320, N=21, G=2  →  SEG_W = floor((320 - 20*2) / 21) = 13
//        Used = 21*13 + 20*2 = 313  →  left margin=3, right margin=4
constexpr int SEGMENTS_TOTAL          = 21;
constexpr int SEGMENTS_PER_SIDE       = 10;
constexpr int CENTER_SEGMENT_INDEX    = 10;
constexpr int SEGMENT_GAP_PX          = 2;
constexpr int SEGMENT_W_PX            = 13;   // derived above
constexpr int SEGMENT_H_PX            = 10;
constexpr int SEGMENT_MARGIN_LEFT_PX  = 3;    // derived above
constexpr int SEGMENT_Y_PX            = 115;  // BAR_Y(120) - SEGMENT_H_PX/2

/// X pixel origin of segment index i (0-based, left edge of rect).
constexpr int segment_x(int index) {
    return SEGMENT_MARGIN_LEFT_PX + index * (SEGMENT_W_PX + SEGMENT_GAP_PX);
}

// ── Power-text geometry ───────────────────────────────────────────────────────
constexpr int TEXT_Y               = SCREEN_HEIGHT - 2;
constexpr int TEXT_BOX_LEFT_OFFSET = 70;
constexpr int TEXT_BOX_TOP_OFFSET  = 20;
constexpr int TEXT_BOX_WIDTH       = 140;
constexpr int TEXT_BOX_HEIGHT      = 22;
}  // namespace PowerBar

namespace Text {
constexpr int READY_TEXT_SIZE = 2;
constexpr int SOC_TEXT_SIZE = 2;
constexpr int POWER_BAR_TEXT_SIZE = 2;
constexpr int POWER_VALUE_TEXT_SIZE = 1;
}  // namespace Text

namespace Assets {
constexpr const char* SPLASH_IMAGE_PATH = "/BatteryEmulator4_320x170.jpg";
constexpr const char* SPLASH_FALLBACK_TEXT = "Battery System";
constexpr size_t MAX_SPLASH_JPEG_BYTES = 512 * 1024;
}  // namespace Assets

}  // namespace LayoutSpec
}  // namespace Display
