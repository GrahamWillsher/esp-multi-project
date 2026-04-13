#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "ui/colors.h"

class SocWidget {
public:
    void draw(lgfx::LGFX_Device& display, int center_x, int center_y, float soc_percent);

private:
    uint16_t soc_color(float soc_percent) const;
    bool has_last_value_ = false;
    int16_t last_soc_tenths_ = 0;
    bool initialized_ = false;
    uint16_t last_color_ = UI::Colors::WHITE;
    char last_text_[16] = {0};
};
