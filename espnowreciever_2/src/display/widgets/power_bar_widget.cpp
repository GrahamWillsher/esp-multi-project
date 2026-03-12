/**
 * Power Bar Widget Implementation
 */

#include "power_bar_widget.h"
#include "../../common.h"
#include "../../helpers.h"
#include <algorithm>
#include <cmath>

// Forward declarations (from TFT_eSPI library)
extern const GFXfont FreeSansBold9pt7b;
extern TFT_eSPI tft;

namespace Display {
namespace Widgets {

namespace {
constexpr int BAR_DRAW_HEIGHT = 10;
constexpr int BAR_CLEAR_HEIGHT = 40;
constexpr int CENTER_MARKER_WIDTH = 1;
constexpr int ZERO_THRESHOLD_W = 10;
constexpr int PULSE_DELAY_MS = 30;
constexpr int TARGET_BAR_PIXEL_WIDTH = 8;
}

TFT_eSPI& PowerBarWidget::get_tft() {
    return ::tft;
}

void PowerBarWidget::compute_geometry_and_gradients() {
    if (max_bars_per_side_ > 0) {
        return;  // Already initialized
    }

    // Reserve the center marker column so dynamic bars never overlap it.
    const int side_pixels = std::min<int>(x_, Display::SCREEN_WIDTH - (x_ + CENTER_MARKER_WIDTH));
    int bars = side_pixels / TARGET_BAR_PIXEL_WIDTH;
    if (bars < 1) bars = 1;
    if (bars > 30) bars = 30;
    max_bars_per_side_ = bars;

    const int base = side_pixels / max_bars_per_side_;
    const int rem = side_pixels % max_bars_per_side_;

    bar_prefix_[0] = 0;
    for (int i = 0; i < max_bars_per_side_; ++i) {
        bar_widths_[i] = base + ((i < rem) ? 1 : 0);
        bar_prefix_[i + 1] = bar_prefix_[i] + bar_widths_[i];
    }

    if (max_bars_per_side_ > 1) {
        pre_calculate_color_gradient(TFT_BLUE, TFT_GREEN, max_bars_per_side_ - 1, gradient_green_);
        pre_calculate_color_gradient(TFT_BLUE, TFT_RED, max_bars_per_side_ - 1, gradient_red_);
    } else {
        gradient_green_[0] = TFT_GREEN;
        gradient_red_[0] = TFT_RED;
    }
}

void PowerBarWidget::render_power_text() {
    TFT_eSPI& tft = get_tft();
    if (last_displayed_power_text_ == current_power_) {
        return;
    }

    // Text positioned at bottom of screen using BC_DATUM (bottom-center)
    // This positions the BASELINE of the text at the y coordinate
    const int text_y = Display::SCREEN_HEIGHT - 2;  // 2px margin from bottom
    tft.fillRect(x_ - 60, text_y - 18, 120, 20, Display::tft_background);
    tft.setTextSize(1);
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextColor(TFT_WHITE, Display::tft_background);
    tft.setTextDatum(BC_DATUM);  // Bottom-center datum

    char powerStr[12];
    snprintf(powerStr, sizeof(powerStr), "%dW", current_power_);
    tft.drawString(powerStr, x_, text_y);

    last_displayed_power_text_ = current_power_;
}

void PowerBarWidget::draw_center_marker() {
    TFT_eSPI& tft = get_tft();
    tft.drawFastVLine(x_, y_ - (BAR_CLEAR_HEIGHT / 2), BAR_CLEAR_HEIGHT, TFT_BLUE);
}

void PowerBarWidget::get_bar_rect(int bar_index, bool negative, int& left, int& width) const {
    const int start = bar_prefix_[bar_index];
    const int end = bar_prefix_[bar_index + 1];
    width = end - start;

    if (negative) {
        // Left side: fill [x_ - end, x_ - start - 1]
        left = x_ - end;
    } else {
        // Right side: fill [x_ + CENTER_MARKER_WIDTH + start, x_ + CENTER_MARKER_WIDTH + end - 1]
        // This keeps the center marker as a dedicated, non-overlapping column.
        left = x_ + CENTER_MARKER_WIDTH + start;
    }
}

void PowerBarWidget::draw_dynamic_bar(int bar_index, bool negative, uint16_t color) {
    TFT_eSPI& tft = get_tft();
    int left = 0;
    int width = 0;
    get_bar_rect(bar_index, negative, left, width);

    tft.fillRect(left,
                 y_ - (BAR_DRAW_HEIGHT / 2),
                 width,
                 BAR_DRAW_HEIGHT,
                 color);
}

void PowerBarWidget::clear_dynamic_bar(int bar_index, bool negative) {
    TFT_eSPI& tft = get_tft();
    int left = 0;
    int width = 0;
    get_bar_rect(bar_index, negative, left, width);

    tft.fillRect(left,
                 y_ - (BAR_DRAW_HEIGHT / 2),
                 width,
                 BAR_DRAW_HEIGHT,
                 Display::tft_background);
}

void PowerBarWidget::animate_pulse(bool negative, int num_bars) {
    if (num_bars <= 0) {
        pulse_phase_ = -1;
        return;
    }

    const int previous_phase = pulse_phase_;
    pulse_phase_ = (pulse_phase_ + 1) % (num_bars + 1);

    if (previous_phase >= 0 && previous_phase < num_bars) {
        const uint16_t base_color = negative ? gradient_green_[previous_phase] : gradient_red_[previous_phase];
        draw_dynamic_bar(previous_phase, negative, base_color);
    }

    if (pulse_phase_ >= 0 && pulse_phase_ < num_bars) {
        const uint16_t base_color = negative ? gradient_green_[pulse_phase_] : gradient_red_[pulse_phase_];
        const uint16_t dimmed_color = (base_color >> 1) & 0x7BEF;
        draw_dynamic_bar(pulse_phase_, negative, dimmed_color);
        smart_delay(PULSE_DELAY_MS);
    }
}

void PowerBarWidget::render_bar() {
    // Initialize geometry + gradients once
    compute_geometry_and_gradients();
    
    // Clamp power to valid range
    int32_t clamped_power = current_power_;
    if (clamped_power < -max_power_) clamped_power = -max_power_;
    if (clamped_power > max_power_) clamped_power = max_power_;
    
    // Calculate target bars (how many bars to display)
    int bars = (abs(clamped_power) * max_bars_per_side_) / max_power_;
    if (bars > max_bars_per_side_) bars = max_bars_per_side_;
    
    // Ensure at least 1 bar for very small non-zero values
    if (clamped_power > 0 && bars == 0) bars = 1;
    else if (clamped_power < 0 && bars == 0) bars = 1;
    
    bool is_charging = (clamped_power < 0);
    int target_signed_bars = is_charging ? -bars : bars;
    bool is_zero = (clamped_power == 0 || abs(clamped_power) < ZERO_THRESHOLD_W);
    const int previous_abs = abs(previous_signed_bars_);
    const bool previous_negative = previous_signed_bars_ < 0;
    
    // Handle zero/near-zero special case
    if (is_zero) {
        if (previous_abs > 0) {
            for (int i = 0; i < previous_abs; ++i) {
                clear_dynamic_bar(i, previous_negative);
            }
        }

        draw_center_marker();
        previous_signed_bars_ = 0;
        pulse_phase_ = -1;
        render_power_text();
        return;
    }

    // Pulse/ripple effect when bar count and direction are unchanged
    bool same_direction = (previous_signed_bars_ < 0 && target_signed_bars < 0) ||
                          (previous_signed_bars_ > 0 && target_signed_bars > 0);
    bool should_pulse = same_direction && (abs(previous_signed_bars_) == abs(target_signed_bars)) && (bars > 0);

    if (should_pulse) {
        animate_pulse(target_signed_bars < 0, bars);
        draw_center_marker();
        previous_signed_bars_ = target_signed_bars;
        render_power_text();
        return;
    }

    pulse_phase_ = -1;
    
    // Clear prior side if direction changed
    if (previous_abs > 0 && previous_negative != is_charging) {
        for (int i = 0; i < previous_abs; ++i) {
            clear_dynamic_bar(i, previous_negative);
        }
    }

    // Clear only bars that are no longer needed when magnitude shrinks on same side.
    if (previous_negative == is_charging && bars < previous_abs) {
        for (int i = bars; i < previous_abs; ++i) {
            clear_dynamic_bar(i, is_charging);
        }
    }

    // Draw only bars newly required on the active side.
    const int draw_start = (previous_negative == is_charging && bars > previous_abs) ? previous_abs : 0;
    if (is_charging) {
        // Charging: draw bars on left side (blue→green gradient)
        for (int i = draw_start; i < bars; i++) {
            draw_dynamic_bar(i, true, gradient_green_[i]);
        }
    } else {
        // Discharging: draw bars on right side (blue→red gradient)
        for (int i = draw_start; i < bars; i++) {
            draw_dynamic_bar(i, false, gradient_red_[i]);
        }
    }

    draw_center_marker();
    
    previous_signed_bars_ = target_signed_bars;
    render_power_text();
}

void PowerBarWidget::update() {
    if (!needs_redraw()) {
        return;
    }
    
    if (!visible_) {
        clear_dirty();
        return;
    }
    
    render_bar();
    clear_dirty();
}

} // namespace Widgets
} // namespace Display
