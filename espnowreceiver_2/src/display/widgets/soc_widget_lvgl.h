/**
 * SOC Display Widget (LVGL Implementation)
 * 
 * Displays battery State of Charge as a proportional number with color gradient
 * - Uses LVGL label widget for text display
 * - Automatic dirty region tracking (no manual partial redraws needed)
 * - Color gradient: RED (0%) → AMBER → LIME → GREEN (100%)
 */

#pragma once

#ifdef USE_LVGL

#include <lvgl.h>
#include <cstdint>

namespace Display {
namespace Widgets {

class SocWidgetLvgl {
public:
    /**
     * Constructor
     * @param parent Parent LVGL object (screen or container)
     * @param center_x X position for centering
     * @param center_y Y position for centering
     */
    SocWidgetLvgl(lv_obj_t* parent, uint16_t center_x, uint16_t center_y);
    
    /**
     * Destructor - cleanup LVGL objects
     */
    ~SocWidgetLvgl();
    
    /**
     * Update SOC value and color
     * @param soc_percent Battery SOC (0.0 to 100.0)
     */
    void update(float soc_percent);
    
    /**
     * Set visibility
     */
    void set_visible(bool visible);
    
    /**
     * Set decimal precision (0-2)
     */
    void set_precision(uint8_t precision);

private:
    lv_obj_t* container_;
    lv_obj_t* label_;
    float last_value_;
    uint8_t precision_;
    
    /**
     * Calculate SOC color from gradient
     * Returns RGB565 color based on SOC percentage
     */
    uint32_t calculate_soc_color(float soc_percent);
    
    /**
     * Initialize SOC color gradient (same as TFT version)
     * 101 steps from RED to GREEN
     */
    void init_gradient();
    
    static uint32_t soc_gradient_[101];
    static bool gradient_initialized_;
};

} // namespace Widgets
} // namespace Display

#endif // USE_LVGL
