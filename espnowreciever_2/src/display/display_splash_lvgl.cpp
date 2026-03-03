/**
 * @file display_splash_lvgl.cpp
 * @brief LVGL 8.4 splash screen with built-in fade animation
 *
 * Uses LVGL's built-in screen transition animations (lv_scr_load_anim)
 * for clean fade in/out effects without manual opacity pumping.
 */

#ifdef USE_LVGL

#include "display_splash_lvgl.h"
#include "../common.h"
#include "../helpers.h"
#include "../hal/display/lvgl_driver.h"
#include <JPEGDecoder.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace Display {

// Static storage for splash image descriptor and pixel buffer
// (must persist until main screen loads and auto-deletes splash)
static lv_img_dsc_t s_img_dsc;
static uint16_t* s_img_buf = nullptr;

/**
 * Decode a JPEG from LittleFS into a PSRAM-allocated RGB565 pixel buffer.
 * 
 * Returns heap_caps_malloc'd buffer. Caller must heap_caps_free() it.
 * Returns nullptr on failure; out_w / out_h are set to 0.
 */
static uint16_t* decode_jpg_to_rgb565(const char* path,
                                      uint16_t& out_w, uint16_t& out_h) {
    out_w = 0;
    out_h = 0;

    if (!LittleFS.exists(path)) {
        LOG_ERROR("SPLASH", "File not found: %s", path);
        return nullptr;
    }

    File f = LittleFS.open(path, "r");
    if (!f) {
        LOG_ERROR("SPLASH", "Cannot open: %s", path);
        return nullptr;
    }

    const size_t file_sz = f.size();
    LOG_INFO("SPLASH", "JPEG %s  size=%u bytes", path, (unsigned)file_sz);

    uint8_t* raw = (uint8_t*)malloc(file_sz);
    if (!raw) {
        LOG_ERROR("SPLASH", "malloc failed for JPEG buffer (%u bytes)", (unsigned)file_sz);
        f.close();
        return nullptr;
    }
    if (f.read(raw, file_sz) != file_sz) {
        LOG_ERROR("SPLASH", "Short read: %s", path);
        free(raw);
        f.close();
        return nullptr;
    }
    f.close();

    const uint32_t t0 = millis();
    if (!JpegDec.decodeArray(raw, file_sz)) {
        LOG_ERROR("SPLASH", "JPEG decode failed: %s", path);
        free(raw);
        return nullptr;
    }
    free(raw);

    out_w = JpegDec.width;
    out_h = JpegDec.height;
    LOG_INFO("SPLASH", "JPEG decoded in %u ms -> %ux%u",
             (unsigned)(millis() - t0), (unsigned)out_w, (unsigned)out_h);

    const size_t num_px = (size_t)out_w * out_h;
    uint16_t* buf = (uint16_t*)heap_caps_malloc(
        num_px * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        LOG_ERROR("SPLASH", "PSRAM malloc failed for pixel buffer (%u bytes)",
                  (unsigned)(num_px * sizeof(uint16_t)));
        out_w = 0; out_h = 0;
        return nullptr;
    }

    // Copy MCU blocks from JPEGDecoder to contiguous buffer
    // JPEGDecoder output is already in RGB565 format
    while (JpegDec.read()) {
        const uint16_t  mcu_w = JpegDec.MCUWidth;
        const uint16_t  mcu_h = JpegDec.MCUHeight;
        const uint32_t  x0    = (uint32_t)JpegDec.MCUx * mcu_w;
        const uint32_t  y0    = (uint32_t)JpegDec.MCUy * mcu_h;
        const uint16_t* src   = JpegDec.pImage;
        for (uint16_t dy = 0; dy < mcu_h; dy++) {
            if (y0 + dy >= out_h) break;
            for (uint16_t dx = 0; dx < mcu_w; dx++) {
                if (x0 + dx >= out_w) break;
                uint16_t px = src[dy * mcu_w + dx];
#if LV_COLOR_16_SWAP
                // LVGL draw buffers are byte-swapped when LV_COLOR_16_SWAP=1,
                // so pre-swap source RGB565 to match renderer expectations.
                px = (uint16_t)((px >> 8) | (px << 8));
#endif
                buf[(y0 + dy) * out_w + (x0 + dx)] = px;
            }
        }
    }
    return buf;
}

/**
 * Display splash screen with LVGL
 * 
 * Splash sequence:
 * 1. Create splash screen with image (or text fallback)
 * 2. Load with FADE_IN animation + backlight fade-in over 300ms
 * 3. Hold splash screen for 2 seconds with continuous LVGL pumping
 * 4. Transition to Ready screen with FADE_OUT animation (800ms)
 * 5. Free image buffer after transition completes
 */
void display_splash_lvgl() {
    LOG_INFO("SPLASH", "=== Splash START ===");

    if (!HAL::Display::LvglDriver::get_display()) {
        LOG_ERROR("SPLASH", "LVGL display not ready");
        return;
    }

    // Start with backlight OFF
    HAL::Display::LvglDriver::set_backlight(0);
    LOG_INFO("SPLASH", "Backlight set to 0");

    // Create splash screen
    lv_obj_t* splash_scr = lv_obj_create(NULL);
    if (!splash_scr) {
        LOG_ERROR("SPLASH", "Failed to create splash screen object");
        return;
    }

    lv_obj_set_size(splash_scr, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(splash_scr, lv_color_black(), 0);
    lv_obj_set_style_border_width(splash_scr, 0, 0);
    lv_obj_set_style_pad_all(splash_scr, 0, 0);

    // Decode and load splash image
    if (s_img_buf) {
        heap_caps_free(s_img_buf);
        s_img_buf = nullptr;
    }

    uint16_t img_w = 0, img_h = 0;
    s_img_buf = decode_jpg_to_rgb565("/BatteryEmulator4_320x170.jpg", img_w, img_h);

    if (s_img_buf && img_w > 0 && img_h > 0) {
        memset(&s_img_dsc, 0, sizeof(s_img_dsc));
        s_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        s_img_dsc.header.w = img_w;
        s_img_dsc.header.h = img_h;
        s_img_dsc.data = (const uint8_t*)s_img_buf;
        s_img_dsc.data_size = (uint32_t)img_w * (uint32_t)img_h * 2U;

        lv_obj_t* img_obj = lv_img_create(splash_scr);
        if (img_obj) {
            lv_img_set_src(img_obj, &s_img_dsc);
            lv_obj_center(img_obj);
            LOG_INFO("SPLASH", "Splash JPG attached to LVGL image object (%ux%u)", img_w, img_h);
        } else {
            LOG_ERROR("SPLASH", "Failed to create LVGL image object");
        }
    } else {
        LOG_WARN("SPLASH", "Splash JPG decode failed; using text fallback");
        lv_obj_t* title = lv_label_create(splash_scr);
        lv_label_set_text(title, "BMS Receiver");
        lv_obj_set_style_text_color(title, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -16);
    }

    // Load screen with FADE_IN animation (300ms fade-in)
    // auto_del=false: we'll keep splash around for transition later
    LOG_DEBUG("SPLASH", "Loading splash screen with FADE_IN animation...");
    lv_scr_load_anim(splash_scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);

    // Animate backlight from 0 to 255 over 300ms (synchronized with fade-in)
    HAL::Display::LvglDriver::animate_backlight_to(255, 300);

    // Wait for fade-in animation to complete with proper timing
    LOG_DEBUG("SPLASH", "Waiting for fade-in animation to complete...");
    const uint32_t fade_start = millis();
    uint32_t fade_frame_count = 0;
    while (lv_anim_count_running() > 0) {
        lv_timer_handler();
        fade_frame_count++;
        
        // Keep delay at 10ms for smooth animation
        smart_delay(10);
        
        // Safety timeout (500ms should be more than enough for 300ms animation)
        if (millis() - fade_start > 500) {
            LOG_WARN("SPLASH", "Fade-in timeout after %u frames, continuing...", fade_frame_count);
            break;
        }
    }
    
    LOG_INFO("SPLASH", "Fade-in complete (%u frames, %u ms)", fade_frame_count, 
             (unsigned)(millis() - fade_start));

    // Hold splash for 2 seconds with proper LVGL pumping at ~20ms intervals
    LOG_DEBUG("SPLASH", "Holding splash for 2000ms...");
    const uint32_t hold_start = millis();
    const uint32_t hold_duration = 2000;
    uint32_t hold_frame_count = 0;
    
    while ((millis() - hold_start) < hold_duration) {
        lv_timer_handler();
        hold_frame_count++;
        
        uint32_t elapsed = millis() - hold_start;
        uint32_t remaining = hold_duration - elapsed;
        
        // Sleep at most 20ms to maintain animation frame rate
        smart_delay(remaining > 20 ? 20 : remaining);
    }
    
    LOG_INFO("SPLASH", "Hold complete (%u frames, %u ms)", hold_frame_count, 
             (unsigned)(millis() - hold_start));

    LOG_INFO("SPLASH", "=== Splash END ===");
}

/**
 * Load the main "Ready" screen with fade-out transition from splash.
 * The FADE_OUT animation fades the old splash screen to black, then
 * shows the new Ready screen. auto_del=true cleans up splash safely.
 * 
 * Wait for animation to complete before returning.
 */
void display_initial_screen_lvgl() {
    LOG_INFO("SPLASH", "Loading Ready screen with fade transition...");

    if (!HAL::Display::LvglDriver::get_display()) {
        LOG_ERROR("SPLASH", "LVGL display not ready");
        return;
    }

    // Build Ready screen
    LOG_DEBUG("SPLASH", "Creating Ready screen object...");
    lv_obj_t* ready_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ready_scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ready_scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ready_scr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ready_scr, 0, LV_PART_MAIN);
    LOG_DEBUG("SPLASH", "Ready screen created at 0x%08X", (uint32_t)ready_scr);

    LOG_DEBUG("SPLASH", "Creating 'Ready' label...");
    lv_obj_t* lbl = lv_label_create(ready_scr);
    lv_label_set_text(lbl, "Ready");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x00FF00), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    // Load Ready screen with FADE_OUT animation (800ms fade)
    // auto_del=true: LVGL will safely delete splash screen after transition
    LOG_DEBUG("SPLASH", "Starting FADE_OUT animation (800ms)...");
    lv_scr_load_anim(ready_scr, LV_SCR_LOAD_ANIM_FADE_OUT, 800, 0, true);

    // Pump LVGL until animation completes
    const uint32_t anim_start = millis();
    uint32_t frame_count = 0;
    
    while (lv_anim_count_running() > 0) {
        lv_timer_handler();
        frame_count++;
        smart_delay(10);
        
        // Safety timeout (1.5 seconds should be more than enough for 800ms animation)
        if (millis() - anim_start > 1500) {
            LOG_WARN("SPLASH", "Animation timeout after %u frames, continuing...", frame_count);
            break;
        }
    }
    
    LOG_INFO("SPLASH", "Fade-out animation complete (%u frames, %u ms)", frame_count, 
             (unsigned)(millis() - anim_start));

    // Free splash image buffer now that splash screen is deleted
    if (s_img_buf) {
        heap_caps_free(s_img_buf);
        s_img_buf = nullptr;
        LOG_DEBUG("SPLASH", "Image buffer freed");
    }

    LOG_INFO("SPLASH", "Ready screen active");
}

} // namespace Display

#endif // USE_LVGL


