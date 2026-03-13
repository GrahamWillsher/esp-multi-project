/**
 * @file display.h
 * @brief Public Display API
 * 
 * Application-facing API for display operations.
 * Implementation details are hidden - uses either TFT or LVGL
 * depending on compile-time configuration.
 */

#pragma once

// ============================================================================
// Splash Screen and Initialization
// ============================================================================

/**
 * Initialize display system
 * Must be called in setup() before any display operations
 */
void init_display();

/**
 * Display splash screen with fade-in animation
 */
void displaySplashWithFade();

/**
 * Display initial Ready screen (after splash)
 */
void displayInitialScreen();

// ============================================================================
// Real-Time Data Updates
// ============================================================================

/**
 * Update SOC (State of Charge) display
 * @param newSoC Battery SOC as percentage (0.0-100.0)
 */
void display_soc(float newSoC);

/**
 * Update power display
 * @param current_power_w Power in watts
 */
void display_power(int32_t current_power_w);

/**
 * Show status page with real-time data
 */
void show_status_page();

// ============================================================================
// Error Display
// ============================================================================

/**
 * Show error state (connection lost, etc.)
 */
void display_error_state();

/**
 * Show fatal error (system error, can't recover)
 * @param component Component name (e.g., "LVGL", "WiFi")
 * @param message Error message
 */
void display_fatal_error(const char* component, const char* message);

// ============================================================================
// Task Handler (Call from Main Loop)
// ============================================================================

/**
 * Process display tasks
 * 
 * Must be called regularly from main loop (ideally every 10-20ms).
 * For TFT: No-op
 * For LVGL: Pumps message loop, processes animations, renders updates
 */
void display_task_handler();
