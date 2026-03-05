#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <cstdint>

/**
 * @brief Centralized display configuration constants
 * 
 * Replaces scattered magic numbers throughout the codebase with
 * named constants for better maintainability and clarity.
 */
namespace DisplayConfig {
    // ═══════════════════════════════════════════════════════════════════════
    // Hardware Configuration
    // ═══════════════════════════════════════════════════════════════════════
    
    /// LilyGo T-Display-S3 physical dimensions
    constexpr uint16_t DISPLAY_WIDTH = 320;
    constexpr uint16_t DISPLAY_HEIGHT = 240;
    
    /// Display orientation (0=0°, 1=90°, 2=180°, 3=270°)
    constexpr uint8_t DISPLAY_ROTATION = 1;
    
    // ═══════════════════════════════════════════════════════════════════════
    // Color Palette (16-bit RGB565 format)
    // ═══════════════════════════════════════════════════════════════════════
    
    constexpr uint16_t COLOR_BACKGROUND = 0x0000;      // Black
    constexpr uint16_t COLOR_TEXT = 0xFFFF;            // White
    constexpr uint16_t COLOR_ACCENT = 0x001F;          // Blue
    constexpr uint16_t COLOR_WARNING = 0xF800;         // Red
    constexpr uint16_t COLOR_SUCCESS = 0x07E0;         // Green
    constexpr uint16_t COLOR_GRAY = 0x8410;            // Gray
    constexpr uint16_t COLOR_ORANGE = 0xFC60;          // Orange (for indicators)
    constexpr uint16_t COLOR_DARK_GRAY = 0x4208;       // Dark gray (for inactive elements)
    
    // ═══════════════════════════════════════════════════════════════════════
    // Layout & Spacing
    // ═══════════════════════════════════════════════════════════════════════
    
    /// Main content area margins
    constexpr uint16_t CONTENT_MARGIN_X = 20;
    constexpr uint16_t CONTENT_MARGIN_Y = 20;
    
    /// Content area dimensions
    constexpr uint16_t CONTENT_WIDTH = DISPLAY_WIDTH - (2 * CONTENT_MARGIN_X);   // 280
    constexpr uint16_t CONTENT_HEIGHT = DISPLAY_HEIGHT - (2 * CONTENT_MARGIN_Y); // 200
    
    /// Widget spacing and positioning
    constexpr uint16_t WIDGET_SPACING = 10;
    constexpr uint16_t HEADER_HEIGHT = 30;
    constexpr uint16_t FOOTER_HEIGHT = 20;
    
    // ═══════════════════════════════════════════════════════════════════════
    // Typography
    // ═══════════════════════════════════════════════════════════════════════
    
    /// TFT_eSPI font sizes
    constexpr uint8_t FONT_SIZE_SMALL = 1;
    constexpr uint8_t FONT_SIZE_NORMAL = 2;
    constexpr uint8_t FONT_SIZE_LARGE = 3;
    constexpr uint8_t FONT_SIZE_XLARGE = 4;
    
    /// Character dimensions (approximate for Font 2)
    constexpr uint16_t CHAR_WIDTH_SMALL = 8;    // Font 1 width
    constexpr uint16_t CHAR_HEIGHT_SMALL = 8;   // Font 1 height
    constexpr uint16_t CHAR_WIDTH_NORMAL = 12;  // Font 2 width
    constexpr uint16_t CHAR_HEIGHT_NORMAL = 16; // Font 2 height
    constexpr uint16_t CHAR_WIDTH_LARGE = 16;   // Font 3 width (approx)
    constexpr uint16_t CHAR_HEIGHT_LARGE = 24;  // Font 3 height (approx)
    
    // ═══════════════════════════════════════════════════════════════════════
    // Widget Dimensions
    // ═══════════════════════════════════════════════════════════════════════
    
    /// Number display widget (for SOC%, Power values)
    constexpr uint16_t NUMBER_DISPLAY_WIDTH = 120;
    constexpr uint16_t NUMBER_DISPLAY_HEIGHT = 40;
    constexpr uint16_t NUMBER_DISPLAY_MARGIN = 4;
    
    /// Progress/Power bar
    constexpr uint16_t PROGRESS_BAR_WIDTH = 200;
    constexpr uint16_t PROGRESS_BAR_HEIGHT = 20;
    constexpr uint16_t PROGRESS_BAR_MARGIN = 2;
    
    /// Status indicator
    constexpr uint16_t STATUS_INDICATOR_SIZE = 12;
    constexpr uint16_t STATUS_INDICATOR_MARGIN = 4;
    
    /// Button areas
    constexpr uint16_t BUTTON_HEIGHT = 30;
    constexpr uint16_t BUTTON_WIDTH = 80;
    
    // ═══════════════════════════════════════════════════════════════════════
    // Timing & Animation
    // ═══════════════════════════════════════════════════════════════════════
    
    /// Page transitions and animations
    constexpr uint32_t PAGE_TRANSITION_MS = 5000;        // Time before auto-transition
    constexpr uint32_t SPINNER_UPDATE_MS = 100;          // Update spinner animation
    constexpr uint32_t DATA_UPDATE_INTERVAL_MS = 500;    // Refresh display data
    constexpr uint32_t BACKLIGHT_FADE_MS = 500;          // Backlight fade duration
    
    /// Connection status indicators
    constexpr uint32_t BLINK_INTERVAL_MS = 500;          // Blink rate for indicators
    constexpr uint32_t DISCONNECT_TIMEOUT_MS = 3000;     // Show disconnect message
    
    // ═══════════════════════════════════════════════════════════════════════
    // Backlight Control
    // ═══════════════════════════════════════════════════════════════════════
    
    constexpr uint8_t BACKLIGHT_MIN_BRIGHTNESS = 50;     // Minimum (20%)
    constexpr uint8_t BACKLIGHT_DEFAULT_BRIGHTNESS = 200; // Default (78%)
    constexpr uint8_t BACKLIGHT_MAX_BRIGHTNESS = 255;    // Maximum
    
    // ═══════════════════════════════════════════════════════════════════════
    // Helper Functions
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Get center X coordinate of display
     */
    constexpr uint16_t get_center_x() {
        return DISPLAY_WIDTH / 2;
    }
    
    /**
     * @brief Get center Y coordinate of display
     */
    constexpr uint16_t get_center_y() {
        return DISPLAY_HEIGHT / 2;
    }
    
    /**
     * @brief Get center X coordinate of content area
     */
    constexpr uint16_t get_content_center_x() {
        return CONTENT_MARGIN_X + (CONTENT_WIDTH / 2);
    }
    
    /**
     * @brief Get center Y coordinate of content area
     */
    constexpr uint16_t get_content_center_y() {
        return CONTENT_MARGIN_Y + (CONTENT_HEIGHT / 2);
    }

} // namespace DisplayConfig

#endif // DISPLAY_CONFIG_H
