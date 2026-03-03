/**
 * @file tft_espi_display_driver.cpp
 * @brief Implementation of TFT_eSPI display HAL
 */

#include "tft_espi_display_driver.h"
#include <logging.h>
#include "../hardware_config.h"

namespace HAL {

TftEspiDisplayDriver::TftEspiDisplayDriver(TFT_eSPI& tft)
    : tft_(tft)
    , initialized_(false) {
}

bool TftEspiDisplayDriver::init() {
    if (initialized_) {
        LOG_WARN("Display already initialized");
        return true;
    }
    
    try {
        tft_.init();
        
        // CRITICAL: TFT_eSPI init() may turn on backlight, so force it OFF immediately
        // to prevent white flash before splash screen
        pinMode(HardwareConfig::GPIO_BACKLIGHT, OUTPUT);
        digitalWrite(HardwareConfig::GPIO_BACKLIGHT, LOW);
        
        // CRITICAL: Set swap bytes for correct color rendering on T-Display-S3
        // Must be called AFTER init() as init() may reset this setting
        tft_.setSwapBytes(true);
        
        initialized_ = true;
        LOG_INFO("TFT_eSPI display initialized successfully (swap_bytes=true, backlight OFF)");
        return true;
    } catch (...) {
        LOG_ERROR("Failed to initialize TFT_eSPI display");
        return false;
    }
}

bool TftEspiDisplayDriver::is_available() const {
    return initialized_;
}

void TftEspiDisplayDriver::set_rotation(uint8_t rotation) {
    if (!initialized_) {
        LOG_ERROR("Cannot set rotation - display not initialized");
        return;
    }
    tft_.setRotation(rotation);
}

uint16_t TftEspiDisplayDriver::get_width() const {
    return initialized_ ? tft_.width() : 0;
}

uint16_t TftEspiDisplayDriver::get_height() const {
    return initialized_ ? tft_.height() : 0;
}

void TftEspiDisplayDriver::fill_screen(uint16_t color) {
    if (!initialized_) return;
    tft_.fillScreen(color);
}

void TftEspiDisplayDriver::draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
    if (!initialized_) return;
    tft_.drawPixel(x, y, color);
}

void TftEspiDisplayDriver::draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (!initialized_) return;
    tft_.drawRect(x, y, w, h, color);
}

void TftEspiDisplayDriver::fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (!initialized_) return;
    tft_.fillRect(x, y, w, h, color);
}

void TftEspiDisplayDriver::draw_circle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color) {
    if (!initialized_) return;
    tft_.drawCircle(x, y, radius, color);
}

void TftEspiDisplayDriver::fill_circle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color) {
    if (!initialized_) return;
    tft_.fillCircle(x, y, radius, color);
}

void TftEspiDisplayDriver::set_text_color(uint16_t fg, uint16_t bg) {
    if (!initialized_) return;
    tft_.setTextColor(fg, bg);
}

void TftEspiDisplayDriver::set_cursor(uint16_t x, uint16_t y) {
    if (!initialized_) return;
    tft_.setCursor(x, y);
}

void TftEspiDisplayDriver::set_text_size(uint8_t size) {
    if (!initialized_) return;
    tft_.setTextSize(size);
}

void TftEspiDisplayDriver::print(const char* text) {
    if (!initialized_ || !text) return;
    tft_.print(text);
}

void TftEspiDisplayDriver::println(const char* text) {
    if (!initialized_ || !text) return;
    tft_.println(text);
}

void TftEspiDisplayDriver::draw_string(const char* text, uint16_t x, uint16_t y, uint16_t color) {
    if (!initialized_ || !text) return;
    tft_.setTextColor(color);
    tft_.setCursor(x, y);
    tft_.print(text);
}

uint16_t TftEspiDisplayDriver::text_width(const char* text) {
    if (!initialized_ || !text) return 0;
    return tft_.textWidth(text);
}

uint16_t TftEspiDisplayDriver::text_height() const {
    if (!initialized_) return 0;
    return tft_.fontHeight();
}

void TftEspiDisplayDriver::update() {
    // TFT_eSPI writes directly to hardware - no buffering needed
    // This method is a no-op but required by interface
}

} // namespace HAL
