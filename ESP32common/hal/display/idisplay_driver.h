#pragma once

/**
 * @file idisplay_driver.h
 * @brief Hardware Abstraction Layer (HAL) interface for display drivers
 * 
 * This interface provides a hardware-agnostic API for display operations,
 * enabling:
 * - Testing without physical hardware (mock implementations)
 * - Support for different display types (TFT, OLED, e-ink)
 * - Clean separation between display logic and hardware
 * 
 * @note Only the RECEIVER uses display drivers. Transmitter has no display.
 */

#include <stdint.h>

namespace HAL {

/**
 * @brief Abstract interface for display driver implementations
 * 
 * Implementations must provide hardware-specific rendering logic
 * for various display types (TFT_eSPI, OLED, e-ink, mock).
 */
class IDisplayDriver {
public:
    virtual ~IDisplayDriver() = default;
    
    // ═══════════════════════════════════════════════════════════════════════
    // Initialization and Configuration
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Initialize the display hardware
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool init() = 0;
    
    /**
     * @brief Check if display is available and ready
     * @return true if display is operational
     */
    virtual bool is_available() const = 0;
    
    /**
     * @brief Set display rotation/orientation
     * @param rotation Rotation value (0-3)
     */
    virtual void set_rotation(uint8_t rotation) = 0;
    
    /**
     * @brief Get current display width (after rotation)
     * @return Width in pixels
     */
    virtual uint16_t get_width() const = 0;
    
    /**
     * @brief Get current display height (after rotation)
     * @return Height in pixels
     */
    virtual uint16_t get_height() const = 0;
    
    // ═══════════════════════════════════════════════════════════════════════
    // Basic Drawing Primitives
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Fill entire screen with solid color
     * @param color RGB565 color value
     */
    virtual void fill_screen(uint16_t color) = 0;
    
    /**
     * @brief Draw a single pixel
     * @param x X coordinate
     * @param y Y coordinate
     * @param color RGB565 color value
     */
    virtual void draw_pixel(uint16_t x, uint16_t y, uint16_t color) = 0;
    
    /**
     * @brief Draw a rectangle outline
     * @param x X coordinate of top-left corner
     * @param y Y coordinate of top-left corner
     * @param w Width in pixels
     * @param h Height in pixels
     * @param color RGB565 color value
     */
    virtual void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) = 0;
    
    /**
     * @brief Draw a filled rectangle
     * @param x X coordinate of top-left corner
     * @param y Y coordinate of top-left corner
     * @param w Width in pixels
     * @param h Height in pixels
     * @param color RGB565 color value
     */
    virtual void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) = 0;
    
    /**
     * @brief Draw a circle outline
     * @param x X coordinate of center
     * @param y Y coordinate of center
     * @param radius Radius in pixels
     * @param color RGB565 color value
     */
    virtual void draw_circle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color) = 0;
    
    /**
     * @brief Draw a filled circle
     * @param x X coordinate of center
     * @param y Y coordinate of center
     * @param radius Radius in pixels
     * @param color RGB565 color value
     */
    virtual void fill_circle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color) = 0;
    
    // ═══════════════════════════════════════════════════════════════════════
    // Text Rendering
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Set text foreground and background colors
     * @param fg Foreground color (RGB565)
     * @param bg Background color (RGB565)
     */
    virtual void set_text_color(uint16_t fg, uint16_t bg) = 0;
    
    /**
     * @brief Set text cursor position
     * @param x X coordinate
     * @param y Y coordinate
     */
    virtual void set_cursor(uint16_t x, uint16_t y) = 0;
    
    /**
     * @brief Set text size multiplier
     * @param size Size multiplier (1 = normal, 2 = double, etc.)
     */
    virtual void set_text_size(uint8_t size) = 0;
    
    /**
     * @brief Print text at current cursor position
     * @param text Null-terminated string
     */
    virtual void print(const char* text) = 0;
    
    /**
     * @brief Print text with newline at current cursor position
     * @param text Null-terminated string
     */
    virtual void println(const char* text) = 0;
    
    /**
     * @brief Draw text at specific position
     * @param text Null-terminated string
     * @param x X coordinate
     * @param y Y coordinate
     * @param color Text color (RGB565)
     */
    virtual void draw_string(const char* text, uint16_t x, uint16_t y, uint16_t color) = 0;
    
    /**
     * @brief Get width of text string in pixels
     * @param text Null-terminated string
     * @return Width in pixels with current text size
     */
    virtual uint16_t text_width(const char* text) = 0;
    
    /**
     * @brief Get height of current font in pixels
     * @return Height in pixels with current text size
     */
    virtual uint16_t text_height() const = 0;
    
    // ═══════════════════════════════════════════════════════════════════════
    // Advanced Operations
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Update display (if buffered)
     * Some displays require explicit update to show changes
     */
    virtual void update() = 0;
};

} // namespace HAL
