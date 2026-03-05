/**
 * @file tft_display.h
 * @brief TFT-eSPI Display Implementation
 * 
 * Pure TFT-eSPI implementation using synchronous, blocking rendering.
 * This is the proven-working approach with direct pixel control.
 * 
 * Used when USE_TFT is defined during compilation.
 */

#pragma once

#ifdef USE_TFT

#include "../display_interface.h"
#include "../layout/display_layout_spec.h"
#include <TFT_eSPI.h>

namespace Display {

/**
 * TFT Display Implementation
 * 
 * Provides synchronous, blocking display operations using TFT_eSPI library.
 * All operations complete immediately - animations are manual loops with delays.
 * 
 * Architecture:
 * - Direct TFT pixel writing
 * - Manual animation loops with blocking delay()
 * - Simple, proven, low-overhead approach
 */
class TftDisplay : public IDisplay {
public:
    /**
     * Constructor - initialize TFT_eSPI instance
     */
    TftDisplay();
    
    /**
     * Destructor - cleanup if needed
     */
    virtual ~TftDisplay() = default;
    
    // =========================================================================
    // IDisplay Implementation
    // =========================================================================
    
    /**
     * Initialize TFT hardware (GPIO, SPI, backlight PWM)
     */
    bool init() override;
    
    /**
     * Display splash screen with fade-in
     * 
     * TFT approach:
     * 1. Set backlight to 0
     * 2. Load and draw splash image
     * 3. Manual fade-in loop: backlight 0->255 with delay(16)
     * 4. Hold for 2 seconds
     * 5. Manual fade-out loop: backlight 255->0
     */
    void display_splash_with_fade() override;
    
    /**
     * Display Ready screen after splash
     */
    void display_initial_screen() override;
    
    /**
     * Update SOC display value
     */
    void update_soc(float soc_percent) override;
    
    /**
     * Update power display value
     */
    void update_power(int32_t power_w) override;
    
    /**
     * Show status page
     */
    void show_status_page() override;
    
    /**
     * Show error state (red screen)
     */
    void show_error_state() override;
    
    /**
     * Show fatal error
     */
    void show_fatal_error(const char* component, const char* message) override;
    
    /**
     * Task handler (no-op for TFT - rendering is synchronous)
     */
    void task_handler() override {
        // TFT rendering is synchronous, so no periodic tasks needed
        // This is a no-op, kept for interface compatibility
    }
    
private:
    uint8_t current_backlight_ = 0;  // Track current brightness state

    // Stateful rendering cache (previously hidden function-local statics)
    int16_t last_backlight_logged_ = -1;
    char soc_text_buffer_[16] = "";
    bool soc_gradient_initialized_ = false;

    bool power_bar_initialized_ = false;
    int power_bar_char_width_ = 0;
    int power_bar_max_bars_per_side_ = 0;
    uint16_t power_bar_gradient_green_[LayoutSpec::PowerBar::MAX_BARS_PER_SIDE] = {0};
    uint16_t power_bar_gradient_red_[LayoutSpec::PowerBar::MAX_BARS_PER_SIDE] = {0};
    int power_bar_previous_signed_bars_ = 0;
    int32_t power_bar_last_power_text_ = 2147483647;
    
    // NOTE: We use the global tft object from globals.cpp (extern)
    // TFT_eSPI is designed as a singleton and doesn't work as a member variable
    
    // =========================================================================
    // Helper Methods
    // =========================================================================
    
    /**
     * Initialize TFT hardware (GPIO, SPI pins, display controller)
     */
    void init_hardware();
    
    /**
     * Initialize backlight PWM
     */
    void init_backlight();
    
    /**
     * Set backlight brightness (0-255)
     */
    void set_backlight(uint8_t brightness);
    
    /**
     * Animate backlight from current to target over specified duration
     * Uses blocking delay() - pauses execution
     */
    void animate_backlight(uint8_t target, uint32_t duration_ms);
    
    /**
     * Load splash image from LittleFS and draw on display
     */
    void load_and_draw_splash();

    /**
     * Show text fallback when splash image cannot be loaded/decoded
     */
    void show_splash_fallback(const char* reason);

    /**
     * Configure common fatal/error screen style
     */
    void setup_error_screen();

    /**
     * Emit backlight logs only for significant changes
     */
    void log_backlight_if_significant(uint8_t brightness, int16_t& last_logged);
    
    /**
     * Draw the SOC value on display
     */
    void draw_soc(float soc_percent);

    // Power-bar rendering helpers
    void init_power_bar_state(int& bar_char_width,
                              int& max_bars_per_side,
                              uint16_t* gradient_green,
                              uint16_t* gradient_red);
    int calculate_power_bar_count(int32_t clamped_power,
                                  int max_bars_per_side,
                                  int32_t max_power) const;
    bool should_pulse_animate(int signed_bars,
                              int previous_signed_bars) const;
    void draw_power_bars(int bars,
                         bool charging,
                         int center_x,
                         int bar_y,
                         int bar_char_width,
                         const uint16_t* gradient_green,
                         const uint16_t* gradient_red,
                         int ripple_pos = -1);
    void clear_power_bar_residuals(int bars,
                                   bool charging,
                                   int previous_signed_bars,
                                   int center_x,
                                   int bar_y,
                                   int bar_char_width);
    void draw_zero_power_marker(int center_x,
                                int bar_y,
                                int max_bars_per_side,
                                int bar_char_width);
    void draw_power_text_if_changed(int32_t power_w,
                                    int center_x,
                                    int text_y,
                                    int32_t& last_power_text);

    // Text style helpers - consolidate repeated font/size/color/datum settings
    void set_text_style_soc();
    void set_text_style_power_bar();
    void set_text_style_power_value();
    void set_text_style_ready(uint16_t color = TFT_GREEN);
    void set_text_style_error();
    
    // Rectangle clearing helpers - consolidate repeated fillRect patterns
    void clear_rect(int x, int y, int w, int h, uint16_t color = 0);  // Uses tft_background if color=0
    
    /**
     * Draw the power value on display
     */
    void draw_power(int32_t power_w);
};

// Helper function (defined in implementation)
// Decodes JPEG from LittleFS to RGB565 buffer
// Returns heap_caps_malloc'd buffer (caller must free)
static uint16_t* decode_jpg_to_rgb565(const char* path,
                                      uint16_t& out_w, uint16_t& out_h);

} // namespace Display

#endif // USE_TFT
