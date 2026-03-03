/**
 * Power Bar Widget Implementation
 */

#include "power_bar_widget.h"
#include "../../common.h"
#include "../../helpers.h"
#include <cmath>

// Forward declarations (from TFT_eSPI library)
extern const GFXfont FreeSansBold9pt7b;
extern const GFXfont FreeSansBold12pt7b;
extern TFT_eSPI tft;

namespace Display {
namespace Widgets {

TFT_eSPI& PowerBarWidget::get_tft() {
    return ::tft;
}

void PowerBarWidget::init_gradients() {
    if (bar_char_width_ > 0) {
        return;  // Already initialized
    }
    
    TFT_eSPI& tft = get_tft();
    
    // Calculate bar character width once (using text size 2 for thicker bars)
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextSize(2);
    bar_char_width_ = tft.textWidth("-");
    
    // Calculate max bars per side (fit within half the screen)
    max_bars_per_side_ = (Display::SCREEN_WIDTH / 2) / bar_char_width_;
    if (max_bars_per_side_ > 30) max_bars_per_side_ = 30;
    
    // Generate blue→green gradient (charging, left side)
    // This goes from BLUE (neutral) to GREEN (charging)
    pre_calculate_color_gradient(TFT_BLUE, TFT_GREEN, max_bars_per_side_ - 1, gradient_green_);
    
    // Generate blue→red gradient (discharging, right side)
    // This goes from BLUE (neutral) to RED (discharging)
    pre_calculate_color_gradient(TFT_BLUE, TFT_RED, max_bars_per_side_ - 1, gradient_red_);
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

void PowerBarWidget::render_bar() {
    TFT_eSPI& tft = get_tft();
    
    // Initialize gradients if needed
    init_gradients();
    
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextSize(2);  // Increased from 1 to 2 for thicker bars
    tft.setTextDatum(MC_DATUM);
    
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
    bool is_zero = (clamped_power == 0 || abs(clamped_power) < 10);

    auto drawBar = [&](int barIndex, bool negative, uint16_t color) {
        int bar_x = negative ? (x_ - (barIndex + 1) * bar_char_width_) : (x_ + barIndex * bar_char_width_);
        tft.setTextColor(color, Display::tft_background);
        tft.drawString("-", bar_x, y_);
    };
    
    // Handle zero/near-zero special case
    if (is_zero) {
        if (!last_was_zero_) {
            // Clear entire bar area (increased height for size 2 text)
            tft.fillRect(x_ - (max_bars_per_side_ * bar_char_width_), 
                        y_ - 20,
                        max_bars_per_side_ * 2 * bar_char_width_, 
                        40,
                        Display::tft_background);
            previous_signed_bars_ = 0;
            last_was_zero_ = true;

            // Draw neutral center marker so power area never appears blank
            tft.setTextColor(TFT_BLUE, Display::tft_background);
            tft.drawString("-", x_, y_);
        }
        render_power_text();
        last_rendered_power_ = current_power_;
        return;
    }
    
    last_was_zero_ = false;

    // Pulse/ripple effect when bar count and direction are unchanged
    bool same_direction = (previous_signed_bars_ < 0 && target_signed_bars < 0) ||
                          (previous_signed_bars_ > 0 && target_signed_bars > 0);
    bool should_pulse = same_direction && (abs(previous_signed_bars_) == abs(target_signed_bars)) && (bars > 0);

    if (should_pulse) {
        bool negative = (target_signed_bars < 0);
        const int numBars = abs(target_signed_bars);
        const int DELAY_PER_BAR_MS = 30;

        for (int ripplePos = 0; ripplePos <= numBars; ripplePos++) {
            for (int i = 0; i < numBars; i++) {
                uint16_t barColor = negative ? gradient_green_[i] : gradient_red_[i];
                uint16_t displayColor = (i == ripplePos && ripplePos < numBars)
                    ? ((barColor >> 1) & 0x7BEF)  // Dimmed for ripple
                    : barColor;
                drawBar(i, negative, displayColor);
            }

            if (ripplePos < numBars) {
                smart_delay(DELAY_PER_BAR_MS);
            }
        }

        previous_signed_bars_ = target_signed_bars;
        last_rendered_power_ = current_power_;
        render_power_text();
        return;
    }
    
    // Draw bars
    if (is_charging) {
        // Charging: draw bars on left side (blue→green gradient)
        for (int i = 0; i < bars; i++) {
            drawBar(i, true, gradient_green_[i]);
        }

        int previous_abs = abs(previous_signed_bars_);
        bool previous_negative = previous_signed_bars_ < 0;
        
        // Clear right side if previously discharging
        if (previous_abs > 0 && !previous_negative) {
            tft.fillRect(x_, 
                        y_ - 20,
                        previous_abs * bar_char_width_, 
                        40,
                        Display::tft_background);
        }
        
        // Clear extra bars on left if power decreased
        if (previous_negative && bars < previous_abs) {
            tft.fillRect(x_ - previous_abs * bar_char_width_,
                        y_ - 20,
                        (previous_abs - bars) * bar_char_width_,
                        40,
                        Display::tft_background);
        }
    } else {
        // Discharging: draw bars on right side (blue→red gradient)
        for (int i = 0; i < bars; i++) {
            drawBar(i, false, gradient_red_[i]);
        }

        int previous_abs = abs(previous_signed_bars_);
        bool previous_negative = previous_signed_bars_ < 0;
        
        // Clear left side if previously charging
        if (previous_abs > 0 && previous_negative) {
            tft.fillRect(x_ - previous_abs * bar_char_width_,
                        y_ - 20,
                        previous_abs * bar_char_width_,
                        40,
                        Display::tft_background);
        }
        
        // Clear extra bars on right if power decreased
        if (!previous_negative && bars < previous_abs) {
            tft.fillRect(x_ + bars * bar_char_width_,
                        y_ - 20,
                        (previous_abs - bars) * bar_char_width_,
                        40,
                        Display::tft_background);
        }
    }
    
    previous_signed_bars_ = target_signed_bars;
    last_rendered_power_ = current_power_;
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
