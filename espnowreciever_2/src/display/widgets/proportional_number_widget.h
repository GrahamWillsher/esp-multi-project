/**
 * Proportional Number Widget
 * Displays numbers with proportional fonts, centering each digit in equal-width boxes
 * Optimized to only redraw digits that have changed
 */

#pragma once

#include "base_widget.h"
#include <TFT_eSPI.h>

namespace Display {
namespace Widgets {

/**
 * Number display widget with proportional font rendering
 * Each digit is centered in an equal-width box for stable visual appearance
 */
class ProportionalNumberWidget : public BaseWidget {
public:
    ProportionalNumberWidget(HAL::IDisplayDriver* driver, uint16_t center_x, uint16_t center_y)
        : BaseWidget(driver, center_x, center_y, 0, 0),
          font_(nullptr),
          current_value_(0.0f),
          last_rendered_value_(-999999.0f),
          color_(TFT_WHITE),
          max_digit_width_(0),
          max_digit_height_(0),
          decimal_point_width_(0),
          last_num_digits_(0),
          last_start_x_(0) {
        last_num_str_[0] = '\0';
    }
    
    // Set the font to use for rendering
    void set_font(const GFXfont* font) {
        if (font_ != font) {
            font_ = font;
            max_digit_width_ = 0;  // Force recalculation
            mark_dirty();
        }
    }
    
    // Set the number value to display
    void set_value(float value) {
        if (current_value_ != value) {
            current_value_ = value;
            mark_dirty();
        }
    }
    
    // Set text color
    void set_color(uint16_t color) {
        if (color_ != color) {
            color_ = color;
            mark_dirty();
        }
    }

    // Set decimal precision (0-2)
    void set_precision(uint8_t precision) {
        if (precision > 2) precision = 2;
        if (precision_ != precision) {
            precision_ = precision;
            mark_dirty();
        }
    }
    
    // Update/render the widget
    void update() override;
    
private:
    const GFXfont* font_;
    float current_value_;
    float last_rendered_value_;
    uint16_t color_;
    uint8_t precision_ = 1;
    
    // Font metrics (cached)
    int max_digit_width_;
    int max_digit_height_;
    int decimal_point_width_;
    
    // Previous render state for optimization
    char last_num_str_[12];
    int last_num_digits_;
    int last_start_x_;
    
    // Calculate font metrics if needed
    void calculate_font_metrics();
    
    // Render the number with selective digit redrawing
    void render_number();
    
    // Get TFT_eSPI reference for advanced operations
    TFT_eSPI& get_tft();
};

} // namespace Widgets
} // namespace Display
