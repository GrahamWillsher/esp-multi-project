/**
 * @file tft_display.cpp
 * @brief TFT Display Implementation
 * 
 * Pure TFT-eSPI rendering with synchronous, blocking operations.
 * Extracted from original working code, wrapped in TftDisplay class.
 */

#ifdef USE_TFT

#include "tft_display.h"
#include "../../common.h"
#include "../../helpers.h"
#include "../../hal/hardware_config.h"
#include "../layout/display_layout_spec.h"
#include <LittleFS.h>
#include <JPEGDecoder.h>
#include <esp_heap_caps.h>
#include <TFT_eSPI.h>
#include <algorithm>
#include <cmath>
#include <cstring>

// Use global TFT_eSPI instance from globals.cpp
extern TFT_eSPI tft;

// Font declarations (provided by TFT_eSPI)
extern const GFXfont FreeSansBold18pt7b;
extern const GFXfont FreeSansBold12pt7b;
extern const GFXfont FreeSansBold9pt7b;

namespace {

bool read_file_to_buffer(const char* path, uint8_t*& out_buffer, size_t& out_size) {
    out_buffer = nullptr;
    out_size = 0;

    File file = LittleFS.open(path, "r");
    if (!file) {
        LOG_ERROR("TFT", "Cannot open file: %s", path);
        return false;
    }

    const size_t file_size = file.size();
    if (file_size == 0 || file_size > Display::LayoutSpec::Assets::MAX_SPLASH_JPEG_BYTES) {
        LOG_ERROR("TFT", "Invalid splash JPEG size: %u bytes", (unsigned)file_size);
        file.close();
        return false;
    }

    // Large runtime buffers should prefer PSRAM, with internal RAM fallback.
    out_buffer = static_cast<uint8_t*>(
        heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!out_buffer) {
        out_buffer = static_cast<uint8_t*>(
            heap_caps_malloc(file_size, MALLOC_CAP_8BIT));
    }

    if (!out_buffer) {
        LOG_ERROR("TFT", "Buffer allocation failed for %s (%u bytes)",
                  path, (unsigned)file_size);
        file.close();
        return false;
    }

    if (file.read(out_buffer, file_size) != file_size) {
        LOG_ERROR("TFT", "Short read: %s", path);
        heap_caps_free(out_buffer);
        out_buffer = nullptr;
        file.close();
        return false;
    }

    file.close();
    out_size = file_size;
    return true;
}

}  // namespace

namespace Display {

// ============================================================================
// Constructor
// ============================================================================

TftDisplay::TftDisplay() {
    LOG_DEBUG("TFT", "TftDisplay constructor");
}

// ============================================================================
// IDisplay Implementation - Initialization
// ============================================================================

bool TftDisplay::init() {
    LOG_INFO("TFT", "Initializing TFT display...");
    
    try {
        init_hardware();
        init_backlight();
        LOG_INFO("TFT", "TFT display initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("TFT", "Failed to initialize TFT: %s", e.what());
        return false;
    }
}

void TftDisplay::init_hardware() {
    LOG_DEBUG("TFT", "Initializing TFT hardware...");

    // Keep backlight hard-OFF before any panel init to avoid white flash blocks
    pinMode(HardwareConfig::GPIO_BACKLIGHT, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_BACKLIGHT, LOW);
    smart_delay(LayoutSpec::Timing::BACKLIGHT_INIT_DELAY_MS);

    // Enable panel power first (required on T-Display-S3)
    pinMode(HardwareConfig::GPIO_DISPLAY_POWER, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_DISPLAY_POWER, HIGH);
    smart_delay(LayoutSpec::Timing::PANEL_POWER_ENABLE_DELAY_MS);
    LOG_DEBUG("TFT", "Display power enabled (GPIO %d)",
              HardwareConfig::GPIO_DISPLAY_POWER);
    
    tft.init();
    tft.setRotation(1);  // Landscape mode
    // DON'T fillScreen here - let splash be first thing drawn to avoid white flash
    tft.setSwapBytes(true);
    
    LOG_DEBUG("TFT", "TFT hardware initialized");
}

void TftDisplay::init_backlight() {
    LOG_DEBUG("TFT", "Initializing backlight PWM...");
    
    // Configure backlight PWM
    pinMode(HardwareConfig::GPIO_BACKLIGHT, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_BACKLIGHT, LOW);  // Keep OFF through PWM attach
    
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    // ESP-IDF v4.x API
    ledcSetup(HardwareConfig::BACKLIGHT_PWM_CHANNEL, 
              HardwareConfig::BACKLIGHT_FREQUENCY_HZ, 
              HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
    ledcAttachPin(HardwareConfig::GPIO_BACKLIGHT, 
                  HardwareConfig::BACKLIGHT_PWM_CHANNEL);
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, 0);
    
    #else
    // ESP-IDF v5.x API
    ledcAttach(HardwareConfig::GPIO_BACKLIGHT, 
               HardwareConfig::BACKLIGHT_FREQUENCY_HZ, 
               HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
    ledcWrite(HardwareConfig::GPIO_BACKLIGHT, 0);
    #endif
    
    LOG_DEBUG("TFT", "Backlight PWM initialized (GPIO %d, %d Hz, %d-bit)",
              HardwareConfig::GPIO_BACKLIGHT,
              HardwareConfig::BACKLIGHT_FREQUENCY_HZ,
              HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
}

// ============================================================================
// IDisplay Implementation - Splash Screen
// ============================================================================

void TftDisplay::display_splash_with_fade() {
    LOG_INFO("TFT", "=== Splash START ===");
    
    // 1. Turn off backlight
    set_backlight(0);
    
    // 2. Load and display splash image
    load_and_draw_splash();
    
    // 3. Fade in backlight (0 to 255 over 2 seconds)
    animate_backlight(255, LayoutSpec::Timing::SPLASH_FADE_IN_MS);
    
    // 4. Hold splash for 2 seconds
    smart_delay(LayoutSpec::Timing::SPLASH_HOLD_MS);
    
    // 5. Fade out backlight (255 to 0 over 2 seconds)
    animate_backlight(0, LayoutSpec::Timing::SPLASH_FADE_OUT_MS);
    
    LOG_INFO("TFT", "=== Splash END ===");
}

void TftDisplay::display_initial_screen() {
    LOG_INFO("TFT", "Displaying Ready screen...");
    
    // Clear screen and display "Ready" text
    tft.fillScreen(TFT_BLACK);
    set_text_style_ready(TFT_GREEN);
    tft.drawString("Ready", LayoutSpec::Layout::READY_TEXT_X, LayoutSpec::Layout::READY_TEXT_Y);
    
    // Fade in backlight to full brightness
    animate_backlight(255, LayoutSpec::Timing::READY_FADE_IN_MS);
}

// ============================================================================
// IDisplay Implementation - Data Updates
// ============================================================================

void TftDisplay::update_soc(float soc_percent) {
    LOG_DEBUG("TFT", "Updating SOC: %.1f%%", soc_percent);
    draw_soc(soc_percent);
}

void TftDisplay::update_power(int32_t power_w) {
    LOG_DEBUG("TFT", "Updating Power: %ld W", power_w);
    draw_power(power_w);
}

void TftDisplay::show_status_page() {
    LOG_INFO("TFT", "Showing status page...");
    
    // TODO: Implement full status page display
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Status Page", 100, 10);
}

// ============================================================================
// IDisplay Implementation - Error Display
// ============================================================================

void TftDisplay::show_error_state() {
    LOG_WARN("TFT", "Showing error state");

    setup_error_screen();
    tft.drawString("ERROR", LayoutSpec::Layout::ERROR_TEXT_X, LayoutSpec::Layout::ERROR_TEXT_Y);
}

void TftDisplay::show_fatal_error(const char* component, const char* message) {
    LOG_ERROR("TFT", "Showing fatal error: [%s] %s", component, message);

    setup_error_screen();
    tft.drawString("FATAL ERROR", LayoutSpec::Layout::FATAL_ERROR_TEXT_X,
                   LayoutSpec::Layout::FATAL_ERROR_TITLE_Y);
    tft.drawString(component ? component : "", LayoutSpec::Layout::FATAL_ERROR_LEFT_MARGIN,
                   LayoutSpec::Layout::FATAL_ERROR_COMPONENT_Y);
    tft.drawString(message ? message : "", LayoutSpec::Layout::FATAL_ERROR_LEFT_MARGIN,
                   LayoutSpec::Layout::FATAL_ERROR_MESSAGE_Y);
}

// ============================================================================
// Helper Methods
// ============================================================================

void TftDisplay::set_backlight(uint8_t brightness) {
    current_backlight_ = brightness;
    
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, brightness);
    #else
    ledcWrite(HardwareConfig::GPIO_BACKLIGHT, brightness);
    #endif

    log_backlight_if_significant(brightness, last_backlight_logged_);
}

void TftDisplay::log_backlight_if_significant(uint8_t brightness, int16_t& last_logged) {
    constexpr int SIGNIFICANT_DELTA = 50;
    constexpr uint8_t MIN_BRIGHTNESS = 0;
    constexpr uint8_t MAX_BRIGHTNESS = 255;

    const bool is_boundary = (brightness == MIN_BRIGHTNESS || brightness == MAX_BRIGHTNESS);
    const bool is_initial = (last_logged < 0);
    const bool is_significant_change =
        (std::abs((int)brightness - last_logged) >= SIGNIFICANT_DELTA);

    if (is_boundary || is_initial || is_significant_change) {
        LOG_DEBUG("TFT", "Backlight PWM write: brightness=%u", (unsigned)brightness);
        last_logged = brightness;
    }
}

void TftDisplay::animate_backlight(uint8_t target, uint32_t duration_ms) {
    if (duration_ms == 0) {
        set_backlight(target);
        return;
    }

    LOG_DEBUG("TFT", "Animating backlight to %u over %u ms", target, duration_ms);

    // Calculate steps for smooth animation (~16ms per frame = ~60 FPS)
    const uint32_t frame_time = LayoutSpec::Timing::ANIMATION_FRAME_TIME_MS;
    const uint32_t steps = (duration_ms + frame_time - 1) / frame_time;

    // IMPORTANT: Capture start brightness once.
    // Re-reading current_backlight_ every iteration causes non-linear progression
    // and visible jump in the final segment.
    const int32_t start = current_backlight_;
    const int32_t delta = (int32_t)target - start;

    for (uint32_t i = 0; i <= steps; i++) {
        // Integer interpolation with rounding for stable monotonic fade.
        int32_t brightness_i = start + (delta * (int32_t)i + (int32_t)(steps / 2)) / (int32_t)steps;
        if (brightness_i < 0) brightness_i = 0;
        if (brightness_i > 255) brightness_i = 255;
        const uint8_t brightness = (uint8_t)brightness_i;

        set_backlight(brightness);

        if (i < steps) {
            smart_delay(frame_time);
        }
    }
}

void TftDisplay::show_splash_fallback(const char* reason) {
    if (reason) {
        LOG_WARN("TFT", "Splash fallback: %s", reason);
    }

    tft.fillScreen(TFT_BLACK);
    set_text_style_ready();
    tft.drawString(LayoutSpec::Assets::SPLASH_FALLBACK_TEXT,
                   LayoutSpec::Layout::READY_TEXT_X,
                   LayoutSpec::Layout::READY_TEXT_Y);
}

void TftDisplay::setup_error_screen() {
    tft.fillScreen(TFT_RED);
    set_text_style_error();
}

void TftDisplay::load_and_draw_splash() {
    LOG_INFO("TFT", "Loading splash image...");
    
    // Clear screen to black FIRST with backlight still off
    tft.fillScreen(TFT_BLACK);
    
    const char* splash_path = LayoutSpec::Assets::SPLASH_IMAGE_PATH;
    if (!LittleFS.exists(splash_path)) {
        show_splash_fallback("File not found");
        return;
    }

    uint8_t* raw = nullptr;
    size_t file_sz = 0;
    if (!read_file_to_buffer(splash_path, raw, file_sz)) {
        show_splash_fallback("Cannot read splash file");
        return;
    }

    LOG_INFO("TFT", "JPEG size: %u bytes", (unsigned)file_sz);

    const uint32_t jpeg_decode_start_time = millis();
    if (!JpegDec.decodeArray(raw, file_sz)) {
        LOG_ERROR("TFT", "JPEG decode failed: %s", splash_path);
        heap_caps_free(raw);
        show_splash_fallback("Cannot decode JPEG");
        return;
    }
    heap_caps_free(raw);

    const uint16_t img_w = JpegDec.width;
    const uint16_t img_h = JpegDec.height;
    LOG_INFO("TFT", "Splash image decoded: %ux%u", img_w, img_h);
    LOG_INFO("TFT", "JPEG decoded in %u ms", (unsigned)(millis() - jpeg_decode_start_time));

    const int16_t x_offset = (Display::SCREEN_WIDTH - img_w) / 2;
    const int16_t y_offset = (Display::SCREEN_HEIGHT - img_h) / 2;

    const uint16_t mcu_w = JpegDec.MCUWidth;
    const uint16_t mcu_h = JpegDec.MCUHeight;

    while (JpegDec.read()) {
        const uint16_t* pImg = JpegDec.pImage;
        const int16_t mcu_x = (JpegDec.MCUx * mcu_w) + x_offset;
        const int16_t mcu_y = (JpegDec.MCUy * mcu_h) + y_offset;

        const uint16_t win_w = (mcu_x + mcu_w <= x_offset + img_w)
            ? mcu_w
            : (x_offset + img_w - mcu_x);
        const uint16_t win_h = (mcu_y + mcu_h <= y_offset + img_h)
            ? mcu_h
            : (y_offset + img_h - mcu_y);

        if (win_w > 0 && win_h > 0) {
            tft.pushImage(mcu_x, mcu_y, win_w, win_h, pImg);
        }
    }

    LOG_INFO("TFT", "Splash image displayed at (%d, %d)", x_offset, y_offset);
}

/**
 * Helper: Decode JPEG from LittleFS to RGB565 buffer
 * Returns heap_caps_malloc'd buffer. Caller must heap_caps_free() it.
 * Returns nullptr on failure; out_w / out_h are set to 0.
 */
static uint16_t* decode_jpg_to_rgb565(const char* path,
                                      uint16_t& out_w, uint16_t& out_h) {
    out_w = 0;
    out_h = 0;

    if (!LittleFS.exists(path)) {
        LOG_ERROR("TFT", "File not found: %s", path);
        return nullptr;
    }

    File f = LittleFS.open(path, "r");
    if (!f) {
        LOG_ERROR("TFT", "Cannot open: %s", path);
        return nullptr;
    }

    const size_t file_sz = f.size();
    LOG_INFO("TFT", "JPEG size: %u bytes", (unsigned)file_sz);

    // Read JPEG file into buffer
    uint8_t* raw = static_cast<uint8_t*>(
        heap_caps_malloc(file_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!raw) {
        raw = static_cast<uint8_t*>(heap_caps_malloc(file_sz, MALLOC_CAP_8BIT));
        if (!raw) {
            LOG_ERROR("TFT", "JPEG source allocation failed (%u bytes)", (unsigned)file_sz);
            f.close();
            return nullptr;
        }
    }
    if (f.read(raw, file_sz) != file_sz) {
        LOG_ERROR("TFT", "Short read: %s", path);
        heap_caps_free(raw);
        f.close();
        return nullptr;
    }
    f.close();

    // Decode JPEG
    const uint32_t t0 = millis();
    if (!JpegDec.decodeArray(raw, file_sz)) {
        LOG_ERROR("TFT", "JPEG decode failed: %s", path);
        heap_caps_free(raw);
        return nullptr;
    }
    heap_caps_free(raw);

    out_w = JpegDec.width;
    out_h = JpegDec.height;
    LOG_INFO("TFT", "JPEG decoded in %u ms: %ux%u", 
             (unsigned)(millis() - t0), (unsigned)out_w, (unsigned)out_h);

    // Allocate RGB565 buffer
    const size_t num_px = (size_t)out_w * out_h;
    uint16_t* buf = (uint16_t*)heap_caps_malloc(
        num_px * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        LOG_ERROR("TFT", "PSRAM malloc failed for pixel buffer (%u bytes)",
                  (unsigned)(num_px * sizeof(uint16_t)));
        out_w = 0; out_h = 0;
        return nullptr;
    }

    // Copy MCU blocks from JPEGDecoder to contiguous buffer
    while (JpegDec.read()) {
        const uint16_t  mcu_w = JpegDec.MCUWidth;
        const uint16_t  mcu_h = JpegDec.MCUHeight;
        const uint32_t  x0    = (uint32_t)JpegDec.MCUx * mcu_w;
        const uint32_t  y0    = (uint32_t)JpegDec.MCUy * mcu_h;
        const uint16_t* src   = JpegDec.pImage;
        
        for (uint16_t dy = 0; dy < mcu_h; dy++) {
            if (y0 + dy >= out_h) break;
            for (uint16_t dx = 0; dx < mcu_w; dx++) {
                if (x0 + dx >= out_w) break;
                buf[(y0 + dy) * out_w + (x0 + dx)] = src[dy * mcu_w + dx];
            }
        }
    }

    LOG_INFO("TFT", "JPEG pixel buffer ready: 0x%08X", (uint32_t)buf);
    return buf;
}

void TftDisplay::draw_soc(float soc_percent) {
    // Initialize SOC color gradient on first call
    if (!soc_gradient_initialized_) {
        if (!Display::soc_gradient_initialized) {
            pre_calculate_color_gradient(TFT_RED, Display::AMBER, 167, &Display::soc_color_gradient[0]);
            pre_calculate_color_gradient(Display::AMBER, Display::LIME, 167, &Display::soc_color_gradient[167]);
            pre_calculate_color_gradient(Display::LIME, TFT_GREEN, 166, &Display::soc_color_gradient[334]);
            Display::soc_gradient_initialized = true;
            LOG_DEBUG("TFT", "SOC color gradient initialized (500 steps: RED→AMBER→LIME→GREEN)");
        }
        soc_gradient_initialized_ = true;
    }

    // SOC centered in top two-thirds of the screen
    const int socCenterX = LayoutSpec::Layout::SOC_CENTER_X;
    const int socCenterY = LayoutSpec::Layout::SOC_CENTER_Y;

    // Build SOC string with 1 decimal place
    char socText[16];
    snprintf(socText, sizeof(socText), "%.1f%%", soc_percent);

    // Skip if unchanged
    if (strcmp(soc_text_buffer_, socText) == 0) {
        return;
    }

    // Clear SOC area in upper region
    clear_rect(LayoutSpec::Layout::SOC_CLEAR_LEFT,
               LayoutSpec::Layout::SOC_CLEAR_TOP,
               LayoutSpec::Layout::SOC_CLEAR_WIDTH,
               LayoutSpec::Layout::SOC_CLEAR_HEIGHT);

    // Color from SOC gradient - map SOC percentage (0-100) to gradient index (0-500)
    uint16_t socColor = TFT_WHITE;
    int gradient_index = (int)((soc_percent / 100.0f) * Display::TOTAL_GRADIENT_STEPS);
    if (gradient_index < 0) gradient_index = 0;
    if (gradient_index > Display::TOTAL_GRADIENT_STEPS) gradient_index = Display::TOTAL_GRADIENT_STEPS;
    socColor = Display::soc_color_gradient[gradient_index];
    
    LOG_DEBUG("TFT", "SOC: %.1f%% → gradient_index=%d → color=0x%04X", 
              soc_percent, gradient_index, socColor);

    set_text_style_soc();
    tft.setTextColor(socColor, TFT_BLACK);
    tft.drawString(socText, socCenterX, socCenterY);

    strncpy(soc_text_buffer_, socText, sizeof(soc_text_buffer_) - 1);
    soc_text_buffer_[sizeof(soc_text_buffer_) - 1] = '\0';
}

void TftDisplay::init_power_bar_state(int& bar_char_width,
                                    int& max_bars_per_side,
                                    uint16_t* gradient_green,
                                    uint16_t* gradient_red) {
    set_text_style_power_bar();
    bar_char_width = tft.textWidth("-");

    max_bars_per_side = (Display::SCREEN_WIDTH / 2) / bar_char_width;
    if (max_bars_per_side > LayoutSpec::PowerBar::MAX_BARS_PER_SIDE) {
        max_bars_per_side = LayoutSpec::PowerBar::MAX_BARS_PER_SIDE;
    }

    pre_calculate_color_gradient(TFT_BLUE, TFT_GREEN, max_bars_per_side - 1, gradient_green);
    pre_calculate_color_gradient(TFT_BLUE, TFT_RED, max_bars_per_side - 1, gradient_red);
}

int TftDisplay::calculate_power_bar_count(int32_t clamped_power,
                                          int max_bars_per_side,
                                          int32_t max_power) const {
    int bars = (std::abs(clamped_power) * max_bars_per_side) / max_power;
    if (bars > max_bars_per_side) {
        bars = max_bars_per_side;
    }
    if (clamped_power != 0 && bars == 0) {
        bars = 1;
    }
    return bars;
}

bool TftDisplay::should_pulse_animate(int signed_bars,
                                      int previous_signed_bars) const {
    const bool same_direction = (previous_signed_bars < 0 && signed_bars < 0) ||
                                (previous_signed_bars > 0 && signed_bars > 0);
    return same_direction && (std::abs(previous_signed_bars) == std::abs(signed_bars));
}

void TftDisplay::draw_power_bars(int bars,
                                 bool charging,
                                 int center_x,
                                 int bar_y,
                                 int bar_char_width,
                                 const uint16_t* gradient_green,
                                 const uint16_t* gradient_red,
                                 int ripple_pos) {
    for (int i = 0; i < bars; i++) {
        const int bar_x = charging
            ? (center_x - i * bar_char_width)
            : (center_x + i * bar_char_width);

        const uint16_t bar_color = charging ? gradient_green[i] : gradient_red[i];
        const uint16_t display_color = (ripple_pos >= 0 && i == ripple_pos)
            ? ((bar_color >> 1) & 0x7BEF)
            : bar_color;

        tft.setTextColor(display_color, Display::tft_background);
        tft.drawString("-", bar_x, bar_y);
    }
}

void TftDisplay::clear_power_bar_residuals(int bars,
                                           bool charging,
                                           int previous_signed_bars,
                                           int center_x,
                                           int bar_y,
                                           int bar_char_width) {
    const int previous_abs = std::abs(previous_signed_bars);
    const bool previous_negative = previous_signed_bars < 0;

    if (charging) {
        // Clear right side if previously discharging.
        if (previous_abs > 0 && !previous_negative) {
            clear_rect(center_x,
                       bar_y - LayoutSpec::PowerBar::BAR_CLEAR_TOP_OFFSET,
                       previous_abs * bar_char_width,
                       LayoutSpec::PowerBar::BAR_CLEAR_HEIGHT);
        }

        // Clear extra left bars if charging power decreased.
        if (previous_negative && bars < previous_abs) {
            clear_rect(center_x - previous_abs * bar_char_width,
                       bar_y - LayoutSpec::PowerBar::BAR_CLEAR_TOP_OFFSET,
                       (previous_abs - bars) * bar_char_width,
                       LayoutSpec::PowerBar::BAR_CLEAR_HEIGHT);
        }
    } else {
        // Clear left side if previously charging.
        if (previous_abs > 0 && previous_negative) {
            clear_rect(center_x - previous_abs * bar_char_width,
                       bar_y - LayoutSpec::PowerBar::BAR_CLEAR_TOP_OFFSET,
                       previous_abs * bar_char_width,
                       LayoutSpec::PowerBar::BAR_CLEAR_HEIGHT);
        }

        // Clear extra right bars if discharging power decreased.
        if (!previous_negative && bars < previous_abs) {
            clear_rect(center_x + bars * bar_char_width,
                       bar_y - LayoutSpec::PowerBar::BAR_CLEAR_TOP_OFFSET,
                       (previous_abs - bars) * bar_char_width,
                       LayoutSpec::PowerBar::BAR_CLEAR_HEIGHT);
        }
    }
}

void TftDisplay::draw_zero_power_marker(int center_x,
                                        int bar_y,
                                        int max_bars_per_side,
                                        int bar_char_width) {
    clear_rect(center_x - (max_bars_per_side * bar_char_width),
               bar_y - LayoutSpec::PowerBar::BAR_CLEAR_TOP_OFFSET,
               max_bars_per_side * 2 * bar_char_width,
               LayoutSpec::PowerBar::BAR_CLEAR_HEIGHT);

    tft.setTextColor(TFT_BLUE, Display::tft_background);
    tft.drawString("-", center_x, bar_y);
}

void TftDisplay::draw_power_text_if_changed(int32_t power_w,
                                            int center_x,
                                            int text_y,
                                            int32_t& last_power_text) {
    if (last_power_text == power_w) {
        return;
    }

    clear_rect(center_x - LayoutSpec::PowerBar::TEXT_BOX_LEFT_OFFSET,
               text_y - LayoutSpec::PowerBar::TEXT_BOX_TOP_OFFSET,
               LayoutSpec::PowerBar::TEXT_BOX_WIDTH,
               LayoutSpec::PowerBar::TEXT_BOX_HEIGHT);

    set_text_style_power_value();

    char power_str[16];
    snprintf(power_str, sizeof(power_str), "%ldW", (long)power_w);
    tft.drawString(power_str, center_x, text_y);
    last_power_text = power_w;
}

// ============================================================================
// Text Style Helper Methods
// ============================================================================

void TftDisplay::set_text_style_soc() {
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.setTextSize(LayoutSpec::Text::SOC_TEXT_SIZE);
    tft.setTextDatum(MC_DATUM);
}

void TftDisplay::set_text_style_power_bar() {
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextSize(LayoutSpec::Text::POWER_BAR_TEXT_SIZE);
    tft.setTextDatum(MC_DATUM);
}

void TftDisplay::set_text_style_power_value() {
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextSize(LayoutSpec::Text::POWER_VALUE_TEXT_SIZE);
    tft.setTextColor(TFT_WHITE, Display::tft_background);
    tft.setTextDatum(BC_DATUM);
}

void TftDisplay::set_text_style_ready(uint16_t color) {
    tft.setTextColor(color);
    tft.setTextSize(LayoutSpec::Text::READY_TEXT_SIZE);
    tft.setTextDatum(MC_DATUM);
}

void TftDisplay::set_text_style_error() {
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(LayoutSpec::Text::READY_TEXT_SIZE);
    tft.setTextDatum(MC_DATUM);
}

// ============================================================================
// Rectangle Clearing Helper
// ============================================================================

void TftDisplay::clear_rect(int x, int y, int w, int h, uint16_t color) {
    if (color == 0) {
        color = Display::tft_background;
    }
    tft.fillRect(x, y, w, h, color);
}

void TftDisplay::draw_power(int32_t power_w) {
    if (!power_bar_initialized_) {
        init_power_bar_state(
            power_bar_char_width_,
            power_bar_max_bars_per_side_,
            power_bar_gradient_green_,
            power_bar_gradient_red_
        );
        power_bar_initialized_ = true;
    }

    const int center_x = Display::SCREEN_WIDTH / 2;
    const int bar_y = LayoutSpec::PowerBar::BAR_Y;
    const int text_y = LayoutSpec::PowerBar::TEXT_Y;
    const int32_t max_power = LayoutSpec::PowerBar::MAX_POWER_W;

    int32_t clamped_power = power_w;
    if (clamped_power < -max_power) clamped_power = -max_power;
    if (clamped_power > max_power) clamped_power = max_power;
    const int bars = calculate_power_bar_count(clamped_power, power_bar_max_bars_per_side_, max_power);
    const bool charging = (clamped_power < 0);
    const int signed_bars = charging ? -bars : bars;

    set_text_style_power_bar();

    const bool pulse = (bars > 0) && should_pulse_animate(signed_bars, power_bar_previous_signed_bars_);
    if (pulse) {
        for (int ripple_pos = 0; ripple_pos <= bars; ripple_pos++) {
            draw_power_bars(bars,
                            charging,
                            center_x,
                            bar_y,
                            power_bar_char_width_,
                            power_bar_gradient_green_,
                            power_bar_gradient_red_,
                            (ripple_pos < bars) ? ripple_pos : -1);
            if (ripple_pos < bars) {
                smart_delay(LayoutSpec::Timing::POWER_BAR_PULSE_DELAY_MS);
            }
        }
    } else if (bars == 0) {
        draw_zero_power_marker(center_x, bar_y, power_bar_max_bars_per_side_, power_bar_char_width_);
    } else {
        draw_power_bars(bars,
                        charging,
                        center_x,
                        bar_y,
                        power_bar_char_width_,
                        power_bar_gradient_green_,
                        power_bar_gradient_red_);
        clear_power_bar_residuals(bars,
                                  charging,
                                  power_bar_previous_signed_bars_,
                                  center_x,
                                  bar_y,
                                  power_bar_char_width_);
    }

    power_bar_previous_signed_bars_ = signed_bars;
    draw_power_text_if_changed(power_w, center_x, text_y, power_bar_last_power_text_);
}

} // namespace Display

#endif // USE_TFT
