#include <Arduino.h>
#include <cmath>
#include <FS.h>
#include <SPIFFS.h>

#include "app/demo_model.h"
#include "app_config.h"
#include "hal/lgfx_waveshare_7.h"
#include "ui/colors.h"
#include "ui/layout.h"
#include "ui/led_widget.h"
#include "ui/power_bar_widget.h"
#include "ui/soc_widget.h"

namespace {
lgfx_custom::LGFX_Waveshare7* display = nullptr;
DemoModel demo_model;
LedWidget led_widget;
SocWidget soc_widget;
PowerBarWidget power_bar_widget;

uint32_t last_frame_ms = 0;
uint32_t last_state_ms = 0;
uint32_t last_log_ms = 0;

void log_line(const char* msg) {
    Serial.println(msg);
    Serial0.println(msg);
}

void run_bringup_color_cycle() {
    display->fillScreen(UI::Colors::RED);
    delay(250);
    display->fillScreen(UI::Colors::GREEN);
    delay(250);
    display->fillScreen(UI::Colors::BLUE);
    delay(250);
    display->fillScreen(UI::Colors::WHITE);
    delay(250);
    display->fillScreen(UI::Colors::BLACK);
}

void backlight_pwm_step(uint8_t level_0_to_255, uint32_t step_ms) {
    if (level_0_to_255 == 0) {
        display->setBrightness(0);
        delay(step_ms);
        return;
    }
    if (level_0_to_255 >= 255) {
        display->setBrightness(255);
        delay(step_ms);
        return;
    }

    // Software PWM over CH422G BL enable line (no true analog PWM on this path).
    constexpr uint32_t kCycleUs = 2000;  // 500 Hz
    const uint32_t end_ms = millis() + step_ms;
    const uint32_t on_us = (kCycleUs * static_cast<uint32_t>(level_0_to_255)) / 255U;
    const uint32_t off_us = (on_us < kCycleUs) ? (kCycleUs - on_us) : 0;

    while (millis() < end_ms) {
        if (on_us > 0) {
            display->setBrightness(255);
            delayMicroseconds(on_us);
        }
        if (off_us > 0) {
            display->setBrightness(0);
            delayMicroseconds(off_us);
        }
    }
}

uint8_t brightness_from_progress(float progress_0_to_1) {
    if (progress_0_to_1 <= 0.0f) return 0;
    if (progress_0_to_1 >= 1.0f) return 255;

    // Cosine easing for visually smoother ramps.
    constexpr float kPi = 3.14159265f;
    const float eased = 0.5f - (0.5f * cosf(progress_0_to_1 * kPi));

    // Compensate low-end backlight dead-zone on CH422G BL gate.
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

void run_splash_sequence() {
    display->fillScreen(UI::Colors::BLACK);

    if (!SPIFFS.begin(true)) {
        log_line("splash: SPIFFS mount failed");
        delay(300);
        return;
    }

    const char* kSplashPath = "/BatteryEmulator_LCD.jpg";
    if (!SPIFFS.exists(kSplashPath)) {
        log_line("splash: image missing in SPIFFS");
        delay(300);
        return;
    }

    int jpg_w = 0;
    int jpg_h = 0;
    if (!read_jpeg_dimensions(SPIFFS, kSplashPath, jpg_w, jpg_h)) {
        log_line("splash: failed to parse JPEG dimensions");
        delay(300);
        return;
    }

    // Fill screen behavior: allow non-uniform scaling to exactly match panel size.
    const float scale_x = static_cast<float>(AppConfig::SCREEN_WIDTH) / static_cast<float>(jpg_w);
    const float scale_y = static_cast<float>(AppConfig::SCREEN_HEIGHT) / static_cast<float>(jpg_h);
    const int draw_x = 0;
    const int draw_y = 0;

    constexpr uint32_t kFadeMs = 2000;
    constexpr uint32_t kHoldMs = 2000;
    constexpr uint32_t kStepMs = 33;
    constexpr uint32_t kSteps = kFadeMs / kStepMs;

    display->fillScreen(UI::Colors::BLACK);
    display->setBrightness(0);
    display->drawJpgFile(SPIFFS, kSplashPath, draw_x, draw_y, 0, 0, 0, 0, scale_x, scale_y);

    // Fade in via backlight duty sweep: 0 -> 255 over 2s.
    for (uint32_t i = 0; i <= kSteps; ++i) {
        const float p = static_cast<float>(i) / static_cast<float>(kSteps);
        const uint8_t level = brightness_from_progress(p);
        backlight_pwm_step(level, kStepMs);
    }

    // Hold fully visible image for 2s.
    display->setBrightness(255);
    delay(kHoldMs);

    // Fade out via backlight duty sweep: 255 -> 0 over 2s.
    for (uint32_t i = 0; i <= kSteps; ++i) {
        const float p = static_cast<float>(i) / static_cast<float>(kSteps);
        const uint8_t level = static_cast<uint8_t>(255U - brightness_from_progress(p));
        backlight_pwm_step(level, kStepMs);
    }

    // Ensure the frame buffer is cleared right after fade-out completes.
    display->setBrightness(0);
    display->fillScreen(UI::Colors::BLACK);

    // Hold black for an extra second before showing the main widgets.
    delay(1000);

    display->setBrightness(255);
}

LEDColor led_color_from_index(uint8_t index) {
    switch (index % 5) {
        case 0: return LEDColor::GREEN;
        case 1: return LEDColor::BLUE;
        case 2: return LEDColor::ORANGE;
        case 3: return LEDColor::TEAL;
        default: return LEDColor::RED;
    }
}

LEDEffect led_effect_from_index(uint8_t index) {
    switch (index % 3) {
        case 0: return LEDEffect::CONTINUOUS;
        case 1: return LEDEffect::FLASH;
        default: return LEDEffect::HEARTBEAT;
    }
}

void draw_led(uint32_t now_ms) {
    const uint16_t led_color = led_widget.is_on(now_ms) ? led_widget.color565(now_ms) : UI::Colors::DIM_GRAY;
    display->fillCircle(UI::Layout::led_x(), UI::Layout::led_y(), AppConfig::LED_RADIUS, led_color);
}

void render_frame(uint32_t now_ms) {
    const auto& st = demo_model.state();

    soc_widget.draw(*display, UI::Layout::soc_x(), UI::Layout::soc_y(), st.soc_percent);
    power_bar_widget.draw(*display, UI::Layout::bar_center_x(), UI::Layout::bar_y(), st.power_w, now_ms);
    draw_led(now_ms);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial0.begin(115200);
    delay(800);
    log_line("");
    log_line("espnowreceiver_LCD: boot");
    log_line("boot: serial online (USB CDC + UART0)");

    // Defer LGFX construction until Arduino runtime is initialized.
    display = new lgfx_custom::LGFX_Waveshare7();
    log_line("boot: LGFX object constructed");

    log_line("boot: display.init() begin");
    const bool display_ok = display->init();
    Serial.printf("boot: display.init() result = %s\n", display_ok ? "ok" : "failed");
    Serial0.printf("boot: display.init() result = %s\n", display_ok ? "ok" : "failed");

    if (!display_ok) {
        log_line("FATAL: display initialization failed. Halting for diagnostics.");
        while (true) {
            delay(1000);
        }
    }

    display->setRotation(0);  // 0 = native landscape (800x480)
    log_line("boot: rotation set (landscape)");

    run_splash_sequence();
    log_line("boot: splash sequence done");

    led_widget.set_effect(LEDEffect::PULSE);
    led_widget.set_color(LEDColor::GREEN);

    log_line("Phase 1 complete: panel initialized.");
    log_line("Phase 2/3/4 running: relative layout + widgets + demo model.");
}

void loop() {
    const uint32_t now = millis();

    if (now - last_state_ms >= AppConfig::STATE_UPDATE_MS) {
        demo_model.update(now);
        last_state_ms = now;
    }

    if (now - last_frame_ms >= AppConfig::FRAME_INTERVAL_MS) {
        render_frame(now);
        last_frame_ms = now;
    }

    if (now - last_log_ms >= 1000) {
        Serial.printf("alive: ms=%lu soc=%.1f power=%ld\n", static_cast<unsigned long>(now), demo_model.state().soc_percent, static_cast<long>(demo_model.state().power_w));
        Serial0.printf("alive: ms=%lu soc=%.1f power=%ld\n", static_cast<unsigned long>(now), demo_model.state().soc_percent, static_cast<long>(demo_model.state().power_w));
        last_log_ms = now;
    }
}
