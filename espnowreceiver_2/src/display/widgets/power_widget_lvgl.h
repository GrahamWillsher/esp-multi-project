/**
 * Power Bar Widget (LVGL Implementation)
 * 
 * Displays power as directional bars with gradient colors:
 * - Negative power (charging): LEFT bars, BLUE → GREEN gradient
 * - Positive power (discharging): RIGHT bars, BLUE → RED gradient
 * - Near-zero: Single BLUE center marker
 * - Includes ripple animation effect
 */

#pragma once

#ifdef USE_LVGL

#include <lvgl.h>
#include <cstdint>

namespace Display {
namespace Widgets {

class PowerWidgetLvgl {
public:
    /**
     * Constructor
     * @param parent Parent LVGL object (screen or container)
     * @param center_x X position for centering
     * @param center_y Y position for centering
     */
    PowerWidgetLvgl(lv_obj_t* parent, uint16_t center_x, uint16_t center_y);
    
    /**
     * Destructor - cleanup LVGL objects
     */
    ~PowerWidgetLvgl();
    
    /**
     * Update power value and redraw bars
     * @param power_w Power in watts (negative = charging, positive = discharging)
     */
    void update(int32_t power_w);
    
    /**
     * Set visibility
     */
    void set_visible(bool visible);
    
    /**
     * Set maximum power for scaling
     */
    void set_max_power(int32_t max_power);

private:
    lv_obj_t* container_;
    lv_obj_t* left_bar_container_;   // Charging (negative power)
    lv_obj_t* right_bar_container_;  // Discharging (positive power)
    lv_obj_t* power_label_;
    int32_t last_power_;
    int32_t max_power_;
    uint16_t center_x_;
    uint16_t center_y_;
    int last_bar_count_;
    bool last_was_charging_;
    bool last_was_zero_;
    
    static constexpr int MAX_BARS_PER_SIDE = 30;
    static constexpr int BAR_WIDTH = 8;
    static constexpr int BAR_HEIGHT = 30;
    static constexpr int BAR_SPACING = 3;
    
    /**
     * Initialize power gradients
     */
    void init_gradients();
    
    /**
     * Clear all bars
     */
    void clear_bars();
    
    /**
     * Draw bars for charging (left side)
     */
    void draw_charging_bars(int bar_count);
    
    /**
     * Draw bars for discharging (right side)
     */
    void draw_discharging_bars(int bar_count);
    
    /**
     * Draw center neutral marker
     */
    void draw_center_marker();
    
    /**
     * Calculate bar count from power value
     */
    int calculate_bar_count(int32_t power_w);
    
    /**
     * Update power text label
     */
    void update_power_text(int32_t power_w);
    
    /**
     * Trigger ripple animation (same direction, same bar count)
     */
    void trigger_ripple(int bar_count, bool is_charging);
    
    // Color gradients (same as TFT version)
    static uint32_t gradient_green_[MAX_BARS_PER_SIDE];  // BLUE → GREEN (charging)
    static uint32_t gradient_red_[MAX_BARS_PER_SIDE];    // BLUE → RED (discharging)
    static bool gradients_initialized_;
};

} // namespace Widgets
} // namespace Display

#endif // USE_LVGL
