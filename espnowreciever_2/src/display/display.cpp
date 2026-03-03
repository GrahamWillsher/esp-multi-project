/**
 * @file display.cpp
 * @brief Display System - TFT Only Implementation
 * 
 * This file instantiates the TFT display implementation.
 * 
 * The application code never needs to know which implementation is active -
 * it just uses the IDisplay interface through the global g_display pointer.
 */

#include "display_interface.h"
#include "../common.h"
#include "tft_impl/tft_display.h"

typedef Display::TftDisplay DisplayImplementation;

// ============================================================================
// Global Display Instance
// ============================================================================

namespace Display {
    IDisplay* g_display = nullptr;
}

// ============================================================================
// Public Display API
// ============================================================================

/**
 * Initialize display system
 * 
 * Creates the appropriate display implementation and initializes hardware.
 * Must be called during setup() before any display operations.
 */
void init_display() {
    if (Display::g_display != nullptr) {
        LOG_WARN("DISPLAY", "Display already initialized");
        return;
    }
    
    LOG_INFO("DISPLAY", "Initializing display system...");
    Display::g_display = new DisplayImplementation();
    
    if (!Display::g_display->init()) {
        LOG_ERROR("DISPLAY", "Failed to initialize display");
        // Continue anyway - display may partially work
    }
    
    LOG_INFO("DISPLAY", "Display system ready");
}

/**
 * Display splash screen with fade-in
 * 
 * Wrapper that delegates to the active display implementation.
 * Called from main setup sequence.
 */
void displaySplashWithFade() {
    if (Display::g_display) {
        Display::g_display->display_splash_with_fade();
    } else {
        LOG_ERROR("DISPLAY", "displaySplashWithFade: Display not initialized");
    }
}

/**
 * Display initial Ready screen
 * 
 * Wrapper that delegates to the active display implementation.
 * Called from main setup sequence after splash completes.
 */
void displayInitialScreen() {
    if (Display::g_display) {
        Display::g_display->display_initial_screen();
    } else {
        LOG_ERROR("DISPLAY", "displayInitialScreen: Display not initialized");
    }
}

/**
 * Update SOC display
 * 
 * Wrapper that delegates to the active display implementation.
 * Called when SOC data is updated.
 */
void display_soc(float newSoC) {
    if (Display::g_display) {
        Display::g_display->update_soc(newSoC);
    }
}

/**
 * Update power display
 * 
 * Wrapper that delegates to the active display implementation.
 * Called when power data is updated.
 */
void display_power(int32_t current_power_w) {
    if (Display::g_display) {
        Display::g_display->update_power(current_power_w);
    }
}

/**
 * Display periodic task handler
 * 
 * Must be called regularly from main loop (ideally every 10-20ms).
 * For TFT this is a no-op. For LVGL this pumps the message loop.
 */
void display_task_handler() {
    if (Display::g_display) {
        Display::g_display->task_handler();
    }
}

/**
 * Show status page
 * 
 * Wrapper that delegates to the active display implementation.
 */
void show_status_page() {
    if (Display::g_display) {
        Display::g_display->show_status_page();
    }
}

/**
 * Show error state
 * 
 * Wrapper that delegates to the active display implementation.
 */
void display_error_state() {
    if (Display::g_display) {
        Display::g_display->show_error_state();
    }
}

/**
 * Show fatal error
 * 
 * Wrapper that delegates to the active display implementation.
 */
void display_fatal_error(const char* component, const char* message) {
    if (Display::g_display) {
        Display::g_display->show_fatal_error(component, message);
    }
}
