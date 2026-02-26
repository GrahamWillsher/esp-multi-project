#pragma once

/**
 * @file tft_espi_display_driver.h
 * @brief TFT_eSPI implementation of display HAL interface
 * 
 * Hardware-specific implementation for TFT_eSPI library (ST7789 controller).
 * Used by LilyGo T-Display-S3 receiver.
 */

#include "../../ESP32common/hal/display/idisplay_driver.h"
#include <TFT_eSPI.h>

namespace HAL {

/**
 * @brief TFT_eSPI display driver implementation
 * 
 * Wraps TFT_eSPI library to provide hardware abstraction.
 * Allows testing with mock implementations and future display swaps.
 */
class TftEspiDisplayDriver : public IDisplayDriver {
public:
    /**
     * @brief Construct driver wrapping existing TFT_eSPI instance
     * @param tft Reference to TFT_eSPI display object
     */
    explicit TftEspiDisplayDriver(TFT_eSPI& tft);
    
    // Disable copy (driver wraps hardware resource)
    TftEspiDisplayDriver(const TftEspiDisplayDriver&) = delete;
    TftEspiDisplayDriver& operator=(const TftEspiDisplayDriver&) = delete;
    
    // ═══════════════════════════════════════════════════════════════════════
    // IDisplayDriver Interface Implementation
    // ═══════════════════════════════════════════════════════════════════════
    
    bool init() override;
    bool is_available() const override;
    void set_rotation(uint8_t rotation) override;
    uint16_t get_width() const override;
    uint16_t get_height() const override;
    
    void fill_screen(uint16_t color) override;
    void draw_pixel(uint16_t x, uint16_t y, uint16_t color) override;
    void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) override;
    void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) override;
    void draw_circle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color) override;
    void fill_circle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color) override;
    
    void set_text_color(uint16_t fg, uint16_t bg) override;
    void set_cursor(uint16_t x, uint16_t y) override;
    void set_text_size(uint8_t size) override;
    void print(const char* text) override;
    void println(const char* text) override;
    void draw_string(const char* text, uint16_t x, uint16_t y, uint16_t color) override;
    uint16_t text_width(const char* text) override;
    uint16_t text_height() const override;
    
    void update() override;
    
    // ═══════════════════════════════════════════════════════════════════════
    // TFT-Specific Extensions (Optional)
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Get underlying TFT_eSPI instance (for advanced operations)
     * @return Reference to wrapped TFT object
     * @note Use sparingly - prefer HAL interface methods
     */
    TFT_eSPI& get_tft() { return tft_; }
    
private:
    TFT_eSPI& tft_;           ///< Wrapped TFT_eSPI hardware instance
    bool initialized_;        ///< Initialization state
};

} // namespace HAL
