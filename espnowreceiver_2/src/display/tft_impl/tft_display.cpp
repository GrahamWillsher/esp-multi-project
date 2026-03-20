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

float gamma_map_brightness(uint8_t logical_brightness) {
    if (logical_brightness <= Display::LayoutSpec::Backlight::PWM_MIN) {
        return static_cast<float>(Display::LayoutSpec::Backlight::PWM_MIN);
    }

    if (logical_brightness >= Display::LayoutSpec::Backlight::PWM_MAX) {
        return static_cast<float>(Display::LayoutSpec::Backlight::PWM_MAX);
    }

    const float normalized = static_cast<float>(logical_brightness) /
                             static_cast<float>(Display::LayoutSpec::Backlight::PWM_MAX);
    float gamma_mapped = std::pow(normalized, Display::LayoutSpec::Backlight::GAMMA);

    // Soft-knee near full brightness to avoid a visible last-step jump on this panel.
    // Blend from gamma curve toward linear in the top 10% range.
    constexpr float TOP_KNEE_START = 0.90f;
    if (normalized > TOP_KNEE_START) {
        const float t = (normalized - TOP_KNEE_START) / (1.0f - TOP_KNEE_START);
        gamma_mapped = (gamma_mapped * (1.0f - t)) + (normalized * t);
    }

    return gamma_mapped * static_cast<float>(Display::LayoutSpec::Backlight::PWM_MAX);
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

    // Gamma map to perceived-linear brightness, then apply temporal dithering
    // so fractional PWM values become smoother over successive frames.
    const float mapped_pwm = gamma_map_brightness(brightness);
    int32_t pwm_base = static_cast<int32_t>(mapped_pwm);
    float frac = mapped_pwm - static_cast<float>(pwm_base);

    if (brightness == Display::LayoutSpec::Backlight::PWM_MIN ||
        brightness == Display::LayoutSpec::Backlight::PWM_MAX) {
        backlight_dither_error_ = 0.0f;
    } else {
        backlight_dither_error_ += frac;
        if (backlight_dither_error_ >= 1.0f && pwm_base < Display::LayoutSpec::Backlight::PWM_MAX) {
            pwm_base += 1;
            backlight_dither_error_ -= 1.0f;
        }
    }

    if (pwm_base < Display::LayoutSpec::Backlight::PWM_MIN) {
        pwm_base = Display::LayoutSpec::Backlight::PWM_MIN;
    }
    if (pwm_base > Display::LayoutSpec::Backlight::PWM_MAX) {
        pwm_base = Display::LayoutSpec::Backlight::PWM_MAX;
    }

    const uint8_t pwm_brightness = static_cast<uint8_t>(pwm_base);
    
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, pwm_brightness);
    #else
    ledcWrite(HardwareConfig::GPIO_BACKLIGHT, pwm_brightness);
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

    const uint8_t start = current_backlight_;
    const uint32_t transitions = static_cast<uint32_t>(std::abs((int)target - (int)start));
    if (transitions == 0) {
        set_backlight(target);
        return;
    }

    // Use exact 1-count brightness transitions for deterministic fade shape.
    // For full-range fade (0->255), transitions=255 and delay≈duration/255.
    const uint32_t step_delay_ms = std::max<uint32_t>(1, (duration_ms + (transitions / 2U)) / transitions);
    const int step_dir = (target > start) ? 1 : -1;

    for (uint32_t i = 0; i < transitions; i++) {
        const int next = static_cast<int>(current_backlight_) + step_dir;
        set_backlight(static_cast<uint8_t>(next));
        smart_delay(step_delay_ms);
    }

    if (current_backlight_ != target) {
        set_backlight(target);
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

void TftDisplay::init_power_bar_state() {
    // Gradient index 0 = nearest to centre (blue), index N-1 = outermost.
    const int steps = LayoutSpec::PowerBar::SEGMENTS_PER_SIDE - 1;
    pre_calculate_color_gradient(TFT_BLUE, TFT_GREEN, steps, power_bar_gradient_green_);
    pre_calculate_color_gradient(TFT_BLUE, TFT_RED,   steps, power_bar_gradient_red_);
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
                                 const uint16_t* gradient_green,
                                 const uint16_t* gradient_red,
                                 int ripple_pos) {
    namespace PB = LayoutSpec::PowerBar;

    // Centre segment is always blue.
    tft.fillRect(PB::segment_x(PB::CENTER_SEGMENT_INDEX), PB::SEGMENT_Y_PX,
                 PB::SEGMENT_W_PX, PB::SEGMENT_H_PX, TFT_BLUE);

    // Side segments: i=0 is nearest to centre, i=bars-1 is outermost.
    for (int i = 0; i < bars; i++) {
        const int seg_idx = charging
            ? (PB::CENTER_SEGMENT_INDEX - 1 - i)
            : (PB::CENTER_SEGMENT_INDEX + 1 + i);

        const uint16_t base_color = charging ? gradient_green[i] : gradient_red[i];
        const uint16_t color = (ripple_pos >= 0 && i == ripple_pos)
            ? ((base_color >> 1) & 0x7BEFu)
            : base_color;

        tft.fillRect(PB::segment_x(seg_idx), PB::SEGMENT_Y_PX,
                     PB::SEGMENT_W_PX, PB::SEGMENT_H_PX, color);
    }
}

void TftDisplay::clear_power_bar_residuals(int bars,
                                           bool charging,
                                           int previous_signed_bars) {
    namespace PB = LayoutSpec::PowerBar;

    const int prev_abs     = std::abs(previous_signed_bars);
    const bool prev_charging = (previous_signed_bars < 0);

    if (prev_abs == 0) return;  // nothing was drawn before

    if (prev_charging != charging) {
        // Direction changed — clear the entire previous side.
        for (int i = 0; i < prev_abs; i++) {
            const int seg_idx = prev_charging
                ? (PB::CENTER_SEGMENT_INDEX - 1 - i)
                : (PB::CENTER_SEGMENT_INDEX + 1 + i);
            tft.fillRect(PB::segment_x(seg_idx), PB::SEGMENT_Y_PX,
                         PB::SEGMENT_W_PX, PB::SEGMENT_H_PX, Display::tft_background);
        }
    } else if (bars < prev_abs) {
        // Same direction but fewer bars — clear the excess segments only.
        for (int i = bars; i < prev_abs; i++) {
            const int seg_idx = charging
                ? (PB::CENTER_SEGMENT_INDEX - 1 - i)
                : (PB::CENTER_SEGMENT_INDEX + 1 + i);
            tft.fillRect(PB::segment_x(seg_idx), PB::SEGMENT_Y_PX,
                         PB::SEGMENT_W_PX, PB::SEGMENT_H_PX, Display::tft_background);
        }
    }
    // bars >= prev_abs same direction: new bars cover all old ones — nothing to clear.
}

void TftDisplay::draw_zero_power_marker() {
    namespace PB = LayoutSpec::PowerBar;
    tft.fillRect(PB::segment_x(PB::CENTER_SEGMENT_INDEX), PB::SEGMENT_Y_PX,
                 PB::SEGMENT_W_PX, PB::SEGMENT_H_PX, TFT_BLUE);
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
        init_power_bar_state();
        power_bar_initialized_ = true;
    }

    const int text_y      = LayoutSpec::PowerBar::TEXT_Y;
    const int32_t max_power = LayoutSpec::PowerBar::MAX_POWER_W;
    const int max_bars    = LayoutSpec::PowerBar::SEGMENTS_PER_SIDE;

    int32_t clamped_power = power_w;
    if (clamped_power < -max_power) clamped_power = -max_power;
    if (clamped_power > max_power)  clamped_power = max_power;
    const int  bars       = calculate_power_bar_count(clamped_power, max_bars, max_power);
    const bool charging   = (clamped_power < 0);
    const int  signed_bars = charging ? -bars : bars;

    const bool pulse = (bars > 0) && should_pulse_animate(signed_bars, power_bar_previous_signed_bars_);
    if (pulse) {
        for (int ripple_pos = 0; ripple_pos <= bars; ripple_pos++) {
            draw_power_bars(bars, charging,
                            power_bar_gradient_green_, power_bar_gradient_red_,
                            (ripple_pos < bars) ? ripple_pos : -1);
            if (ripple_pos < bars) {
                smart_delay(LayoutSpec::Timing::POWER_BAR_PULSE_DELAY_MS);
            }
        }
    } else if (bars == 0) {
        clear_power_bar_residuals(0, charging, power_bar_previous_signed_bars_);
        draw_zero_power_marker();
    } else {
        clear_power_bar_residuals(bars, charging, power_bar_previous_signed_bars_);
        draw_power_bars(bars, charging, power_bar_gradient_green_, power_bar_gradient_red_);
    }

    power_bar_previous_signed_bars_ = signed_bars;
    draw_power_text_if_changed(power_w, Display::SCREEN_WIDTH / 2, text_y, power_bar_last_power_text_);
}

} // namespace Display

#endif // USE_TFT
