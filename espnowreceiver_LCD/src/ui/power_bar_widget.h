#pragma once

#include <Arduino.h>
#include <climits>
#include <LovyanGFX.hpp>

#include "app_config.h"
#include "ui/colors.h"

class PowerBarWidget {
public:
    void draw(lgfx::LGFX_Device& display, int center_x, int center_y, int32_t power_w, uint32_t now_ms);

private:
    uint16_t gradient_blue_to_green(uint8_t index, uint8_t max) const;
    uint16_t gradient_blue_to_red(uint8_t index, uint8_t max) const;
    uint16_t dim_rgb565(uint16_t c) const;
    void get_bar_rect(int bar_index, int center_x, bool negative, int seg_count, int left_pitch, int right_pitch, int center_gap, int& left, int& width) const;
    void draw_bar(int bar_index, lgfx::LGFX_Device& display, int center_x, int center_y, bool negative, int seg_count, int left_pitch, int right_pitch, int center_gap, int seg_h, uint16_t color) const;
    void clear_bar(int bar_index, lgfx::LGFX_Device& display, int center_x, int center_y, bool negative, int seg_count, int left_pitch, int right_pitch, int center_gap, int seg_h) const;

    bool has_last_state_ = false;
    int last_bar_count_ = 0;
    bool last_was_charging_ = false;
    bool last_was_zero_ = true;
    int32_t last_displayed_power_text_ = INT32_MAX;
    bool ripple_active_ = false;
    uint32_t ripple_start_ms_ = 0;
};
