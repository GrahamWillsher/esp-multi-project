/**
 * SOC Widget LVGL Implementation
 */

#ifdef USE_LVGL

#include "soc_widget_lvgl.h"
#include "../../common.h"
#include "../../helpers.h"
#include <cstdio>

namespace Display {
namespace Widgets {

// Static member initialization
uint32_t SocWidgetLvgl::soc_gradient_[101];
bool SocWidgetLvgl::gradient_initialized_ = false;

SocWidgetLvgl::SocWidgetLvgl(lv_obj_t* parent, uint16_t center_x, uint16_t center_y)
    : container_(nullptr), label_(nullptr), last_value_(-1.0f), precision_(1) {
    
    // Initialize gradient (once)
    if (!gradient_initialized_) {
        init_gradient();
    }
    
    // Create container for SOC display
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, 200, 70);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(container_, 0, 0);         // No border
    lv_obj_set_style_pad_all(container_, 0, 0);              // No padding
    lv_obj_align(container_, LV_ALIGN_TOP_MID, center_x - (::Display::SCREEN_WIDTH / 2), center_y - 35);
    
    // Create label for SOC number
    label_ = lv_label_create(container_);
    lv_label_set_text(label_, "0.0");
    
    // Apply Montserrat 28pt font (equivalent to FreeSansBold18pt7b at size 2)
    lv_obj_set_style_text_font(label_, &lv_font_montserrat_28, 0);
    
    // Align label in center of container
    lv_obj_center(label_);
    
    LOG_DEBUG("LVGL_SOC", "SOC widget created at (%d, %d)", center_x, center_y);
}

SocWidgetLvgl::~SocWidgetLvgl() {
    if (label_) {
        lv_obj_del(label_);
    }
    if (container_) {
        lv_obj_del(container_);
    }
}

void SocWidgetLvgl::init_gradient() {
    LOG_DEBUG("LVGL_SOC", "Initializing SOC color gradient (101 steps)...");
    
    // Same gradient as TFT version: RED → AMBER → LIME → GREEN
    // Split into 3 segments for smoother transitions
    
    // Segment 1: RED (0xFF0000) → AMBER (0xFFC800) [0-33]
    const uint32_t RED = 0xFF0000;
    const uint32_t AMBER = 0xFFC800;
    for (int i = 0; i <= 33; i++) {
        float t = (float)i / 33.0f;
        uint8_t r = (uint8_t)((RED >> 16) + t * ((AMBER >> 16) - (RED >> 16)));
        uint8_t g = (uint8_t)(((RED >> 8) & 0xFF) + t * (((AMBER >> 8) & 0xFF) - ((RED >> 8) & 0xFF)));
        uint8_t b = (uint8_t)((RED & 0xFF) + t * ((AMBER & 0xFF) - (RED & 0xFF)));
        soc_gradient_[i] = (r << 16) | (g << 8) | b;
    }
    
    // Segment 2: AMBER (0xFFC800) → LIME (0xBFFF00) [34-66]
    const uint32_t LIME = 0xBFFF00;
    for (int i = 34; i <= 66; i++) {
        float t = (float)(i - 34) / 32.0f;
        uint8_t r = (uint8_t)((AMBER >> 16) + t * ((LIME >> 16) - (AMBER >> 16)));
        uint8_t g = (uint8_t)(((AMBER >> 8) & 0xFF) + t * (((LIME >> 8) & 0xFF) - ((AMBER >> 8) & 0xFF)));
        uint8_t b = (uint8_t)((AMBER & 0xFF) + t * ((LIME & 0xFF) - (AMBER & 0xFF)));
        soc_gradient_[i] = (r << 16) | (g << 8) | b;
    }
    
    // Segment 3: LIME (0xBFFF00) → GREEN (0x00FF00) [67-100]
    const uint32_t GREEN = 0x00FF00;
    for (int i = 67; i <= 100; i++) {
        float t = (float)(i - 67) / 33.0f;
        uint8_t r = (uint8_t)((LIME >> 16) + t * ((GREEN >> 16) - (LIME >> 16)));
        uint8_t g = (uint8_t)(((LIME >> 8) & 0xFF) + t * (((GREEN >> 8) & 0xFF) - ((LIME >> 8) & 0xFF)));
        uint8_t b = (uint8_t)((LIME & 0xFF) + t * ((GREEN & 0xFF) - (LIME & 0xFF)));
        soc_gradient_[i] = (r << 16) | (g << 8) | b;
    }
    
    gradient_initialized_ = true;
    LOG_DEBUG("LVGL_SOC", "Gradient initialized: 0x%06X → 0x%06X → 0x%06X → 0x%06X",
              RED, AMBER, LIME, GREEN);
}

uint32_t SocWidgetLvgl::calculate_soc_color(float soc_percent) {
    // Clamp to valid range
    if (soc_percent < 0.0f) soc_percent = 0.0f;
    if (soc_percent > 100.0f) soc_percent = 100.0f;
    
    // Map to gradient index (0-100)
    int idx = (int)(soc_percent);
    if (idx > 100) idx = 100;
    
    return soc_gradient_[idx];
}

void SocWidgetLvgl::update(float soc_percent) {
    // Check if value changed (dirty flag equivalent)
    if (soc_percent == last_value_) {
        return;  // No update needed
    }
    
    // Update label text
    char buf[12];
    if (precision_ == 0) {
        snprintf(buf, sizeof(buf), "%.0f", soc_percent);
    } else if (precision_ == 1) {
        snprintf(buf, sizeof(buf), "%.1f", soc_percent);
    } else {
        snprintf(buf, sizeof(buf), "%.2f", soc_percent);
    }
    lv_label_set_text(label_, buf);
    
    // Update color based on SOC gradient
    uint32_t color_hex = calculate_soc_color(soc_percent);
    lv_color_t color = lv_color_hex(color_hex);
    lv_obj_set_style_text_color(label_, color, 0);
    
    // Re-center label (text width may have changed)
    lv_obj_center(label_);
    
    last_value_ = soc_percent;
    
    LOG_TRACE("LVGL_SOC", "Updated SOC: %.1f%% (color: 0x%06X)", soc_percent, color_hex);
}

void SocWidgetLvgl::set_visible(bool visible) {
    if (container_) {
        if (visible) {
            lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void SocWidgetLvgl::set_precision(uint8_t precision) {
    if (precision > 2) precision = 2;
    precision_ = precision;
}

} // namespace Widgets
} // namespace Display

#endif // USE_LVGL
