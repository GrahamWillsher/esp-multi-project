/**
 * Main Status Page Implementation
 */

#include "status_page.h"
#include "../../common.h"
#include <TFT_eSPI.h>

// Font declarations (from TFT_eSPI library)
extern const GFXfont FreeSansBold18pt7b;

namespace Display {
namespace Pages {

StatusPage::StatusPage(HAL::IDisplayDriver* driver)
    : driver_(driver),
      soc_display_(driver, Display::SCREEN_WIDTH / 2, Display::SCREEN_HEIGHT / 3),
      power_bar_(driver, Display::SCREEN_WIDTH / 2, 130),  // Moved up from 141 for better text spacing
      visible_(true) {
    
    // Configure SOC display
    soc_display_.set_font(&FreeSansBold18pt7b);
    soc_display_.set_precision(1);
    
    // Power bar is configured with defaults
}

void StatusPage::update_soc(float soc_percent) {
    soc_display_.set_value(soc_percent);
    
    // Update color based on SOC level
    uint16_t soc_color;
    if (!Display::soc_gradient_initialized) {
        soc_color = TFT_WHITE;
    } else {
        int gradient_index = (int)((soc_percent / 100.0f) * Display::TOTAL_GRADIENT_STEPS);
        if (gradient_index < 0) gradient_index = 0;
        if (gradient_index > Display::TOTAL_GRADIENT_STEPS) gradient_index = Display::TOTAL_GRADIENT_STEPS;
        soc_color = Display::soc_color_gradient[gradient_index];
    }
    
    soc_display_.set_color(soc_color);
}

void StatusPage::update_power(int32_t power_w) {
    power_bar_.set_power(power_w);
}

void StatusPage::render() {
    if (!visible_) {
        return;
    }
    
    // Update all widgets
    soc_display_.update();
    power_bar_.update();
}

void StatusPage::set_visible(bool visible) {
    visible_ = visible;
    soc_display_.set_visible(visible);
    power_bar_.set_visible(visible);
}

} // namespace Pages
} // namespace Display
