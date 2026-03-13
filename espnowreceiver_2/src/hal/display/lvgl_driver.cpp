/**
 * LVGL Display Driver Implementation
 */

#ifdef USE_LVGL

#include "lvgl_driver.h"
#include "../../common.h"
#include "../../helpers.h"
#include <esp_heap_caps.h>
#include <esp_log.h>

namespace HAL {
namespace Display {

// Static member initialization
TFT_eSPI* LvglDriver::tft_ = nullptr;
lv_disp_drv_t LvglDriver::disp_drv_;
lv_disp_draw_buf_t LvglDriver::disp_buf_;
lv_disp_t* LvglDriver::disp_ = nullptr;
lv_color_t* LvglDriver::buf1_ = nullptr;
lv_color_t* LvglDriver::buf2_ = nullptr;
uint8_t LvglDriver::current_backlight_ = 0;

bool LvglDriver::init(TFT_eSPI& tft) {
    LOG_INFO("LVGL", "Initializing LVGL display driver...");
    LOG_INFO("LVGL", "Following LilyGo T-Display-S3 bring-up sequence:");
    LOG_INFO("LVGL", "  1. Backlight OFF");
    LOG_INFO("LVGL", "  2. Panel init");
    LOG_INFO("LVGL", "  3. LVGL init");
    LOG_INFO("LVGL", "  4. Register display + render black");
    LOG_INFO("LVGL", "  5. Enable backlight with CRITICAL gradient fade");
    
    tft_ = &tft;
    
    // STEP 1 & 2: Initialize TFT hardware (backlight OFF, panel init)
    LOG_INFO("LVGL", "STEP 1-2: Hardware initialization (backlight OFF, panel ON)");
    init_hardware();
    LOG_INFO("LVGL", "  Hardware ready, backlight at PWM 0");
    
    // STEP 3: Initialize LVGL core AFTER hardware is ready
    LOG_INFO("LVGL", "STEP 3: Initializing LVGL core");
    lv_init();
    LOG_INFO("LVGL", "  LVGL core initialized");
    
    // Allocate display buffers in PSRAM
    if (!allocate_buffers()) {
        LOG_ERROR("LVGL", "Failed to allocate display buffers");
        return false;
    }
    LOG_INFO("LVGL", "  Display buffers allocated");
    
    // Initialize display buffer (double buffering)
    const uint32_t buf_size = ::Display::SCREEN_WIDTH * ::Display::SCREEN_HEIGHT / 10;  // 1/10 screen
    lv_disp_draw_buf_init(&disp_buf_, buf1_, buf2_, buf_size);
    LOG_INFO("LVGL", "  Display buffer init complete");
    
    // Initialize display driver
    lv_disp_drv_init(&disp_drv_);
    disp_drv_.hor_res = ::Display::SCREEN_WIDTH;
    disp_drv_.ver_res = ::Display::SCREEN_HEIGHT;
    disp_drv_.flush_cb = flush_cb;
    disp_drv_.draw_buf = &disp_buf_;
    
    // STEP 4: Register display driver with LVGL
    LOG_INFO("LVGL", "STEP 4: Registering display with LVGL");
    disp_ = lv_disp_drv_register(&disp_drv_);
    if (!disp_) {
        LOG_ERROR("LVGL", "Failed to register display driver");
        // Free allocated buffers on failure
        heap_caps_free(buf1_);
        heap_caps_free(buf2_);
        buf1_ = nullptr;
        buf2_ = nullptr;
        return false;
    }
    LOG_INFO("LVGL", "  Display registered (disp=0x%08X)", (uint32_t)disp_);

    // Set display background to black globally
    lv_disp_set_bg_color(disp_, lv_color_black());
    lv_disp_set_bg_opa(disp_, LV_OPA_COVER);
    LOG_INFO("LVGL", "  Display background set to black");
    
    LOG_INFO("LVGL", "TFT_eSPI hardware initialized (resolution: %dx%d)", 
             disp_drv_.hor_res, disp_drv_.ver_res);

    // Create black bootstrap screen using LVGL (not direct TFT writes!)
    LOG_INFO("LVGL", "  Creating black bootstrap screen with LVGL");
    lv_obj_t* boot_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(boot_scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(boot_scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(boot_scr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(boot_scr, 0, LV_PART_MAIN);
    lv_scr_load(boot_scr);
    
    // Let LVGL render the black bootstrap to the display (backlight still OFF)
    LOG_INFO("LVGL", "  Rendering black bootstrap screen via LVGL");
    lv_timer_handler();
    lv_refr_now(disp_);  // Force immediate full refresh
    LOG_INFO("LVGL", "  Bootstrap rendered - display RAM has black pixels");
    
    // STEP 5: Turn backlight ON now (LilyGo reference sequence)
    // Per official lv_demos.ino: backlight fades in AFTER lv_init() but BEFORE splash
    // This prevents white block because LVGL has already rendered black frame to GRAM
    LOG_INFO("LVGL", "STEP 5: Fading in backlight gradually (LilyGo sequence)");
    for (uint8_t i = 0; i < 255; i++) {
        #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
        ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, i);
        #else
        ledcWrite(HardwareConfig::GPIO_BACKLIGHT, i);
        #endif
        smart_delay(2);
    }
    current_backlight_ = 255;

    LOG_INFO("LVGL", "Backlight ON at full brightness - display ready");
    LOG_DEBUG("LVGL", "Display ready (backlight ON, black frame visible)");
    
    return true;
}

void LvglDriver::init_hardware() {
    LOG_DEBUG("LVGL", "Initializing hardware (TFT-compatible sequence)...");

    // Keep backlight hard OFF before panel init (same as TFT path)
    pinMode(HardwareConfig::GPIO_BACKLIGHT, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_BACKLIGHT, LOW);
    smart_delay(5);
    LOG_WARN("LVGL", "DEBUG: Backlight set to LOW");

    // Enable panel power first (same as TFT path)
    pinMode(HardwareConfig::GPIO_DISPLAY_POWER, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_DISPLAY_POWER, HIGH);
    smart_delay(100);
    LOG_WARN("LVGL", "DEBUG: Display power enabled, waiting 100ms");
    LOG_DEBUG("LVGL", "Display power enabled (GPIO %d)", HardwareConfig::GPIO_DISPLAY_POWER);

    // Configure backlight PWM at 0
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcSetup(HardwareConfig::BACKLIGHT_PWM_CHANNEL, 
              HardwareConfig::BACKLIGHT_FREQUENCY_HZ, 
              HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
    ledcAttachPin(HardwareConfig::GPIO_BACKLIGHT, HardwareConfig::BACKLIGHT_PWM_CHANNEL);
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, 0);  // Start at 0
    #else
    ledcAttach(HardwareConfig::GPIO_BACKLIGHT, 
               HardwareConfig::BACKLIGHT_FREQUENCY_HZ, 
               HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
    ledcWrite(HardwareConfig::GPIO_BACKLIGHT, 0);  // Start at 0
    #endif
    LOG_WARN("LVGL", "DEBUG: Backlight PWM configured at 0");

    // Initialize TFT (this sends DISPON from tft->init())
    LOG_WARN("LVGL", "DEBUG: Calling tft->init()");
    tft_->init();
    LOG_WARN("LVGL", "DEBUG: tft->init() complete");
    
    // CRITICAL FIX: Send DISPOFF immediately after tft->init()
    // This prevents the panel from showing garbage GRAM content
    // while we initialize LVGL rendering
    LOG_WARN("LVGL", "DEBUG: Sending DISPOFF command to LCD panel");
    tft_->writecommand(0x28);  // ST7789 DISPOFF command
    smart_delay(10);
    LOG_WARN("LVGL", "DEBUG: Panel display OFF - no garbage visible during LVGL init");
    
    tft_->setRotation(1);  // Landscape
    tft_->setSwapBytes(true);

    LOG_DEBUG("LVGL", "Backlight PWM initialized at 0 (GPIO %d)", HardwareConfig::GPIO_BACKLIGHT);
}

bool LvglDriver::allocate_buffers() {
    const uint32_t buf_size = ::Display::SCREEN_WIDTH * ::Display::SCREEN_HEIGHT / 10;
    const size_t buf_bytes = buf_size * sizeof(lv_color_t);
    
    LOG_INFO("LVGL", "Allocating display buffers in PSRAM (%d bytes each)...", buf_bytes);
    
    // Allocate buffer 1 in PSRAM (preferred)
    buf1_ = (lv_color_t*)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf1_) {
        LOG_WARN("LVGL", "PSRAM allocation failed for buffer 1, trying internal RAM");
        buf1_ = (lv_color_t*)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!buf1_) {
        LOG_ERROR("LVGL", "Failed to allocate display buffer 1");
        return false;
    }
    LOG_DEBUG("LVGL", "Buffer 1 allocated at 0x%08X", (uint32_t)buf1_);
    
    // CRITICAL: Pre-fill buffer 1 with black (0x0000 for RGB565) to prevent random data flash
    LOG_WARN("LVGL", "DEBUG: Pre-filling buffer 1 with black");
    memset(buf1_, 0x00, buf_bytes);
    
    // Allocate buffer 2 in PSRAM (double buffering), fallback to internal RAM
    buf2_ = (lv_color_t*)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf2_) {
        LOG_WARN("LVGL", "PSRAM allocation failed for buffer 2, trying internal RAM");
        buf2_ = (lv_color_t*)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!buf2_) {
        LOG_ERROR("LVGL", "Failed to allocate display buffer 2");
        heap_caps_free(buf1_);
        buf1_ = nullptr;
        return false;
    }
    LOG_DEBUG("LVGL", "Buffer 2 allocated at 0x%08X", (uint32_t)buf2_);
    
    // CRITICAL: Pre-fill buffer 2 with black (0x0000 for RGB565) to prevent random data flash
    LOG_WARN("LVGL", "DEBUG: Pre-filling buffer 2 with black");
    memset(buf2_, 0x00, buf_bytes);
    
    LOG_INFO("LVGL", "Display buffers allocated successfully");
    return true;
}

void LvglDriver::flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
    if (!tft_) {
        lv_disp_flush_ready(disp);
        return;
    }
    
    // Calculate region dimensions
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    // Set TFT window for this region
    tft_->startWrite();
    tft_->setAddrWindow(area->x1, area->y1, w, h);
    
    // Push pixels to display
    // Pass false to pushColors: _swapBytes is already true from setSwapBytes(true)
    // in init(), and with LV_COLOR_16_SWAP=1, LVGL's color_p buffer is already
    // in the correct byte order for the hardware. Passing true would double-swap.
    tft_->pushColors((uint16_t*)color_p, w * h, false);
    
    tft_->endWrite();
    
    // Inform LVGL that flush is complete
    lv_disp_flush_ready(disp);
}

/**
 * Backlight animation callback - updates PWM when animating brightness
 */
static void backlight_anim_cb(void* var, int32_t v) {
    HAL::Display::LvglDriver::set_backlight((uint8_t)v);
}

void LvglDriver::task_handler() {
    lv_timer_handler();
}

/**
 * Animate backlight brightness over specified duration
 * Useful for smooth fade-in/fade-out effects
 */
void LvglDriver::animate_backlight_to(uint8_t target, uint32_t duration_ms) {
    if (duration_ms == 0) {
        set_backlight(target);
        return;
    }
    
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_values(&a, current_backlight_, target);
    lv_anim_set_time(&a, duration_ms);
    lv_anim_set_exec_cb(&a, backlight_anim_cb);
    lv_anim_start(&a);
    
    LOG_INFO("LVGL", "Backlight animation: %u -> %u over %ums", 
             (unsigned)current_backlight_, (unsigned)target, (unsigned)duration_ms);
}

void LvglDriver::set_backlight(uint8_t brightness) {
    static int16_t last_logged = -1;
    current_backlight_ = brightness;
    
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, brightness);
    #else
    ledcWrite(HardwareConfig::GPIO_BACKLIGHT, brightness);
    #endif

    // Debug logging with low noise: boundaries + coarse step deltas only
    if (last_logged < 0 || brightness == 0 || brightness == 255 ||
        (uint8_t)last_logged == 0 || (uint8_t)last_logged == 255 ||
        ((int)brightness - last_logged >= 50) || ((int)brightness - last_logged <= -50)) {
        LOG_INFO("LVGL", "Backlight PWM write: brightness=%u", (unsigned)brightness);
        last_logged = brightness;
    }
}

uint8_t LvglDriver::get_backlight() {
    return current_backlight_;
}

lv_disp_t* LvglDriver::get_display() {
    return disp_;
}

TFT_eSPI* LvglDriver::get_tft() {
    return tft_;
}

} // namespace Display
} // namespace HAL

// ============================================================================
// PSRAM Memory Allocators for LVGL
// ============================================================================

void* lv_custom_mem_alloc(size_t size) {
    // Allocate from PSRAM if available, otherwise internal RAM
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        // Fallback to internal RAM if PSRAM allocation fails
        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

void lv_custom_mem_free(void* ptr) {
    if (ptr) {
        heap_caps_free(ptr);
    }
}

void* lv_custom_mem_realloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return lv_custom_mem_alloc(new_size);
    }
    
    if (new_size == 0) {
        lv_custom_mem_free(ptr);
        return nullptr;
    }
    
    // Reallocate (tries PSRAM first, then internal RAM)
    void* new_ptr = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!new_ptr) {
        new_ptr = heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    
    return new_ptr;
}

// ============================================================================
// Custom Logging for LVGL
// ============================================================================

void lv_custom_log(const char* buf) {
    // Forward LVGL logs to ESP-IDF logging system
    ESP_LOGW("LVGL", "%s", buf);
}

#endif // USE_LVGL
