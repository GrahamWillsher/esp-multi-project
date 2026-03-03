/**
 * @file display_splash_lvgl.h
 * @brief Pure LVGL splash screen and initial screen management
 *
 * Screen lifecycle:
 *   display_splash_lvgl()         — fade in/hold/fade out via LVGL opacity animation
 *   display_initial_screen_lvgl() — loads Ready screen, auto-deletes splash screen
 *
 * Rendering: 100% LVGL API. No TFT_eSPI calls.
 * Backlight: HAL-controlled (LvglDriver::set_backlight), enabled once after load.
 */

#pragma once

#ifdef USE_LVGL

#include <lvgl.h>

namespace Display {

/** Run full splash sequence (decode, fade in, hold, fade out). */
void display_splash_lvgl();

/** Load the initial Ready screen, safely replacing the splash screen. */
void display_initial_screen_lvgl();

} // namespace Display

#endif // USE_LVGL
