#include "ui/runtime/splash_sequence.h"

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <algorithm>
#include <cmath>

#include "app_config.h"
#include "ui/colors.h"

namespace UI::Runtime {
namespace {

void backlight_pwm_step(lgfx::LGFX_Device& display, uint8_t level_0_to_255, uint32_t step_ms) {
    if (level_0_to_255 == 0) {
        display.setBrightness(0);
        delay(step_ms);
        return;
    }
    if (level_0_to_255 >= 255) {
        display.setBrightness(255);
        delay(step_ms);
        return;
    }

    constexpr uint32_t kCycleUs = 2000;
    const uint32_t end_ms = millis() + step_ms;
    const uint32_t on_us = (kCycleUs * static_cast<uint32_t>(level_0_to_255)) / 255U;
    const uint32_t off_us = (on_us < kCycleUs) ? (kCycleUs - on_us) : 0;

    while (millis() < end_ms) {
        if (on_us > 0) {
            display.setBrightness(255);
            delayMicroseconds(on_us);
        }
        if (off_us > 0) {
            display.setBrightness(0);
            delayMicroseconds(off_us);
        }
    }
}

uint8_t brightness_from_progress(float progress_0_to_1) {
    if (progress_0_to_1 <= 0.0f) return 0;
    if (progress_0_to_1 >= 1.0f) return 255;

    constexpr float kPi = 3.14159265f;
    const float eased = 0.5f - (0.5f * cosf(progress_0_to_1 * kPi));
    constexpr int kMinVisibleDuty = 16;
    const int duty = kMinVisibleDuty + static_cast<int>(eased * static_cast<float>(255 - kMinVisibleDuty) + 0.5f);
    return static_cast<uint8_t>(std::max(0, std::min(255, duty)));
}

bool read_jpeg_dimensions(fs::FS& fs, const char* path, int& width, int& height) {
    width = 0;
    height = 0;
    fs::File f = fs.open(path, FILE_READ);
    if (!f) return false;

    auto read_u8 = [&](uint8_t& out) -> bool {
        int c = f.read();
        if (c < 0) return false;
        out = static_cast<uint8_t>(c);
        return true;
    };

    auto read_u16be = [&](uint16_t& out) -> bool {
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!read_u8(hi) || !read_u8(lo)) return false;
        out = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
        return true;
    };

    uint8_t b0 = 0;
    uint8_t b1 = 0;
    if (!read_u8(b0) || !read_u8(b1) || b0 != 0xFF || b1 != 0xD8) {
        f.close();
        return false;
    }

    while (f.available() > 0) {
        uint8_t marker_prefix = 0;
        if (!read_u8(marker_prefix)) break;
        if (marker_prefix != 0xFF) continue;

        uint8_t marker = 0;
        do {
            if (!read_u8(marker)) {
                f.close();
                return false;
            }
        } while (marker == 0xFF);

        if (marker == 0xD9 || marker == 0xDA) break;

        uint16_t seg_len = 0;
        if (!read_u16be(seg_len) || seg_len < 2) break;

        const bool is_sof = (marker == 0xC0) || (marker == 0xC1) || (marker == 0xC2) || (marker == 0xC3) ||
                            (marker == 0xC5) || (marker == 0xC6) || (marker == 0xC7) ||
                            (marker == 0xC9) || (marker == 0xCA) || (marker == 0xCB) ||
                            (marker == 0xCD) || (marker == 0xCE) || (marker == 0xCF);

        if (is_sof && seg_len >= 7) {
            uint8_t precision = 0;
            uint16_t h = 0;
            uint16_t w = 0;
            if (!read_u8(precision) || !read_u16be(h) || !read_u16be(w)) {
                f.close();
                return false;
            }
            (void)precision;
            width = static_cast<int>(w);
            height = static_cast<int>(h);
            f.close();
            return (width > 0 && height > 0);
        }

        const uint32_t skip = static_cast<uint32_t>(seg_len - 2);
        if (f.seek(f.position() + skip) == false) break;
    }

    f.close();
    return false;
}

}  // namespace

void run_splash_sequence(lgfx::LGFX_Device& display) {
    display.fillScreen(UI::Colors::BLACK);

    if (!SPIFFS.begin(true)) {
        delay(300);
        return;
    }

    const char* kSplashPath = "/BatteryEmulator_LCD.jpg";
    if (!SPIFFS.exists(kSplashPath)) {
        delay(300);
        return;
    }

    int jpg_w = 0;
    int jpg_h = 0;
    if (!read_jpeg_dimensions(SPIFFS, kSplashPath, jpg_w, jpg_h)) {
        delay(300);
        return;
    }

    const float scale_x = static_cast<float>(AppConfig::SCREEN_WIDTH) / static_cast<float>(jpg_w);
    const float scale_y = static_cast<float>(AppConfig::SCREEN_HEIGHT) / static_cast<float>(jpg_h);

    constexpr uint32_t kFadeMs = 2000;
    constexpr uint32_t kHoldMs = 2000;
    constexpr uint32_t kStepMs = 33;
    constexpr uint32_t kSteps = kFadeMs / kStepMs;

    fs::File splash = SPIFFS.open(kSplashPath, FILE_READ);
    if (!splash) {
        delay(300);
        return;
    }

    display.fillScreen(UI::Colors::BLACK);
    display.setBrightness(0);
    display.drawJpg(&splash, 0, 0, 0, 0, 0, 0, scale_x, scale_y);
    splash.close();

    for (uint32_t i = 0; i <= kSteps; ++i) {
        const float p = static_cast<float>(i) / static_cast<float>(kSteps);
        backlight_pwm_step(display, brightness_from_progress(p), kStepMs);
    }

    display.setBrightness(255);
    delay(kHoldMs);

    for (uint32_t i = 0; i <= kSteps; ++i) {
        const float p = static_cast<float>(i) / static_cast<float>(kSteps);
        backlight_pwm_step(display, static_cast<uint8_t>(255U - brightness_from_progress(p)), kStepMs);
    }

    display.setBrightness(0);
    display.fillScreen(UI::Colors::BLACK);
    delay(1000);
    display.setBrightness(255);
}

}  // namespace UI::Runtime
