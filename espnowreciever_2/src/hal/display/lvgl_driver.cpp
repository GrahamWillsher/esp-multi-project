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
    
    tft_ = &tft;
    
    // Initialize hardware (GPIO, backlight PWM)
    init_hardware();
    
    // Initialize LVGL core
    lv_init();
    LOG_INFO("LVGL", "LVGL core initialized");
    
    // Allocate display buffers in PSRAM
    if (!allocate_buffers()) {
        LOG_ERROR("LVGL", "Failed to allocate display buffers");
        return false;
    }
    
    // Initialize display buffer (double buffering)
    const uint32_t buf_size = ::Display::SCREEN_WIDTH * ::Display::SCREEN_HEIGHT / 10;  // 1/10 screen
    lv_disp_draw_buf_init(&disp_buf_, buf1_, buf2_, buf_size);
    LOG_INFO("LVGL", "Display buffers initialized: %d pixels per buffer", buf_size);
    
    // Initialize display driver
    lv_disp_drv_init(&disp_drv_);
    disp_drv_.hor_res = ::Display::SCREEN_WIDTH;
    disp_drv_.ver_res = ::Display::SCREEN_HEIGHT;
    disp_drv_.flush_cb = flush_cb;
    disp_drv_.draw_buf = &disp_buf_;
    
    // Register display driver
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
    
    LOG_INFO("LVGL", "Display driver registered successfully (disp=0x%08X)", (uint32_t)disp_);
    
    // Initialize TFT_eSPI (same as TFT version)
    LOG_DEBUG("LVGL", "Initializing TFT_eSPI hardware...");
    tft_->init();
    tft_->setRotation(1);  // Landscape
    tft_->fillScreen(TFT_BLACK);
    tft_->setSwapBytes(true);
    
    LOG_INFO("LVGL", "TFT_eSPI hardware initialized (resolution: %dx%d)", 
             disp_drv_.hor_res, disp_drv_.ver_res);
    
    // Set backlight to 0 initially
    set_backlight(0);
    
    return true;
}

void LvglDriver::init_hardware() {
    LOG_DEBUG("LVGL", "Initializing display hardware...");
    
    // Initialize display power GPIO
    pinMode(HardwareConfig::GPIO_DISPLAY_POWER, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_DISPLAY_POWER, HIGH);  // Power ON
    LOG_DEBUG("LVGL", "Display power enabled (GPIO %d)", HardwareConfig::GPIO_DISPLAY_POWER);
    
    // Initialize backlight PWM
    pinMode(HardwareConfig::GPIO_BACKLIGHT, OUTPUT);
    
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
    
    LOG_DEBUG("LVGL", "Backlight PWM initialized (GPIO %d, %d Hz, %d-bit)",
              HardwareConfig::GPIO_BACKLIGHT,
              HardwareConfig::BACKLIGHT_FREQUENCY_HZ,
              HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
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
