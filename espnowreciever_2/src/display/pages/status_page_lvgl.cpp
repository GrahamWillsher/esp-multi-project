/**
 * Status Page LVGL Implementation
 */

#ifdef USE_LVGL

#include "status_page_lvgl.h"
#include "../../common.h"

namespace Display {
namespace Pages {

StatusPageLvgl::StatusPageLvgl()
    : screen_(nullptr), soc_widget_(nullptr), power_widget_(nullptr), visible_(true) {
    
    LOG_DEBUG("LVGL_PAGE", "Creating status page...");
    
    // Create main screen
    screen_ = lv_obj_create(NULL);
    lv_obj_set_size(screen_, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);
    lv_obj_set_style_border_width(screen_, 0, 0);
    lv_obj_set_style_pad_all(screen_, 0, 0);
    
    // Create SOC widget (upper area, centered)
    soc_widget_ = new Widgets::SocWidgetLvgl(
        screen_,
        ::Display::SCREEN_WIDTH / 2,
        ::Display::SCREEN_HEIGHT / 3
    );
    soc_widget_->set_precision(1);  // 1 decimal place
    
    // Create power bar widget (middle area, centered)
    power_widget_ = new Widgets::PowerWidgetLvgl(
        screen_,
        ::Display::SCREEN_WIDTH / 2,
        130  // Same Y position as TFT version
    );
    power_widget_->set_max_power(5000);  // 5000W max
    
    LOG_INFO("LVGL_PAGE", "Status page created successfully");
}

StatusPageLvgl::~StatusPageLvgl() {
    if (soc_widget_) {
        delete soc_widget_;
        soc_widget_ = nullptr;
    }
    if (power_widget_) {
        delete power_widget_;
        power_widget_ = nullptr;
    }
    if (screen_) {
        lv_obj_del(screen_);
        screen_ = nullptr;
    }
}

void StatusPageLvgl::update_soc(float soc_percent) {
    if (soc_widget_) {
        soc_widget_->update(soc_percent);
    }
}

void StatusPageLvgl::update_power(int32_t power_w) {
    if (power_widget_) {
        power_widget_->update(power_w);
    }
}

void StatusPageLvgl::render() {
    // LVGL handles rendering automatically via dirty region tracking
    // No explicit render() call needed
    // This function exists for API compatibility with TFT version
}

void StatusPageLvgl::show() {
    if (!screen_) return;
    // Guard: if already the active screen, do nothing — calling lv_scr_load_anim
    // with auto_del=true when we ARE the active screen would delete ourselves.
    if (lv_scr_act() == screen_) {
        LOG_DEBUG("LVGL_PAGE", "Status page already active — no-op");
        return;
    }
    // auto_del=true: LVGL deletes the previous active screen (ready_scr or
    // error_scr) AFTER updating disp->act_scr — the only safe way to do this.
    lv_scr_load_anim(screen_, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    lv_timer_handler();
    LOG_DEBUG("LVGL_PAGE", "Status page loaded as active screen");
}

void StatusPageLvgl::set_visible(bool visible) {
    visible_ = visible;
    
    if (soc_widget_) {
        soc_widget_->set_visible(visible);
    }
    if (power_widget_) {
        power_widget_->set_visible(visible);
    }
}

} // namespace Pages
} // namespace Display

#endif // USE_LVGL
