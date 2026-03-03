/**
 * Power Widget LVGL Implementation
 */

#ifdef USE_LVGL

#include "power_widget_lvgl.h"
#include "../../common.h"
#include "../../helpers.h"
#include <cstdio>
#include <cmath>

namespace Display {
namespace Widgets {

// Static member initialization
uint32_t PowerWidgetLvgl::gradient_green_[MAX_BARS_PER_SIDE];
uint32_t PowerWidgetLvgl::gradient_red_[MAX_BARS_PER_SIDE];
bool PowerWidgetLvgl::gradients_initialized_ = false;

PowerWidgetLvgl::PowerWidgetLvgl(lv_obj_t* parent, uint16_t center_x, uint16_t center_y)
    : container_(nullptr),
      left_bar_container_(nullptr),
      right_bar_container_(nullptr),
      power_label_(nullptr),
      last_power_(-999999),
      max_power_(5000),  // Default 5000W
      center_x_(center_x),
      center_y_(center_y),
      last_bar_count_(0),
      last_was_charging_(false),
      last_was_zero_(true) {
    
    // Initialize gradients (once)
    if (!gradients_initialized_) {
        init_gradients();
    }
    
    // Create main container
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, ::Display::SCREEN_WIDTH, 60);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_align(container_, LV_ALIGN_CENTER, 0, center_y - (::Display::SCREEN_HEIGHT / 2) - 30);
    
    // Create left bar container (charging)
    left_bar_container_ = lv_obj_create(container_);
    lv_obj_set_size(left_bar_container_, center_x, BAR_HEIGHT + 10);
    lv_obj_set_style_bg_opa(left_bar_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_bar_container_, 0, 0);
    lv_obj_set_style_pad_all(left_bar_container_, 0, 0);
    lv_obj_align(left_bar_container_, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Create right bar container (discharging)
    right_bar_container_ = lv_obj_create(container_);
    lv_obj_set_size(right_bar_container_, ::Display::SCREEN_WIDTH - center_x, BAR_HEIGHT + 10);
    lv_obj_set_style_bg_opa(right_bar_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_bar_container_, 0, 0);
    lv_obj_set_style_pad_all(right_bar_container_, 0, 0);
    lv_obj_align(right_bar_container_, LV_ALIGN_RIGHT_MID, 0, 0);
    
    // Create power text label (bottom of screen)
    power_label_ = lv_label_create(parent);
    lv_label_set_text(power_label_, "0W");
    lv_obj_set_style_text_font(power_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(power_label_, lv_color_white(), 0);
    lv_obj_align(power_label_, LV_ALIGN_BOTTOM_MID, 0, -2);
    
    LOG_DEBUG("LVGL_PWR", "Power widget created at (%d, %d)", center_x, center_y);
}

PowerWidgetLvgl::~PowerWidgetLvgl() {
    if (power_label_) {
        lv_obj_del(power_label_);
    }
    if (left_bar_container_) {
        lv_obj_del(left_bar_container_);
    }
    if (right_bar_container_) {
        lv_obj_del(right_bar_container_);
    }
    if (container_) {
        lv_obj_del(container_);
    }
}

void PowerWidgetLvgl::init_gradients() {
    LOG_DEBUG("LVGL_PWR", "Initializing power bar gradients (%d steps)...", MAX_BARS_PER_SIDE);
    
    const uint32_t BLUE = 0x0000FF;
    const uint32_t GREEN = 0x00FF00;
    const uint32_t RED = 0xFF0000;
    
    // Gradient 1: BLUE → GREEN (charging, left side)
    for (int i = 0; i < MAX_BARS_PER_SIDE; i++) {
        float t = (float)i / (float)(MAX_BARS_PER_SIDE - 1);
        uint8_t r = (uint8_t)((BLUE >> 16) + t * ((GREEN >> 16) - (BLUE >> 16)));
        uint8_t g = (uint8_t)(((BLUE >> 8) & 0xFF) + t * (((GREEN >> 8) & 0xFF) - ((BLUE >> 8) & 0xFF)));
        uint8_t b = (uint8_t)((BLUE & 0xFF) + t * ((GREEN & 0xFF) - (BLUE & 0xFF)));
        gradient_green_[i] = (r << 16) | (g << 8) | b;
    }
    
    // Gradient 2: BLUE → RED (discharging, right side)
    for (int i = 0; i < MAX_BARS_PER_SIDE; i++) {
        float t = (float)i / (float)(MAX_BARS_PER_SIDE - 1);
        uint8_t r = (uint8_t)((BLUE >> 16) + t * ((RED >> 16) - (BLUE >> 16)));
        uint8_t g = (uint8_t)(((BLUE >> 8) & 0xFF) + t * (((RED >> 8) & 0xFF) - ((BLUE >> 8) & 0xFF)));
        uint8_t b = (uint8_t)((BLUE & 0xFF) + t * ((RED & 0xFF) - (BLUE & 0xFF)));
        gradient_red_[i] = (r << 16) | (g << 8) | b;
    }
    
    gradients_initialized_ = true;
    LOG_DEBUG("LVGL_PWR", "Gradients initialized: BLUE→GREEN, BLUE→RED");
}

int PowerWidgetLvgl::calculate_bar_count(int32_t power_w) {
    int32_t abs_power = abs(power_w);
    if (abs_power >= max_power_) {
        return MAX_BARS_PER_SIDE;
    }
    
    int bars = (abs_power * MAX_BARS_PER_SIDE) / max_power_;
    
    // Ensure at least 1 bar for non-zero power
    if (abs_power > 10 && bars == 0) {
        bars = 1;
    }
    
    return bars;
}

void PowerWidgetLvgl::clear_bars() {
    // LVGL: Delete all child objects (auto cleanup)
    lv_obj_clean(left_bar_container_);
    lv_obj_clean(right_bar_container_);
}

void PowerWidgetLvgl::draw_charging_bars(int bar_count) {
    // Draw bars from right to left (charging direction)
    for (int i = 0; i < bar_count; i++) {
        // Create bar object
        lv_obj_t* bar = lv_obj_create(left_bar_container_);
        lv_obj_set_size(bar, BAR_WIDTH, BAR_HEIGHT);
        
        // Get color from gradient
        uint32_t color_hex = gradient_green_[i];
        lv_obj_set_style_bg_color(bar, lv_color_hex(color_hex), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        
        // Position bar (right-aligned, moving left)
        int x_offset = -(i * (BAR_WIDTH + BAR_SPACING)) - BAR_WIDTH;
        lv_obj_set_x(bar, center_x_ + x_offset);
        lv_obj_set_y(bar, 5);
    }
}

void PowerWidgetLvgl::draw_discharging_bars(int bar_count) {
    // Draw bars from left to right (discharging direction)
    for (int i = 0; i < bar_count; i++) {
        // Create bar object
        lv_obj_t* bar = lv_obj_create(right_bar_container_);
        lv_obj_set_size(bar, BAR_WIDTH, BAR_HEIGHT);
        
        // Get color from gradient
        uint32_t color_hex = gradient_red_[i];
        lv_obj_set_style_bg_color(bar, lv_color_hex(color_hex), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        
        // Position bar (left-aligned, moving right)
        int x_offset = i * (BAR_WIDTH + BAR_SPACING);
        lv_obj_set_x(bar, x_offset);
        lv_obj_set_y(bar, 5);
    }
}

void PowerWidgetLvgl::draw_center_marker() {
    // Draw single BLUE bar at center
    lv_obj_t* marker = lv_obj_create(container_);
    lv_obj_set_size(marker, BAR_WIDTH, BAR_HEIGHT);
    lv_obj_set_style_bg_color(marker, lv_color_hex(0x0000FF), 0);  // BLUE
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(marker, 0, 0);
    lv_obj_set_style_radius(marker, 0, 0);
    lv_obj_align(marker, LV_ALIGN_CENTER, 0, -15);
}

void PowerWidgetLvgl::update_power_text(int32_t power_w) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%dW", power_w);
    lv_label_set_text(power_label_, buf);
}

void PowerWidgetLvgl::trigger_ripple(int bar_count, bool is_charging) {
    // Create ripple animation using LVGL animation engine
    // This will dim bars sequentially for visual effect
    
    // For LVGL, we can implement this using opacity animation
    // over each bar in sequence (Option 1 - enhanced animation)
    
    // TODO: Implement ripple using lv_anim_t with opacity callbacks
    // For now, static display (ripple can be added as enhancement)
    
    LOG_TRACE("LVGL_PWR", "Ripple animation triggered (%d bars, %s)",
              bar_count, is_charging ? "charging" : "discharging");
}

void PowerWidgetLvgl::update(int32_t power_w) {
    bool is_zero = (abs(power_w) < 10);
    bool is_charging = (power_w < 0);
    int bar_count = calculate_bar_count(power_w);
    
    // Check if we should trigger ripple (same direction, same bar count)
    bool same_direction = (last_was_charging_ == is_charging);
    bool same_bar_count = (last_bar_count_ == bar_count);
    bool should_ripple = same_direction && same_bar_count && !is_zero && !last_was_zero_;
    
    if (should_ripple) {
        trigger_ripple(bar_count, is_charging);
    } else {
        // Clear and redraw bars
        clear_bars();
        
        if (is_zero) {
            draw_center_marker();
            last_was_zero_ = true;
        } else {
            if (is_charging) {
                draw_charging_bars(bar_count);
            } else {
                draw_discharging_bars(bar_count);
            }
            last_was_zero_ = false;
        }
    }
    
    // Update power text
    update_power_text(power_w);
    
    // Update tracking variables
    last_power_ = power_w;
    last_bar_count_ = bar_count;
    last_was_charging_ = is_charging;
    
    LOG_TRACE("LVGL_PWR", "Updated power: %dW (%d bars, %s)",
              power_w, bar_count, is_charging ? "charging" : "discharging");
}

void PowerWidgetLvgl::set_visible(bool visible) {
    if (container_) {
        if (visible) {
            lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (power_label_) {
        if (visible) {
            lv_obj_clear_flag(power_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(power_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void PowerWidgetLvgl::set_max_power(int32_t max_power) {
    max_power_ = max_power;
}

} // namespace Widgets
} // namespace Display

#endif // USE_LVGL
