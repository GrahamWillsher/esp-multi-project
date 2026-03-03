/**
 * Main Status Page
 * Displays SOC and power using widget components
 */

#pragma once

#include "../widgets/proportional_number_widget.h"
#include "../widgets/power_bar_widget.h"
#include <hal/display/idisplay_driver.h>

namespace Display {
namespace Pages {

/**
 * Main status page showing battery state of charge and power
 */
class StatusPage {
public:
    explicit StatusPage(HAL::IDisplayDriver* driver);
    
    // Update data values
    void update_soc(float soc_percent);
    void update_power(int32_t power_w);
    
    // Render all widgets
    void render();
    
    // Show/hide entire page
    void set_visible(bool visible);
    
private:
    HAL::IDisplayDriver* driver_;
    
    // Widgets
    Widgets::ProportionalNumberWidget soc_display_;
    Widgets::PowerBarWidget power_bar_;
    
    // Page state
    bool visible_;
};

} // namespace Pages
} // namespace Display
