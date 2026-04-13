#include "ui/soc_widget.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

uint16_t SocWidget::soc_color(float soc_percent) const {
    const float clamped = std::max(0.0f, std::min(100.0f, soc_percent));
    const float t = clamped / 100.0f;

    // Red -> yellow -> green
    uint8_t r = 0;
    uint8_t g = 0;
    if (t < 0.5f) {
        r = 255;
        g = static_cast<uint8_t>(510.0f * t);
    } else {
        r = static_cast<uint8_t>(255.0f * (1.0f - ((t - 0.5f) * 2.0f)));
        g = 255;
    }

    return UI::rgb565(r, g, 0);
}

void SocWidget::draw(lgfx::LGFX_Device& display, int center_x, int center_y, float soc_percent) {
    const int16_t soc_tenths = static_cast<int16_t>(soc_percent * 10.0f + (soc_percent >= 0.0f ? 0.5f : -0.5f));
    if (has_last_value_ && soc_tenths == last_soc_tenths_) {
        return;  // redraw only when displayed value changes
    }
    has_last_value_ = true;
    last_soc_tenths_ = soc_tenths;

    char soc_text[16];
    // Fixed-width formatting keeps glyph positions stable (less visual jitter).
    snprintf(soc_text, sizeof(soc_text), "%5.1f", static_cast<float>(soc_tenths) / 10.0f);
    const uint16_t text_color = soc_color(soc_percent);

    // SOC drawing region.
    constexpr int kSocBoxW = 620;
    constexpr int kSocBoxH = 240;
    const int box_left = center_x - (kSocBoxW / 2);
    const int box_top = center_y - (kSocBoxH / 2);

    // Typography/layout.
    const int text_center_x = center_x - 30;
    const int text_center_y = center_y + 20;

    display.setTextFont(8);
    display.setTextSize(2);
    display.setTextDatum(middle_left);

    const int total_w = display.textWidth(soc_text);
    const int start_x = text_center_x - (total_w / 2);
    const int text_h = display.fontHeight();

    const bool need_full_redraw = !initialized_ || (last_color_ != text_color);

    auto draw_label = [&]() {
        display.setTextColor(UI::Colors::WHITE, UI::Colors::BLACK);
        display.setTextDatum(top_center);
        display.setTextFont(2);
        display.setTextSize(2);
        display.drawString("State of Charge", center_x, box_top + 10);
    };

    if (need_full_redraw) {
        display.fillRect(box_left, box_top, kSocBoxW, kSocBoxH, UI::Colors::BLACK);
        draw_label();

        display.setTextFont(8);
        display.setTextSize(2);
        display.setTextDatum(middle_left);
        display.setTextColor(UI::Colors::DIM_GRAY, UI::Colors::BLACK);
        display.drawString(soc_text, start_x + 2, text_center_y + 2);
        display.setTextColor(text_color, UI::Colors::BLACK);
        display.drawString(soc_text, start_x, text_center_y);

        std::strncpy(last_text_, soc_text, sizeof(last_text_) - 1);
        last_text_[sizeof(last_text_) - 1] = '\0';
        initialized_ = true;
        last_color_ = text_color;
        return;
    }

    // Selective redraw: update only changed character boxes.
    const size_t new_len = std::strlen(soc_text);
    char prefix[16] = {0};
    for (size_t i = 0; i < new_len; ++i) {
        const char prev_ch = (i < std::strlen(last_text_)) ? last_text_[i] : '\0';
        if (prev_ch == soc_text[i]) {
            continue;
        }

        // X position of this character via prefix width.
        if (i > 0) {
            std::memcpy(prefix, soc_text, i);
        }
        prefix[i] = '\0';
        const int char_x = start_x + display.textWidth(prefix);

        char one_char[2] = {soc_text[i], '\0'};
        const int char_w = std::max(8, display.textWidth(one_char));

        display.fillRect(char_x - 2,
                         text_center_y - (text_h / 2) - 2,
                         char_w + 6,
                         text_h + 6,
                         UI::Colors::BLACK);

        display.setTextColor(UI::Colors::DIM_GRAY, UI::Colors::BLACK);
        display.drawString(one_char, char_x + 2, text_center_y + 2);
        display.setTextColor(text_color, UI::Colors::BLACK);
        display.drawString(one_char, char_x, text_center_y);
    }

    std::strncpy(last_text_, soc_text, sizeof(last_text_) - 1);
    last_text_[sizeof(last_text_) - 1] = '\0';
    last_color_ = text_color;
}
