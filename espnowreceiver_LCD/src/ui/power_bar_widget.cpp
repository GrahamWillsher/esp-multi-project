#include "ui/power_bar_widget.h"

#include <algorithm>
#include <cstdlib>

uint16_t PowerBarWidget::gradient_blue_to_green(uint8_t index, uint8_t max) const {
    if (max == 0) return UI::Colors::GREEN;
    const float t = static_cast<float>(index) / static_cast<float>(max);
    const uint8_t r = 0;
    const uint8_t g = static_cast<uint8_t>(255.0f * t);
    const uint8_t b = static_cast<uint8_t>(255.0f * (1.0f - t));
    return UI::rgb565(r, g, b);
}

uint16_t PowerBarWidget::gradient_blue_to_red(uint8_t index, uint8_t max) const {
    if (max == 0) return UI::Colors::RED;
    const float t = static_cast<float>(index) / static_cast<float>(max);
    const uint8_t r = static_cast<uint8_t>(255.0f * t);
    const uint8_t g = 0;
    const uint8_t b = static_cast<uint8_t>(255.0f * (1.0f - t));
    return UI::rgb565(r, g, b);
}

uint16_t PowerBarWidget::dim_rgb565(uint16_t c) const {
    // Same dimming trick used in receiver_2 TFT implementation.
    return static_cast<uint16_t>((c >> 1) & 0x7BEF);
}

void PowerBarWidget::get_bar_rect(int bar_index,
                                  int center_x,
                                  bool negative,
                                  int seg_count,
                                  int left_pitch,
                                  int right_pitch,
                                  int center_gap,
                                  int& left,
                                  int& width) const {
    if (negative) {
        width = std::max(1, left_pitch - 1);
        left = center_x - center_gap - ((bar_index + 1) * left_pitch) + 1;
    } else {
        width = std::max(1, right_pitch - 1);
        left = center_x + center_gap + (bar_index * right_pitch);
    }

    (void)seg_count;
}

void PowerBarWidget::draw_bar(int bar_index,
                              lgfx::LGFX_Device& display,
                              int center_x,
                              int center_y,
                              bool negative,
                              int seg_count,
                              int left_pitch,
                              int right_pitch,
                              int center_gap,
                              int seg_h,
                              uint16_t color) const {
    int left = 0;
    int width = 0;
    get_bar_rect(bar_index, center_x, negative, seg_count, left_pitch, right_pitch, center_gap, left, width);
    display.fillRect(left, center_y - (seg_h / 2), width, seg_h, color);
}

void PowerBarWidget::clear_bar(int bar_index,
                               lgfx::LGFX_Device& display,
                               int center_x,
                               int center_y,
                               bool negative,
                               int seg_count,
                               int left_pitch,
                               int right_pitch,
                               int center_gap,
                               int seg_h) const {
    draw_bar(bar_index, display, center_x, center_y, negative, seg_count, left_pitch, right_pitch, center_gap, seg_h, UI::Colors::BLACK);
}

void PowerBarWidget::draw(lgfx::LGFX_Device& display, int center_x, int center_y, int32_t power_w, uint32_t now_ms) {
    const int seg_count = AppConfig::BAR_SEGMENTS_PER_SIDE;
    const int seg_h = AppConfig::BAR_SEGMENT_H;
    const int edge_margin = AppConfig::BAR_EDGE_MARGIN;
    const int center_gap = AppConfig::BAR_CENTER_GAP;
    const int left_space = std::max(1, center_x - center_gap - edge_margin);
    const int right_space = std::max(1, (AppConfig::SCREEN_WIDTH - edge_margin) - (center_x + center_gap));
    const int left_pitch = std::max(2, left_space / seg_count);
    const int right_pitch = std::max(2, right_space / seg_count);

    const int32_t clamped = std::max(-AppConfig::MAX_POWER_W, std::min(AppConfig::MAX_POWER_W, power_w));
    const int32_t abs_power = std::abs(clamped);
    const bool is_charging = (clamped < 0);
    const bool is_zero = (abs_power < 10);  // receiver_2 threshold style
    int active_segments = (clamped == 0) ? 0 : static_cast<int>((static_cast<int64_t>(abs_power) * seg_count) / AppConfig::MAX_POWER_W);
    if (!is_zero && active_segments == 0) {
        active_segments = 1;
    }
    if (active_segments > seg_count) {
        active_segments = seg_count;
    }

    const bool new_value = !has_last_state_ || (last_displayed_power_text_ != power_w);
    const bool same_direction = has_last_state_ && !is_zero && !last_was_zero_ && (last_was_charging_ == is_charging);

    // Trigger a new ripple only when a fresh value arrives with same direction and same bar count.
    if (new_value && same_direction && !is_zero && (active_segments == last_bar_count_) && (active_segments > 0)) {
        ripple_active_ = true;
        ripple_start_ms_ = now_ms;
    }
    // Cancel any in-progress ripple if direction changed or value went to zero.
    if (is_zero || (has_last_state_ && !same_direction && !is_zero)) {
        ripple_active_ = false;
    }

    if (is_zero) {
        // Redraw center marker only on transition to zero.
        if (new_value || !has_last_state_) {
            display.fillRect(0, center_y - seg_h, AppConfig::SCREEN_WIDTH, (seg_h * 2) + 8, UI::Colors::BLACK);
            const int marker_w = std::max(1, std::max(left_pitch, right_pitch) - 1);
            display.fillRect(center_x - (marker_w / 2), center_y - (seg_h / 2), marker_w, seg_h, UI::Colors::BLUE);
        }
    } else if (ripple_active_) {
        const uint32_t elapsed = now_ms - ripple_start_ms_;
        if (elapsed >= 1000) {
            // Ripple complete: draw static bars once and stop.
            ripple_active_ = false;
            display.fillRect(center_x, center_y - seg_h, 1, seg_h * 2, UI::Colors::BLACK);
            for (int i = 0; i < active_segments; ++i) {
                const uint16_t c = is_charging ? gradient_blue_to_green(i, seg_count - 1)
                                               : gradient_blue_to_red(i, seg_count - 1);
                draw_bar(i, display, center_x, center_y, is_charging, seg_count, left_pitch, right_pitch, center_gap, seg_h, c);
            }
        } else {
            // Animate: one dimmed bar advances across all segments over 1s.
            const int ripple_idx = static_cast<int>((static_cast<uint64_t>(elapsed) * active_segments) / 1000);
            display.fillRect(center_x, center_y - seg_h, 1, seg_h * 2, UI::Colors::BLACK);
            for (int i = 0; i < active_segments; ++i) {
                const uint16_t base = is_charging ? gradient_blue_to_green(i, seg_count - 1)
                                                  : gradient_blue_to_red(i, seg_count - 1);
                const uint16_t c = (i == ripple_idx) ? dim_rgb565(base) : base;
                draw_bar(i, display, center_x, center_y, is_charging, seg_count, left_pitch, right_pitch, center_gap, seg_h, c);
            }
        }
    } else if (new_value) {
        // Bar count or direction changed (no ripple): redraw bars.
        const bool direction_changed = has_last_state_ && !last_was_zero_ && (last_was_charging_ != is_charging);
        if (direction_changed) {
            for (int i = 0; i < last_bar_count_; ++i) {
                clear_bar(i, display, center_x, center_y, last_was_charging_, seg_count, left_pitch, right_pitch, center_gap, seg_h);
            }
        } else if (has_last_state_ && !last_was_zero_ && active_segments < last_bar_count_) {
            for (int i = active_segments; i < last_bar_count_; ++i) {
                clear_bar(i, display, center_x, center_y, is_charging, seg_count, left_pitch, right_pitch, center_gap, seg_h);
            }
        }
        for (int i = 0; i < active_segments; ++i) {
            const uint16_t c = is_charging ? gradient_blue_to_green(i, seg_count - 1)
                                           : gradient_blue_to_red(i, seg_count - 1);
            draw_bar(i, display, center_x, center_y, is_charging, seg_count, left_pitch, right_pitch, center_gap, seg_h, c);
        }
        display.fillRect(center_x, center_y - seg_h, 1, seg_h * 2, UI::Colors::BLACK);
    }
    // else: same value, not rippling — bars unchanged, nothing to draw.

    // Redraw text only when value changes; bar count changes with same source cadence.
    if (last_displayed_power_text_ != power_w) {
        char power_num_text[16];
        snprintf(power_num_text, sizeof(power_num_text), "%ld", static_cast<long>(power_w));
        const int text_y = (AppConfig::SCREEN_HEIGHT * 88) / 100;
        display.fillRect(center_x - 220, text_y - 30, 440, 60, UI::Colors::BLACK);

        // Power text: font4 size 1 for both value and unit suffix.
        display.setTextFont(4);
        display.setTextSize(1);
        display.setTextDatum(middle_left);
        const int num_w = display.textWidth(power_num_text);
        const int x0 = center_x - (num_w / 2);

        display.setTextColor(UI::Colors::WHITE, UI::Colors::BLACK);
        display.drawString(power_num_text, x0, text_y);
        display.drawString("W", x0 + num_w + 4, text_y);

        last_displayed_power_text_ = power_w;
    }

    has_last_state_ = true;
    last_bar_count_ = active_segments;
    last_was_charging_ = is_charging;
    last_was_zero_ = is_zero;
}
