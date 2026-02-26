#pragma once

/**
 * @file mock_display_driver.h
 * @brief Mock display driver for testing
 * 
 * Provides a no-op display implementation for:
 * - Unit testing without hardware
 * - CI/CD pipelines
 * - Headless operation modes
 */

#include "idisplay_driver.h"
#include <cstring>

namespace HAL {

/**
 * @brief Mock display driver (no-op implementation)
 * 
 * All operations are logged but not rendered. Useful for:
 * - Testing display logic without hardware
 * - Running on systems without displays
 * - Debugging display rendering sequence
 */
class MockDisplayDriver : public IDisplayDriver {
public:
    MockDisplayDriver(uint16_t width = 320, uint16_t height = 170)
        : width_(width)
        , height_(height)
        , initialized_(false)
        , rotation_(0)
        , text_size_(1)
        , cursor_x_(0)
        , cursor_y_(0)
        , fg_color_(0xFFFF)
        , bg_color_(0x0000) {
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // IDisplayDriver Interface Implementation
    // ═══════════════════════════════════════════════════════════════════════
    
    bool init() override {
        initialized_ = true;
        return true;
    }
    
    bool is_available() const override {
        return initialized_;
    }
    
    void set_rotation(uint8_t rotation) override {
        rotation_ = rotation;
        // Swap width/height for 90/270 degree rotations
        if (rotation == 1 || rotation == 3) {
            uint16_t temp = width_;
            width_ = height_;
            height_ = temp;
        }
    }
    
    uint16_t get_width() const override {
        return width_;
    }
    
    uint16_t get_height() const override {
        return height_;
    }
    
    void fill_screen(uint16_t color) override {
        (void)color; // Mock - no rendering
    }
    
    void draw_pixel(uint16_t x, uint16_t y, uint16_t color) override {
        (void)x; (void)y; (void)color; // Mock - no rendering
    }
    
    void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) override {
        (void)x; (void)y; (void)w; (void)h; (void)color; // Mock - no rendering
    }
    
    void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) override {
        (void)x; (void)y; (void)w; (void)h; (void)color; // Mock - no rendering
    }
    
    void draw_circle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color) override {
        (void)x; (void)y; (void)radius; (void)color; // Mock - no rendering
    }
    
    void fill_circle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color) override {
        (void)x; (void)y; (void)radius; (void)color; // Mock - no rendering
    }
    
    void set_text_color(uint16_t fg, uint16_t bg) override {
        fg_color_ = fg;
        bg_color_ = bg;
    }
    
    void set_cursor(uint16_t x, uint16_t y) override {
        cursor_x_ = x;
        cursor_y_ = y;
    }
    
    void set_text_size(uint8_t size) override {
        text_size_ = size;
    }
    
    void print(const char* text) override {
        if (text) {
            cursor_x_ += strlen(text) * 6 * text_size_; // Approximate advance
        }
    }
    
    void println(const char* text) override {
        print(text);
        cursor_x_ = 0;
        cursor_y_ += 8 * text_size_; // Approximate line height
    }
    
    void draw_string(const char* text, uint16_t x, uint16_t y, uint16_t color) override {
        (void)text; (void)x; (void)y; (void)color; // Mock - no rendering
    }
    
    uint16_t text_width(const char* text) override {
        return text ? strlen(text) * 6 * text_size_ : 0; // Approximate width
    }
    
    uint16_t text_height() const override {
        return 8 * text_size_; // Approximate height
    }
    
    void update() override {
        // Mock - no buffering/refresh needed
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // Mock-Specific Test Helpers
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Reset mock state (for test isolation)
     */
    void reset() {
        initialized_ = false;
        rotation_ = 0;
        cursor_x_ = 0;
        cursor_y_ = 0;
        text_size_ = 1;
        fg_color_ = 0xFFFF;
        bg_color_ = 0x0000;
    }
    
    /**
     * @brief Get current cursor position (for test validation)
     */
    void get_cursor(uint16_t& x, uint16_t& y) const {
        x = cursor_x_;
        y = cursor_y_;
    }
    
private:
    uint16_t width_;
    uint16_t height_;
    bool initialized_;
    uint8_t rotation_;
    uint8_t text_size_;
    uint16_t cursor_x_;
    uint16_t cursor_y_;
    uint16_t fg_color_;
    uint16_t bg_color_;
};

} // namespace HAL
