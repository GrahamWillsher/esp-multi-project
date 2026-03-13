/**
 * Proportional Number Widget Implementation
 */

#include "proportional_number_widget.h"
#include "../../common.h"
#include <cstdio>
#include <cmath>

// Forward declarations
extern TFT_eSPI tft;

namespace Display {
namespace Widgets {

// Get TFT_eSPI reference for font operations
TFT_eSPI& ProportionalNumberWidget::get_tft() {
    return ::tft;
}

void ProportionalNumberWidget::calculate_font_metrics() {
    if (max_digit_width_ > 0 || !font_) {
        return;  // Already calculated or no font set
    }
    
    TFT_eSPI& tft = get_tft();
    tft.setFreeFont(font_);
    tft.setTextSize(2);
    
    // Use '8' as widest digit + 6px margin (3px each side)
    max_digit_width_ = tft.textWidth("8") + 6;
    max_digit_height_ = tft.fontHeight() + 6;
    
    // Decimal point is narrower
    decimal_point_width_ = tft.textWidth(".") + 6;
}

void ProportionalNumberWidget::render_number() {
    if (!font_) {
        return;  // No font set
    }
    
    TFT_eSPI& tft = get_tft();
    
    // Ensure font metrics are calculated
    calculate_font_metrics();
    
    // ALWAYS set font at start
    tft.setFreeFont(font_);
    tft.setTextSize(2);
    
    // Convert number to string with configured precision
    char numStr[12];
    const char* format = (precision_ == 0) ? "%.0f" : (precision_ == 1) ? "%.1f" : "%.2f";
    snprintf(numStr, sizeof(numStr), format, current_value_);
    
    // Calculate total width needed
    int numDigits = strlen(numStr);
    int decimalCount = 0;
    for (int i = 0; i < numDigits; i++) {
        if (numStr[i] == '.') decimalCount++;
    }
    
    int totalWidth = (numDigits - decimalCount) * max_digit_width_ + decimalCount * decimal_point_width_;
    int startX = x_ - (totalWidth / 2);
    
    // Render each digit
    for (int i = 0; i < numDigits; i++) {
        char digit = numStr[i];
        bool changed = (i >= last_num_digits_) || (last_num_str_[i] != digit);
        
        // Calculate this digit's box
        int boxWidth = (digit == '.') ? decimal_point_width_ : max_digit_width_;
        int digitX = startX;
        
        // Only redraw if digit changed or position changed
        if (changed || startX != last_start_x_) {
            // Clear old digit area with background
            tft.fillRect(digitX, y_ - (max_digit_height_ / 2), 
                        boxWidth, max_digit_height_, 
                        Display::tft_background);
            
            // Get actual width of this digit for centering
            char digitStr[2] = {digit, '\0'};
            int actualWidth = tft.textWidth(digitStr);
            
            // Center digit in box
            int offsetX = (boxWidth - actualWidth) / 2;
            
            // Draw the digit
            tft.setTextColor(color_, Display::tft_background);
            tft.setTextDatum(ML_DATUM);
            tft.drawString(digitStr, digitX + offsetX, y_);
        }
        
        startX += boxWidth;
    }
    
    // Clear any extra digits from previous render
    if (last_num_digits_ > numDigits && last_start_x_ > 0) {
        int clearStartX = startX;
        int clearEndX = last_start_x_;
        for (int i = numDigits; i < last_num_digits_; i++) {
            char oldDigit = last_num_str_[i];
            int oldBoxWidth = (oldDigit == '.') ? decimal_point_width_ : max_digit_width_;
            clearEndX += oldBoxWidth;
        }
        
        if (clearEndX > clearStartX) {
            tft.fillRect(clearStartX, y_ - (max_digit_height_ / 2),
                        clearEndX - clearStartX, max_digit_height_,
                        Display::tft_background);
        }
    }
    
    // Save current state for next render
    strncpy(last_num_str_, numStr, sizeof(last_num_str_) - 1);
    last_num_str_[sizeof(last_num_str_) - 1] = '\0';
    last_num_digits_ = numDigits;
    last_start_x_ = x_ - (totalWidth / 2);
    last_rendered_value_ = current_value_;
}

void ProportionalNumberWidget::update() {
    if (!needs_redraw()) {
        return;
    }
    
    if (!visible_) {
        clear_dirty();
        return;
    }
    
    render_number();
    clear_dirty();
}

} // namespace Widgets
} // namespace Display
