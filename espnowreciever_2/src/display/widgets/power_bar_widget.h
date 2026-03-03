/**
 * Power Bar Widget
 * Displays power as horizontal bar with gradient coloring
 */

#pragma once

#include "base_widget.h"
#include <TFT_eSPI.h>

namespace Display {
namespace Widgets {

/**
 * Horizontal bar widget for displaying power flow
 * Shows charge (green) on left, discharge (red) on right
 */
class PowerBarWidget : public BaseWidget {
public:
    PowerBarWidget(HAL::IDisplayDriver* driver, uint16_t center_x, uint16_t y)
        : BaseWidget(driver, center_x, y, 0, 0),
          current_power_(0),
          last_rendered_power_(INT32_MAX),
          max_power_(4000),
          max_bars_per_side_(-1),
          bar_char_width_(0),
          previous_signed_bars_(0),
          last_was_zero_(false) {
        // Gradients initialized on first update
    }
    
    // Set current power value (positive = discharge, negative = charge)
    void set_power(int32_t power_w) {
        if (current_power_ != power_w) {
            current_power_ = power_w;
            mark_dirty();
        }
    }
    
    // Set maximum power for scaling
    void set_max_power(int32_t max_power_w) {
        if (max_power_ != max_power_w) {
            max_power_ = max_power_w;
            mark_dirty();
        }
    }
    
    // Update/render the widget
    void update() override;
    
private:
    int32_t current_power_;
    int32_t last_rendered_power_;
    int32_t max_power_;
    
    int max_bars_per_side_;
    int bar_char_width_;
    uint16_t gradient_green_[30];
    uint16_t gradient_red_[30];
    
    int previous_signed_bars_;
    bool last_was_zero_;
    int32_t last_displayed_power_text_ = INT32_MAX;
    
    // Initialize gradients if needed
    void init_gradients();
    
    // Render the power bar
    void render_bar();

    // Render numeric power text below the bar
    void render_power_text();
    
    // Get TFT_eSPI reference
    TFT_eSPI& get_tft();
};

} // namespace Widgets
} // namespace Display
