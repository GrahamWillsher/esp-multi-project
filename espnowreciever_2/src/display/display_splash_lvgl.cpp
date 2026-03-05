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
#include "layout/display_layout_spec.h"
#include <JPEGDecoder.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace Display {

// Static storage for splash image - MUST persist until screen transition completes
static lv_img_dsc_t s_splash_img_dsc;
static lv_color_t* s_splash_img_data = nullptr;

// TEST IMAGE: Simple 320x170 pattern with color stripes
// This is a simple gradient pattern for testing LVGL image display
static bool create_test_image() {
    LOG_INFO("SPLASH", "=== Creating TEST IMAGE (320x170) ===");
    
    // Clean up any existing buffer
    if (s_splash_img_data) {
        LOG_DEBUG("SPLASH", "Freeing existing image buffer");
        heap_caps_free(s_splash_img_data);
        s_splash_img_data = nullptr;
    }

    const uint16_t IMG_WIDTH = 320;
    const uint16_t IMG_HEIGHT = 170;
    size_t pixel_count = (size_t)IMG_WIDTH * IMG_HEIGHT;
    
    // Allocate PSRAM buffer for test image
    LOG_INFO("SPLASH", "Allocating %u bytes in PSRAM for test image", pixel_count * sizeof(lv_color_t));
    s_splash_img_data = (lv_color_t*)heap_caps_malloc(
        pixel_count * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    
    if (!s_splash_img_data) {
        LOG_ERROR("SPLASH", "Failed to allocate test image buffer");
        return false;
    }
    LOG_INFO("SPLASH", "✓ Test image buffer allocated at 0x%08X", (uint32_t)s_splash_img_data);

    // Fill with test pattern: vertical color stripes
    // Red stripe, Green stripe, Blue stripe, Yellow stripe, Cyan stripe, Magenta stripe
    uint16_t stripe_width = IMG_WIDTH / 6;
    lv_color_t colors[] = {
        lv_color_hex(0xFF0000),  // Red
        lv_color_hex(0x00FF00),  // Green
        lv_color_hex(0x0000FF),  // Blue
        lv_color_hex(0xFFFF00),  // Yellow
        lv_color_hex(0x00FFFF),  // Cyan
        lv_color_hex(0xFF00FF),  // Magenta
    };

    uint16_t* dest = (uint16_t*)s_splash_img_data;
    
    // Fill entire image with pattern
    for (uint16_t y = 0; y < IMG_HEIGHT; y++) {
        for (uint16_t x = 0; x < IMG_WIDTH; x++) {
            uint8_t stripe = x / stripe_width;
            if (stripe > 5) stripe = 5;
            
            lv_color_t color = colors[stripe];
            uint16_t pixel = color.full;
            
            // Handle byte swap if needed for LVGL
            #if LV_COLOR_16_SWAP
            pixel = (pixel >> 8) | (pixel << 8);
            #endif
            
            dest[y * IMG_WIDTH + x] = pixel;
        }
    }
    LOG_INFO("SPLASH", "✓ Test pattern filled (6 vertical stripes)");

    // Initialize LVGL image descriptor
    memset(&s_splash_img_dsc, 0, sizeof(s_splash_img_dsc));
    s_splash_img_dsc.header.always_zero = 0;
    s_splash_img_dsc.header.w = IMG_WIDTH;
    s_splash_img_dsc.header.h = IMG_HEIGHT;
    s_splash_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_splash_img_dsc.data = (const uint8_t*)s_splash_img_data;
    s_splash_img_dsc.data_size = pixel_count * sizeof(lv_color_t);

    LOG_INFO("SPLASH", "✓ Image descriptor initialized: %ux%u, TRUE_COLOR format, %u bytes", 
             IMG_WIDTH, IMG_HEIGHT, s_splash_img_dsc.data_size);
    LOG_INFO("SPLASH", "=== TEST IMAGE CREATION SUCCESS ===");
    return true;
}

/**
 * Decode JPEG and create LVGL-compatible RGB565 image buffer in PSRAM
 * Returns true if successful, false otherwise
 */
static bool load_splash_image(const char* filepath) {
    LOG_INFO("SPLASH", "=== load_splash_image() START ===");
    
    // Clean up any existing buffer
    if (s_splash_img_data) {
        LOG_DEBUG("SPLASH", "Freeing existing image buffer");
        heap_caps_free(s_splash_img_data);
        s_splash_img_data = nullptr;
    }

    if (!LittleFS.exists(filepath)) {
        LOG_ERROR("SPLASH", "File not found: %s", filepath);
        return false;
    }
    LOG_DEBUG("SPLASH", "File exists: %s", filepath);

    // Load JPEG file into RAM
    File f = LittleFS.open(filepath, "r");
    if (!f) {
        LOG_ERROR("SPLASH", "Failed to open: %s", filepath);
        return false;
    }

    size_t fileSize = f.size();
    LOG_INFO("SPLASH", "File size: %u bytes", fileSize);
    
    uint8_t* jpegBuffer = (uint8_t*)malloc(fileSize);
    if (!jpegBuffer) {
        LOG_ERROR("SPLASH", "Failed to allocate JPEG buffer (%u bytes)", fileSize);
        f.close();
        return false;
    }
    LOG_DEBUG("SPLASH", "JPEG buffer allocated: %u bytes", fileSize);

    size_t bytesRead = f.read(jpegBuffer, fileSize);
    LOG_INFO("SPLASH", "Read %u bytes from file", bytesRead);
    
    if (bytesRead != fileSize) {
        LOG_ERROR("SPLASH", "Failed to read JPEG file (got %u, expected %u)", bytesRead, fileSize);
        free(jpegBuffer);
        f.close();
        return false;
    }
    f.close();
    LOG_DEBUG("SPLASH", "File closed");

    // Decode JPEG
    LOG_INFO("SPLASH", "Starting JPEG decode...");
    if (!JpegDec.decodeArray(jpegBuffer, fileSize)) {
        LOG_ERROR("SPLASH", "JPEG decode failed");
        free(jpegBuffer);
        return false;
    }
    LOG_INFO("SPLASH", "JPEG decode SUCCESS");
    free(jpegBuffer);
    LOG_DEBUG("SPLASH", "JPEG buffer freed");

    uint16_t img_w = JpegDec.width;
    uint16_t img_h = JpegDec.height;
    LOG_INFO("SPLASH", "JPEG dimensions: %ux%u pixels", img_w, img_h);

    // Allocate PSRAM buffer for image pixels
    size_t pixel_count = (size_t)img_w * img_h;
    LOG_INFO("SPLASH", "Pixel count: %u (allocating %u bytes from PSRAM)", pixel_count, pixel_count * sizeof(lv_color_t));
    
    s_splash_img_data = (lv_color_t*)heap_caps_malloc(
        pixel_count * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    
    if (!s_splash_img_data) {
        LOG_ERROR("SPLASH", "Failed to allocate image buffer in PSRAM (%u bytes)", pixel_count * sizeof(lv_color_t));
        return false;
    }
    LOG_INFO("SPLASH", "PSRAM buffer allocated: 0x%08X", (uint32_t)s_splash_img_data);

    // Copy decoded MCU blocks to contiguous buffer
    LOG_INFO("SPLASH", "Copying MCU blocks to buffer...");
    uint16_t* dest = (uint16_t*)s_splash_img_data;
    uint16_t mcu_w = JpegDec.MCUWidth;
    uint16_t mcu_h = JpegDec.MCUHeight;
    uint32_t mcu_count = 0;
    LOG_DEBUG("SPLASH", "MCU dimensions: %ux%u", mcu_w, mcu_h);

    while (JpegDec.read()) {
        mcu_count++;
        uint16_t* src = JpegDec.pImage;
        uint32_t mcu_x = JpegDec.MCUx * mcu_w;
        uint32_t mcu_y = JpegDec.MCUy * mcu_h;

        for (uint16_t y = 0; y < mcu_h; y++) {
            if (mcu_y + y >= img_h) break;
            for (uint16_t x = 0; x < mcu_w; x++) {
                if (mcu_x + x >= img_w) break;
                
                uint16_t pixel = src[y * mcu_w + x];
                
                // Handle byte swap if needed for LVGL
                #if LV_COLOR_16_SWAP
                // Swap bytes for LVGL's expected format
                pixel = (pixel >> 8) | (pixel << 8);
                #endif
                
                dest[(mcu_y + y) * img_w + (mcu_x + x)] = pixel;
            }
        }
    }
    LOG_INFO("SPLASH", "MCU copy complete: %u MCU blocks processed", mcu_count);

    // Initialize LVGL image descriptor
    memset(&s_splash_img_dsc, 0, sizeof(s_splash_img_dsc));
    s_splash_img_dsc.header.always_zero = 0;
    s_splash_img_dsc.header.w = img_w;
    s_splash_img_dsc.header.h = img_h;
    s_splash_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_splash_img_dsc.data = (const uint8_t*)s_splash_img_data;
    s_splash_img_dsc.data_size = pixel_count * sizeof(lv_color_t);

    LOG_INFO("SPLASH", "Image descriptor initialized: %ux%u, format=TRUE_COLOR, data_size=%u bytes", 
             s_splash_img_dsc.header.w, s_splash_img_dsc.header.h, s_splash_img_dsc.data_size);
    LOG_INFO("SPLASH", "=== load_splash_image() END (SUCCESS) ===");
    return true;
}

void display_splash_lvgl() {
    LOG_INFO("SPLASH", "╔════════════════════════════════════════════════╗");
    LOG_INFO("SPLASH", "║      SPLASH SCREEN - TEST IMAGE MODE           ║");
    LOG_INFO("SPLASH", "╚════════════════════════════════════════════════╝");
    LOG_INFO("SPLASH", "DEBUG: Using GENERATED test image (vertical stripes)");
    LOG_INFO("SPLASH", "DEBUG: If you see color stripes, LVGL display works!");

    // === SYSTEM CHECK ===
    LOG_INFO("SPLASH", "--- PHASE 1: SYSTEM CHECK ---");
    if (!HAL::Display::LvglDriver::get_display()) {
        LOG_ERROR("SPLASH", "✗ LVGL display not ready!");
        return;
    }
    LOG_INFO("SPLASH", "✓ Display driver ready");

    // DEBUG: IMPORTANT - Set current active screen to BLACK to prevent white flash
    LOG_DEBUG("SPLASH", "Setting current screen to black to prevent flash...");
    lv_obj_t* current_scr = lv_scr_act();
    if (current_scr) {
        lv_obj_set_style_bg_color(current_scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(current_scr, LV_OPA_COVER, 0);
        lv_timer_handler();  // Process immediately
        smart_delay(50);
    }
    LOG_DEBUG("SPLASH", "✓ Current screen set to black");

    // === CREATE TEST IMAGE ===
    LOG_INFO("SPLASH", "--- PHASE 2: CREATING TEST IMAGE ---");
    // DEBUG: This creates a simple color stripe pattern without decoding JPEG
    if (!create_test_image()) {
        LOG_ERROR("SPLASH", "✗ Failed to create test image");
        return;
    }
    LOG_INFO("SPLASH", "✓ Test image ready (6 color stripes: R,G,B,Y,C,M)");

    // === CREATE SPLASH SCREEN ===
    LOG_INFO("SPLASH", "--- PHASE 3: CREATING SPLASH SCREEN ---");
    // DEBUG: Create blank screen
    lv_obj_t* splash_scr = lv_obj_create(NULL);
    if (!splash_scr) {
        LOG_ERROR("SPLASH", "✗ Failed to create splash screen object");
        return;
    }
    LOG_DEBUG("SPLASH", "DEBUG: Screen object at 0x%08X", (uint32_t)splash_scr);
    
    // Remove theme/default styles to avoid inherited white background frame
    lv_obj_remove_style_all(splash_scr);

    // DEBUG: Configure screen properties - EXPLICITLY set black with full opacity
    lv_obj_set_size(splash_scr, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(splash_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(splash_scr, LV_OPA_COVER, 0);  // Explicitly set to fully opaque
    lv_obj_set_style_border_width(splash_scr, 0, 0);
    lv_obj_set_style_pad_all(splash_scr, 0, 0);
    LOG_INFO("SPLASH", "✓ Screen configured: %ux%u, black background (opaque)",
             LV_HOR_RES_MAX, LV_VER_RES_MAX);

    // === CREATE IMAGE WIDGET ===
    LOG_INFO("SPLASH", "--- PHASE 4: CREATING IMAGE WIDGET ---");
    // DEBUG: Create image widget
    lv_obj_t* img = lv_img_create(splash_scr);
    if (!img) {
        LOG_ERROR("SPLASH", "✗ Failed to create image widget");
        return;
    }
    LOG_DEBUG("SPLASH", "DEBUG: Image widget at 0x%08X", (uint32_t)img);

    // Remove all inherited styles so image object itself cannot draw white bg
    lv_obj_remove_style_all(img);

    // Force fully transparent widget background/border (only image pixels visible)
    lv_obj_set_style_bg_opa(img, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(img, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_outline_opa(img, LV_OPA_TRANSP, LV_PART_MAIN);

    // DEBUG: Set image source to our test pattern descriptor
    LOG_INFO("SPLASH", "Setting image source...");
    lv_img_set_src(img, &s_splash_img_dsc);
    LOG_DEBUG("SPLASH", "DEBUG: Image descriptor at 0x%08X", (uint32_t)&s_splash_img_dsc);
    LOG_DEBUG("SPLASH", "DEBUG: Image data at 0x%08X", (uint32_t)s_splash_img_data);

    // DEBUG: Position image at top-left (0,0)
    lv_obj_set_pos(img, 0, 0);
    LOG_INFO("SPLASH", "✓ Image positioned at (0, 0)");

    // DEBUG: Set initial opacity to 0 (transparent) for fade-in test
    lv_obj_set_style_img_opa(img, LV_OPA_0, 0);
    LOG_INFO("SPLASH", "✓ Image opacity set to 0 (starts transparent)");

    // === LOAD SCREEN ===
    LOG_INFO("SPLASH", "--- PHASE 5: LOADING SCREEN TO DISPLAY ---");
    // DEBUG: Load the screen
    LOG_INFO("SPLASH", "DEBUG STOP 1: About to load splash screen to LVGL display");
    smart_delay(500);  // Pause for observation
    
    lv_scr_load(splash_scr);
    LOG_INFO("SPLASH", "✓ Splash screen loaded (DISPLAY SHOULD SHOW NOW)");
    LOG_INFO("SPLASH", "DEBUG STOP 2: Screen loaded, about to pump LVGL and render");
    smart_delay(500);  // Pause for observation
    LOG_INFO("SPLASH", "DEBUG: If screen is BLACK with no stripes: check image isn't rendering");
    
    // Pump LVGL to process screen load
    LOG_INFO("SPLASH", "DEBUG STOP 3: Pumping LVGL timer handler and refreshing display...");
    lv_timer_handler();
    lv_refr_now(HAL::Display::LvglDriver::get_display());
    LOG_INFO("SPLASH", "✓ LVGL refresh complete");
    smart_delay(100);
    
    // Backlight is already ON (turned on in lvgl_driver init after black bootstrap)
    // No DISPON needed - panel is already enabled by TFT_eSPI init
    LOG_INFO("SPLASH", "✓ Screen loaded and rendered (backlight already on)");
    // === FADE-IN ANIMATION ===
    LOG_INFO("SPLASH", "--- PHASE 6: FADE-IN ANIMATION ---");
    uint32_t fade_in_start = millis();
    uint32_t fade_in_duration = LayoutSpec::Timing::SPLASH_FADE_IN_MS;
    
    LOG_INFO("SPLASH", "DEBUG: Starting fade-in: opacity 0 -> 255 over %ums", fade_in_duration);
    LOG_INFO("SPLASH", "DEBUG STOP 9: About to start fade-in animation");
    LOG_INFO("SPLASH", "DEBUG: You should see stripes FADE IN gradually");
    smart_delay(300);  // Pause before fade-in starts
    
    // DEBUG: Create animation structure
    lv_anim_t anim_in;
    lv_anim_init(&anim_in);
    lv_anim_set_var(&anim_in, img);
    
    // DEBUG: Use lambda to log opacity changes
    lv_anim_set_exec_cb(&anim_in, [](void* var, int32_t value) {
        // DEBUG: Log every ~50 steps
        static int32_t last_logged = -1;
        if (value != last_logged && (value % 50) == 0) {
            LOG_DEBUG("SPLASH", "DEBUG: Animation opacity = %u", (uint8_t)value);
            last_logged = value;
        }
        lv_obj_set_style_img_opa((lv_obj_t*)var, (uint8_t)value, 0);
    });
    
    lv_anim_set_time(&anim_in, fade_in_duration);
    lv_anim_set_values(&anim_in, LV_OPA_0, LV_OPA_COVER);
    lv_anim_start(&anim_in);
    LOG_INFO("SPLASH", "✓ Fade-in animation STARTED");
    LOG_INFO("SPLASH", "DEBUG STOP 10: Fade-in animation is running (opacity: 0->255)");
    
    // Wait for animation to complete
    uint32_t fade_in_frames = 0;
    while (lv_anim_count_running() > 0) {
        lv_timer_handler();
        fade_in_frames++;
        smart_delay(10);
        
        if (millis() - fade_in_start > (fade_in_duration + 1000)) {
            LOG_WARN("SPLASH", "✗ Fade-in TIMEOUT after %ums", millis() - fade_in_start);
            break;
        }
    }
    uint32_t fade_in_actual = millis() - fade_in_start;
    LOG_INFO("SPLASH", "✓ Fade-in COMPLETE: %u frames, %ums", fade_in_frames, fade_in_actual);
    LOG_INFO("SPLASH", "DEBUG: Image should now be FULLY VISIBLE");
    LOG_INFO("SPLASH", "DEBUG STOP 11: Fade-in animation complete, stripes should be fully visible!");
    smart_delay(500);  // Pause to observe final result

    // === HOLD PHASE ===
    LOG_INFO("SPLASH", "--- PHASE 7: HOLD PHASE ---");
    uint32_t hold_duration = LayoutSpec::Timing::SPLASH_HOLD_MS;
    uint32_t hold_start = millis();
    
    LOG_INFO("SPLASH", "DEBUG STOP 12: Entering hold phase for %ums - OBSERVE STRIPES NOW", hold_duration);
    LOG_INFO("SPLASH", "DEBUG: Displaying image for %ums (you should see FULL STRIPES)", hold_duration);
    uint32_t hold_frames = 0;
    
    while ((millis() - hold_start) < hold_duration) {
        lv_timer_handler();
        hold_frames++;
        smart_delay(20);
    }
    uint32_t hold_actual = millis() - hold_start;
    LOG_INFO("SPLASH", "✓ Hold COMPLETE: %u frames, %ums", hold_frames, hold_actual);

    // === FADE-OUT ANIMATION ===
    LOG_INFO("SPLASH", "--- PHASE 8: FADE-OUT ANIMATION ---");
    uint32_t fade_out_start = millis();
    uint32_t fade_out_duration = LayoutSpec::Timing::SPLASH_FADE_OUT_MS;
    
    LOG_INFO("SPLASH", "DEBUG: Starting fade-out: opacity 255 -> 0 over %ums", fade_out_duration);
    LOG_INFO("SPLASH", "DEBUG STOP 13: About to start fade-out animation");
    smart_delay(300);  // Pause before fade-out
    LOG_INFO("SPLASH", "DEBUG: You should see stripes FADE OUT gradually");
    
    // DEBUG: Create fade-out animation
    lv_anim_t anim_out;
    lv_anim_init(&anim_out);
    lv_anim_set_var(&anim_out, img);
    
    // DEBUG: Log opacity changes during fade-out
    lv_anim_set_exec_cb(&anim_out, [](void* var, int32_t value) {
        static int32_t last_logged = -1;
        if (value != last_logged && (value % 50) == 0) {
            LOG_DEBUG("SPLASH", "DEBUG: Animation opacity = %u", (uint8_t)value);
            last_logged = value;
        }
        lv_obj_set_style_img_opa((lv_obj_t*)var, (uint8_t)value, 0);
    });
    
    lv_anim_set_time(&anim_out, fade_out_duration);
    lv_anim_set_values(&anim_out, LV_OPA_COVER, LV_OPA_0);
    lv_anim_start(&anim_out);
    LOG_INFO("SPLASH", "✓ Fade-out animation STARTED");

    // DEBUG: Wait for fade-out
    uint32_t fade_out_frames = 0;
    LOG_INFO("SPLASH", "DEBUG: Waiting for fade-out to complete...");
    LOG_INFO("SPLASH", "DEBUG STOP 14: Fade-out animation is running (opacity: 255->0)");
    while (lv_anim_count_running() > 0) {
        lv_timer_handler();
        fade_out_frames++;
        smart_delay(10);
        
        if (millis() - fade_out_start > (fade_out_duration + 1000)) {
            LOG_WARN("SPLASH", "✗ Fade-out TIMEOUT after %ums", millis() - fade_out_start);
            break;
        }
    }
    uint32_t fade_out_actual = millis() - fade_out_start;
    LOG_INFO("SPLASH", "✓ Fade-out COMPLETE: %u frames, %ums", fade_out_frames, fade_out_actual);
    LOG_INFO("SPLASH", "DEBUG: Image should now be INVISIBLE");

    // === CLEANUP & TRANSITION ===
    LOG_INFO("SPLASH", "--- PHASE 9: CLEANUP & TRANSITION ---");
    
    // DEBUG: DON'T delete splash screen manually - lv_scr_load() will handle it
    // Manually deleting breaks LVGL's display state
    LOG_INFO("SPLASH", "Transitioning to Ready screen (LVGL will auto-cleanup splash)...");
    
    // Load the Ready screen - this will replace the splash screen
    // IMPORTANT: Do NOT delete splash_scr, lv_scr_load() handles cleanup
    LOG_INFO("SPLASH", "DEBUG STOP 15: Splash fade-out complete, loading Ready screen");
    smart_delay(500);  // Final pause to observe black screen
    
    display_initial_screen_lvgl();
    LOG_INFO("SPLASH", "✓ Ready screen loaded");
    LOG_INFO("SPLASH", "DEBUG STOP 16: Ready screen is now visible");
    smart_delay(500);  // Observe Ready screen

    // DEBUG: Free PSRAM image buffer NOW (after new screen is active and stable)
    smart_delay(100);  // Give LVGL time to stabilize
    if (s_splash_img_data) {
        LOG_INFO("SPLASH", "Freeing test image buffer...");
        heap_caps_free(s_splash_img_data);
        s_splash_img_data = nullptr;
        LOG_DEBUG("SPLASH", "DEBUG: Buffer freed");
    }
    
    LOG_INFO("SPLASH", "╔════════════════════════════════════════════════════╗");
    LOG_INFO("SPLASH", "║  === SPLASH SEQUENCE COMPLETE ===                 ║");
    LOG_INFO("SPLASH", "║  ✓ Black background loaded (NO WHITE BLOCK!)      ║");
    LOG_INFO("SPLASH", "║  ✓ Stripes displayed and faded                    ║");
    LOG_INFO("SPLASH", "║  ✓ Ready screen loaded                            ║");
    LOG_INFO("SPLASH", "║  ✓ System ready for operation                     ║");
    LOG_INFO("SPLASH", "╚════════════════════════════════════════════════════╝");
}

/**
 * Load the main "Ready" screen after splash completes.
 * Simple screen load without animation - LVGL will auto-cleanup old screen.
 */
void display_initial_screen_lvgl() {
    LOG_DEBUG("SPLASH", "Creating Ready screen...");

    // Create screen - do minimal setup to avoid LVGL state corruption
    lv_obj_t* ready_scr = lv_obj_create(NULL);
    if (!ready_scr) {
        LOG_ERROR("SPLASH", "Failed to create Ready screen");
        return;
    }

    // Minimal configuration
    lv_obj_set_size(ready_scr, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(ready_scr, lv_color_black(), 0);
    
    // Create label
    lv_obj_t* lbl = lv_label_create(ready_scr);
    if (lbl) {
        lv_label_set_text(lbl, "Ready");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }

    // Load screen - LVGL will auto-cleanup old screen
    LOG_DEBUG("SPLASH", "Loading Ready screen to display...");
    lv_scr_load(ready_scr);
    
    // Pump LVGL to ensure it processes
    lv_timer_handler();
    LOG_DEBUG("SPLASH", "✓ Ready screen loaded and active");
}

} // namespace Display

#endif // USE_LVGL


