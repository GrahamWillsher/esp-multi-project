/**
 * Status Page (LVGL Implementation)
 * 
 * Composite page that displays:
 * - SOC (State of Charge) widget
 * - Power bar widget
 * 
 * Equivalent to TFT status_page.cpp
 */

#pragma once

#ifdef USE_LVGL

#include <lvgl.h>
#include "../widgets/soc_widget_lvgl.h"
#include "../widgets/power_widget_lvgl.h"

namespace Display {
namespace Pages {

class StatusPageLvgl {
public:
    /**
     * Constructor - creates status page screen and widgets
     */
    StatusPageLvgl();
    
    /**
     * Destructor - cleanup LVGL objects
     */
    ~StatusPageLvgl();
    
    /**
     * Update SOC value
     * @param soc_percent Battery SOC (0.0 to 100.0)
     */
    void update_soc(float soc_percent);
    
    /**
     * Update power value
     * @param power_w Power in watts
     */
    void update_power(int32_t power_w);
    
    /**
     * Render/update all widgets (LVGL handles this automatically)
     */
    void render();
    
    /**
     * Show this screen (load as active screen)
     */
    void show();
    
    /**
     * Set visibility of entire page
     */
    void set_visible(bool visible);
    
    /**
     * Get LVGL screen object
     */
    lv_obj_t* get_screen() { return screen_; }

private:
    lv_obj_t* screen_;
    Widgets::SocWidgetLvgl* soc_widget_;
    Widgets::PowerWidgetLvgl* power_widget_;
    bool visible_;
};

} // namespace Pages
} // namespace Display

#endif // USE_LVGL
