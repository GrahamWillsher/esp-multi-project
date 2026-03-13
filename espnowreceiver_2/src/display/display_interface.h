/**
 * @file display_interface.h
 * @brief Abstract display interface for hardware-agnostic display operations
 * 
 * Both TFT and LVGL implementations must satisfy this interface.
 * This allows compile-time selection between implementations without
 * mixing code or using runtime ifdef blocks.
 */

#pragma once

#include <cstdint>

namespace Display {

/**
 * Abstract Display Interface
 * 
 * Defines all display operations that the application expects.
 * Two implementations are provided:
 * - TftDisplay: Pure TFT-eSPI with direct rendering (proven working)
 * - LvglDisplay: Pure LVGL with async animations (modern UI)
 */
class IDisplay {
public:
    virtual ~IDisplay() = default;
    
    // =========================================================================
    // Initialization
    // =========================================================================
    
    /**
     * Initialize display hardware and driver
     * @return true if initialization successful, false on error
     */
    virtual bool init() = 0;
    
    // =========================================================================
    // Splash Screen Sequence
    // =========================================================================
    
    /**
     * Display splash screen with fade-in animation
     * 
     * Sequence:
     * 1. Load splash image from LittleFS
     * 2. Display with fade-in effect (backlight and/or opacity)
     * 3. Hold splash screen for ~2 seconds
     * 4. Complete and return (Ready screen will follow)
     */
    virtual void display_splash_with_fade() = 0;
    
    /**
     * Display initial "Ready" screen (transition from splash)
     * 
     * Called after splash display completes.
     * Shows static "Ready" text or icon indicating system is ready for data.
     */
    virtual void display_initial_screen() = 0;
    
    // =========================================================================
    // Real-Time Data Updates
    // =========================================================================
    
    /**
     * Update State of Charge (SOC) display
     * @param soc_percent Battery SOC as percentage (0.0 to 100.0)
     */
    virtual void update_soc(float soc_percent) = 0;
    
    /**
     * Update Power display
     * @param power_w Power in watts (can be negative for discharge)
     */
    virtual void update_power(int32_t power_w) = 0;
    
    /**
     * Display status page showing SOC, power, and other metrics
     * 
     * Called when transitioning to main status view after splash.
     * Remains active throughout normal operation.
     */
    virtual void show_status_page() = 0;
    
    // =========================================================================
    // Error Display
    // =========================================================================
    
    /**
     * Display error state (e.g., connection lost)
     * 
     * Shows red screen with error indicator.
     * System can recover - this is not fatal.
     * Calling show_status_page() will restore normal display.
     */
    virtual void show_error_state() = 0;
    
    /**
     * Display fatal error - system is stuck
     * 
     * Red screen with error details (component, message).
     * System will likely enter infinite loop after this.
     * 
     * @param component Component name (e.g., "LVGL", "Display", "WiFi")
     * @param message Error message (e.g., "Failed to initialize")
     */
    virtual void show_fatal_error(const char* component, const char* message) = 0;
    
    // =========================================================================
    // Periodic Task Handler
    // =========================================================================
    
    /**
     * Process display tasks (call regularly from main loop)
     * 
     * TFT implementation: No-op (rendering is synchronous)
     * LVGL implementation: Pumps message loop, processes animations, renders
     * 
     * Should be called frequently (every 10-20ms) from loop() or task.
     */
    virtual void task_handler() = 0;
};

// ============================================================================
// Global Instance
// ============================================================================

/**
 * Global display instance
 * 
 * Points to either TftDisplay or LvglDisplay depending on build configuration.
 * Initialized by init_display() in main setup sequence.
 */
extern IDisplay* g_display;

} // namespace Display
