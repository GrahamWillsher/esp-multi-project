# LVGL Implementation Analysis & Complete Development Plan

**Document Version:** 1.0  
**Date:** March 3, 2026  
**Status:** Comprehensive Analysis & Planning Document  
**Target:** Replicate TFT display functionality using LVGL with compile-time backend selection

---

## EXECUTIVE SUMMARY

This document outlines a complete strategy to replicate the production-grade TFT display implementation using LVGL as an alternative backend. The goal is **functional equivalence** with compile-time build selection (`USE_TFT` vs `USE_LVGL`) rather than runtime decisions.

**Key Objectives:**
- ✅ Achieve identical visual output (splash screen, SOC display, power bar, power text, error states)
- ✅ Maintain compile-time backend selection for clean binary separation
- ✅ Leverage LVGL's native animation system (instead of manual loops)
- ✅ Reuse shared layout specification (display_layout_spec.h) for both backends
- ✅ Achieve similar performance and memory efficiency
- ✅ Maintain the asynchronous event-driven architecture LVGL provides

**Current Status:**
- ✅ Infrastructure started (lvgl_driver.h, display_splash_lvgl.cpp)
- ✅ Layout specification already backend-agnostic
- ✅ HAL abstraction layer in place
- ⏳ Full LVGL widget implementation needed
- ⏳ Animation system completion needed
- ⏳ Testing and validation required

---

## SECTION 1: ARCHITECTURAL OVERVIEW

### 1.1 Fundamental Differences: TFT vs LVGL

| Aspect | TFT | LVGL |
|--------|-----|------|
| **Rendering Model** | Synchronous, blocking | Asynchronous, event-driven |
| **Animation** | Manual loops with delay() | Built-in animation framework (lv_anim_t) |
| **Updates** | Direct pixel writes | Invalidation + redraw queues |
| **Widgets** | None; custom drawing | Rich widget library |
| **Memory** | Minimal; direct display | Display buffers + widget trees |
| **Concurrency** | None (blocking) | Task-based (requires pumping) |
| **User Events** | Polling | Event-driven callbacks |

### 1.2 Design Strategy: Unified Interface with Different Backends

The `IDisplay` interface (display_interface.h) serves as the contract both backends must satisfy:

```cpp
class IDisplay {
    virtual bool init() = 0;
    virtual void display_splash_with_fade() = 0;
    virtual void display_initial_screen() = 0;
    virtual void update_soc(float soc_percent) = 0;
    virtual void update_power(int32_t power_w) = 0;
    virtual void show_status_page() = 0;
    virtual void show_error_state() = 0;
    virtual void show_fatal_error(const char* component, const char* message) = 0;
    virtual void task_handler() = 0;  // Key difference: LVGL needs periodic pumping
};
```

**Compile-time Backend Selection:**

```cpp
// In display_manager.cpp / display.cpp
#ifdef USE_TFT
    #include "tft_impl/tft_display.h"
    static TftDisplay* g_display = nullptr;
#elif defined(USE_LVGL)
    #include "lvgl_impl/lvgl_display.h"
    static LvglDisplay* g_display = nullptr;
#endif
```

**Build Integration:**

```ini
; platformio.ini
[env:receiver_tft]
build_flags = -DUSE_TFT

[env:receiver_lvgl]
build_flags = -DUSE_LVGL
```

---

## SECTION 2: FEATURE-BY-FEATURE ANALYSIS

### 2.1 FEATURE: Splash Screen with Fade Animation

**TFT Implementation:**
```cpp
// Synchronous, blocking
animate_backlight(0);           // Off
load_and_draw_splash();         // Draw image
animate_backlight(255, 3000);   // Fade in over 3 seconds (blocking)
smart_delay(2000);              // Hold for 2 seconds
animate_backlight(0, 3000);     // Fade out over 3 seconds (blocking)
// Function returns; splash is gone
```

**LVGL Implementation Strategy:**
```cpp
// Asynchronous, event-driven
void display_splash_with_fade() {
    // 1. Create splash screen object
    lv_obj_t* splash_scr = lv_obj_create(nullptr);
    lv_scr_load_anim(splash_scr, LV_SCR_LOAD_ANIM_FADE_IN, 3000, 0, false);
    
    // 2. Load image into LVGL image widget
    lv_obj_t* img = lv_img_create(splash_scr);
    lv_img_set_src(img, &s_img_dsc);  // Pre-decoded RGB565 buffer
    
    // 3. Backlight animation
    LvglDriver::animate_backlight_to(255, 3000);
    
    // 4. Hold screen
    // Set a timer to trigger fade-out after hold duration
    lv_timer_t* hold_timer = lv_timer_create(
        [](lv_timer_t* timer) {
            // Fade out
            lv_scr_load_anim(nullptr, LV_SCR_LOAD_ANIM_FADE_OUT, 3000, 2000, true);
            LvglDriver::animate_backlight_to(0, 3000);
        },
        LayoutSpec::Timing::SPLASH_HOLD_MS,
        nullptr);
    
    // 5. Return immediately (animations happen in background)
}

// Main loop must call task_handler() regularly
void loop() {
    display_obj->task_handler();  // Pumps LVGL message loop
    // ... other work
}
```

**Key Differences:**
- ✅ LVGL animations run asynchronously; we return immediately
- ✅ Function returns; animations continue in background via timers
- ✅ Main loop must pump `lv_timer_handler()` regularly
- ⚠️ Need to manage screen lifecycle (delete splash when ready screen loads)

**Implementation Details:**

```cpp
namespace Display {

class LvglDisplay : public IDisplay {
private:
    lv_obj_t* splash_screen_ = nullptr;
    lv_img_dsc_t splash_img_dsc_;
    uint16_t* splash_img_buf_ = nullptr;
    bool splash_fade_out_queued_ = false;
    
public:
    void display_splash_with_fade() override {
        LOG_INFO("LVGL", "=== Splash START ===");
        
        // Decode JPEG from LittleFS
        if (!load_splash_image()) {
            show_splash_fallback("Cannot load JPEG");
            return;
        }
        
        // Create splash screen
        splash_screen_ = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(splash_screen_, lv_color_black(), 0);
        
        // Create image widget
        lv_obj_t* img = lv_img_create(splash_screen_);
        lv_img_set_src(img, &splash_img_dsc_);
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
        
        // Load with fade-in animation
        lv_scr_load_anim(splash_screen_, LV_SCR_LOAD_ANIM_FADE_IN, 
                        LayoutSpec::Timing::SPLASH_FADE_IN_MS, 0, false);
        
        // Start backlight fade-in
        LvglDriver::animate_backlight_to(255, LayoutSpec::Timing::SPLASH_FADE_IN_MS);
        
        // Schedule fade-out after hold period
        uint32_t hold_and_fade_time = 
            LayoutSpec::Timing::SPLASH_HOLD_MS + 
            LayoutSpec::Timing::SPLASH_FADE_OUT_MS;
        
        lv_timer_create(
            [](lv_timer_t* timer) {
                LvglDisplay* self = (LvglDisplay*)timer->user_data;
                self->on_splash_hold_complete();
            },
            LayoutSpec::Timing::SPLASH_HOLD_MS,
            this);
        
        LOG_INFO("LVGL", "Splash animation queued (3s fade-in + 2s hold + 3s fade-out)");
    }
    
private:
    void on_splash_hold_complete() {
        LOG_INFO("LVGL", "Splash hold complete, starting fade-out");
        
        // Fade out screen
        lv_scr_load_anim(nullptr, LV_SCR_LOAD_ANIM_FADE_OUT,
                        LayoutSpec::Timing::SPLASH_FADE_OUT_MS, 0, true);
        
        // Fade out backlight
        LvglDriver::animate_backlight_to(0, LayoutSpec::Timing::SPLASH_FADE_OUT_MS);
        
        LOG_INFO("LVGL", "=== Splash END ===");
    }
    
    bool load_splash_image() {
        // Use same JPEG decoding as TFT implementation
        const char* splash_path = LayoutSpec::Assets::SPLASH_IMAGE_PATH;
        uint16_t w, h;
        
        splash_img_buf_ = decode_jpg_to_rgb565(splash_path, w, h);
        if (!splash_img_buf_) {
            return false;
        }
        
        // Create LVGL image descriptor
        splash_img_dsc_.header.cf = LV_IMG_CF_TRUE_COLOR;
        splash_img_dsc_.header.w = w;
        splash_img_dsc_.header.h = h;
        splash_img_dsc_.data_size = w * h * sizeof(uint16_t);
        splash_img_dsc_.data = (const uint8_t*)splash_img_buf_;
        
        LOG_INFO("LVGL", "Splash image loaded: %ux%u", w, h);
        return true;
    }
};

} // namespace Display
```

**Challenges & Solutions:**

| Challenge | Solution |
|-----------|----------|
| Image data format (JPEG→RGB565) | Use same JPEGDecoder + buffer approach as TFT |
| Async animations mean function returns immediately | Caller must pump task_handler() in main loop |
| Screen cleanup (splash object lifetime) | Timer callback or custom animation complete callback |
| Backlight sync with screen animations | Chain animations: screen fade happens alongside backlight fade |

---

### 2.2 FEATURE: Ready Screen

**TFT Implementation:**
```cpp
void display_initial_screen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Ready", SOC_CENTER_X, SOC_CENTER_Y);
    animate_backlight(255, 300);  // Blocking fade-in
}
```

**LVGL Implementation:**
```cpp
void LvglDisplay::display_initial_screen() override {
    LOG_INFO("LVGL", "Displaying Ready screen...");
    
    // Delete splash screen if still exists
    if (splash_screen_) {
        lv_obj_del(splash_screen_);
        splash_screen_ = nullptr;
        if (splash_img_buf_) {
            heap_caps_free(splash_img_buf_);
            splash_img_buf_ = nullptr;
        }
    }
    
    // Create ready screen
    lv_obj_t* ready_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(ready_scr, lv_color_black(), 0);
    
    // Create "Ready" label
    lv_obj_t* label = lv_label_create(ready_scr);
    lv_label_set_text(label, "Ready");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    // Configure text style
    lv_obj_set_style_text_color(label, lv_color_green(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    
    // Load with fade-in
    lv_scr_load_anim(ready_scr, LV_SCR_LOAD_ANIM_FADE_IN,
                    LayoutSpec::Timing::READY_FADE_IN_MS, 0, false);
    
    // Animate backlight
    LvglDriver::animate_backlight_to(255, LayoutSpec::Timing::READY_FADE_IN_MS);
    
    LOG_INFO("LVGL", "Ready screen displayed");
}
```

**Key Differences:**
- LVGL uses label widgets instead of raw text drawing
- Font selection is done at creation time (lv_font_montserrat_24)
- Text color is a style property set with lv_obj_set_style_text_color()

---

### 2.3 FEATURE: SOC Display with Color Gradient

**TFT Implementation:**
```cpp
// Static color gradient pre-calculated
static uint16_t soc_color_gradient[501];  // RED→AMBER→LIME→GREEN over 0-100%

void draw_soc(float soc_percent) {
    int gradient_index = (int)((soc_percent / 100.0f) * 500);
    uint16_t color = soc_color_gradient[gradient_index];
    
    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(soc_text, SOC_CENTER_X, SOC_CENTER_Y);
}
```

**LVGL Implementation Strategy:**

LVGL doesn't have built-in color gradients, but we have options:

**Option A: Pre-calculated Color Array (Simplest, Matches TFT)**
```cpp
class LvglDisplay : public IDisplay {
private:
    lv_obj_t* soc_label_ = nullptr;
    uint16_t soc_color_gradient_[501];
    
    void init_soc_gradient() {
        // Use same color interpolation as TFT
        pre_calculate_color_gradient(TFT_RED, Display::AMBER, 167, 
                                   &soc_color_gradient_[0]);
        pre_calculate_color_gradient(Display::AMBER, Display::LIME, 167,
                                   &soc_color_gradient_[167]);
        pre_calculate_color_gradient(Display::LIME, TFT_GREEN, 166,
                                   &soc_color_gradient_[334]);
    }
    
public:
    void update_soc(float soc_percent) override {
        static float last_soc = -1.0f;
        
        if (last_soc == soc_percent) {
            return;  // No change
        }
        
        if (!soc_label_) {
            // Create SOC label on first call
            lv_obj_t* soc_container = lv_obj_create(lv_scr_act());
            lv_obj_align(soc_container, LV_ALIGN_TOP_MID, 0, 10);
            
            soc_label_ = lv_label_create(soc_container);
            lv_obj_align(soc_label_, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_text_font(soc_label_, &lv_font_montserrat_28, 0);
        }
        
        // Get gradient color
        int gradient_idx = (int)((soc_percent / 100.0f) * 500);
        gradient_idx = std::clamp(gradient_idx, 0, 500);
        lv_color_t color = lv_color_hex(soc_color_gradient_[gradient_idx]);
        
        // Update label
        char soc_text[16];
        snprintf(soc_text, sizeof(soc_text), "%.1f%%", soc_percent);
        lv_label_set_text(soc_label_, soc_text);
        lv_obj_set_style_text_color(soc_label_, color, 0);
        
        last_soc = soc_percent;
        LOG_DEBUG("LVGL", "SOC updated: %.1f%% color=0x%06X", soc_percent, 
                 soc_color_gradient_[gradient_idx]);
    }
};
```

**Option B: LVGL Color Gradients (More Complex, More Flexible)**
```cpp
// LVGL also supports gradient fills, but for text it's still manual
// Better to stick with Option A for simplicity
```

**Chosen Approach:** **Option A** (Pre-calculated array)
- ✅ Identical to TFT implementation
- ✅ No runtime color interpolation overhead
- ✅ Compile-time constants

---

### 2.4 FEATURE: Power Bar with Charging/Discharging Indicators

**TFT Implementation:**
```cpp
// Manual drawing of "-" characters in left/right directions
// with color gradients (BLUE→GREEN for charging, BLUE→RED for discharging)
// Pulse animation when power changes

void draw_power(int32_t power_w) {
    // Calculate bar count
    // Draw left bars (charging) or right bars (discharging)
    // With ripple animation if transition
    // Update power text below
}
```

**LVGL Implementation Strategy:**

LVGL has built-in progress bars (`lv_bar`) but they're designed for uni-directional progress (0-100%). For bidirectional power bars, we have options:

**Option A: Custom Widget (Full Control)**
```cpp
class PowerBarWidget {
private:
    lv_obj_t* container_ = nullptr;
    lv_obj_t* left_bar_ = nullptr;    // Charging side
    lv_obj_t* right_bar_ = nullptr;   // Discharging side
    lv_obj_t* center_marker_ = nullptr;
    lv_obj_t* power_text_ = nullptr;
    int32_t last_power_w_ = INT32_MAX;
    
public:
    void init(lv_obj_t* parent) {
        // Create container
        container_ = lv_obj_create(parent);
        lv_obj_set_size(container_, 320, 50);
        lv_obj_align(container_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
        
        // Create left bar (charging, goes left from center)
        left_bar_ = lv_bar_create(container_);
        lv_obj_set_size(left_bar_, 160, 20);
        lv_obj_align(left_bar_, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(left_bar_, lv_color_blue(), 0);
        
        // Create right bar (discharging, goes right from center)
        right_bar_ = lv_bar_create(container_);
        lv_obj_set_size(right_bar_, 160, 20);
        lv_obj_align(right_bar_, LV_ALIGN_RIGHT_MID, 0, 0);
        
        // Power text
        power_text_ = lv_label_create(container_);
        lv_obj_align(power_text_, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
    
    void update(int32_t power_w, int32_t max_power) {
        if (power_w == last_power_w_) return;
        
        // Clamp power
        power_w = std::clamp(power_w, -max_power, max_power);
        
        if (power_w < 0) {
            // Charging: show left bar
            int bars = (-power_w * 30) / max_power;
            lv_bar_set_value(left_bar_, bars, LV_ANIM_ON);
            lv_bar_set_value(right_bar_, 0, LV_ANIM_ON);
            lv_obj_set_style_bg_color(left_bar_, gradient_green[bars], 0);
        } else if (power_w > 0) {
            // Discharging: show right bar
            int bars = (power_w * 30) / max_power;
            lv_bar_set_value(right_bar_, bars, LV_ANIM_ON);
            lv_bar_set_value(left_bar_, 0, LV_ANIM_ON);
            lv_obj_set_style_bg_color(right_bar_, gradient_red[bars], 0);
        } else {
            // Zero
            lv_bar_set_value(left_bar_, 0, LV_ANIM_ON);
            lv_bar_set_value(right_bar_, 0, LV_ANIM_ON);
        }
        
        // Update power text
        char text[16];
        snprintf(text, sizeof(text), "%ldW", (long)power_w);
        lv_label_set_text(power_text_, text);
        
        last_power_w_ = power_w;
    }
};
```

**Option B: Canvas Drawing (Most Flexible)**
```cpp
// Use lv_canvas for custom drawing (similar to TFT pixel-by-pixel)
// More powerful but more complex
```

**Chosen Approach:** **Option A** (Custom widget with progress bars)
- ✅ Uses LVGL's built-in animation system (LV_ANIM_ON)
- ✅ Maintains bidirectional design
- ✅ Color gradients applied dynamically
- ✅ Less code than canvas approach

---

### 2.5 FEATURE: Simulated LED Indicator

**TFT Implementation:**
```cpp
// Circular LED at top-right corner with fade animations
// Colors: RED, GREEN, ORANGE
// Modes: set_led(), flash_led(), heartbeat_led()

void flash_led(LEDColor color, uint32_t cycle_duration_ms = 1000) {
    // Fade from color → background, then background → color
    // Animation loops: in + out phases
    // Used for data arrival, status changes
}

void heartbeat_led(LEDColor color, uint32_t cycle_duration_ms = 1200) {
    // Brief pulse (on 20% of cycle, off 80%)
    // Indicates system is alive and working
}

void set_led(LEDColor color) {
    // Solid color (no fade)
    tft.fillCircle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, color);
}

void clear_led() {
    // Turn off LED
    tft.fillCircle(LED_X_POSITION, LED_Y_POSITION, LED_RADIUS, background);
}
```

**LVGL Implementation Strategy:**

LVGL doesn't have built-in circle widgets, but we can use `lv_obj_t` with circular background styling:

```cpp
class LvglDisplay : public IDisplay {
private:
    lv_obj_t* led_indicator_ = nullptr;
    LEDColor last_led_color_ = LED_RED;
    
public:
    void init_led_indicator() {
        // Create LED object at middle-right (vertically centered)
        led_indicator_ = lv_obj_create(lv_scr_act());
        lv_obj_set_size(led_indicator_, 16, 16);  // Diameter ~8pt radius
        lv_obj_align(led_indicator_, LV_ALIGN_CENTER_RIGHT, -10, 0);
        
        // Configure as circular
        lv_obj_set_style_radius(led_indicator_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(led_indicator_, 0, 0);  // No border
        
        // Initial state: off (background color)
        lv_obj_set_style_bg_color(led_indicator_, lv_color_black(), 0);
    }
    
    void flash_led(LEDColor color, uint32_t cycle_duration_ms = 1000) {
        // Pre-calculate fade gradient from color to background
        lv_color_t led_color = get_led_color(color);
        lv_color_t bg_color = lv_color_black();
        
        // Animation: color → background → color
        // Phase 1: Fade out (color to background)
        lv_anim_t anim_out;
        lv_anim_init(&anim_out);
        lv_anim_set_var(&anim_out, led_indicator_);
        lv_anim_set_values(&anim_out, led_color.full, bg_color.full);
        lv_anim_set_time(&anim_out, cycle_duration_ms / 2);
        lv_anim_set_exec_cb(&anim_out, [](void* var, int32_t v) {
            // Interpolate color value
            lv_obj_set_style_bg_color((lv_obj_t*)var, lv_color_t{.full = (uint16_t)v}, 0);
        });
        
        // Phase 2: Fade in (background to color), delayed
        lv_anim_t anim_in;
        lv_anim_init(&anim_in);
        lv_anim_set_var(&anim_in, led_indicator_);
        lv_anim_set_values(&anim_in, bg_color.full, led_color.full);
        lv_anim_set_time(&anim_in, cycle_duration_ms / 2);
        lv_anim_set_delay(&anim_in, cycle_duration_ms / 2);
        lv_anim_set_exec_cb(&anim_in, [](void* var, int32_t v) {
            lv_obj_set_style_bg_color((lv_obj_t*)var, lv_color_t{.full = (uint16_t)v}, 0);
        });
        
        lv_anim_start(&anim_in);
    }
    
    void heartbeat_led(LEDColor color, uint32_t cycle_duration_ms = 1200) {
        lv_color_t led_color = get_led_color(color);
        
        uint32_t pulse_on_ms = cycle_duration_ms / 5;   // 20% on
        uint32_t pulse_off_ms = cycle_duration_ms - pulse_on_ms;
        
        // Quick on pulse
        lv_anim_t anim_on;
        lv_anim_init(&anim_on);
        lv_anim_set_var(&anim_on, led_indicator_);
        lv_anim_set_values(&anim_on, lv_color_black().full, led_color.full);
        lv_anim_set_time(&anim_on, 10);  // Instant on
        lv_anim_set_exec_cb(&anim_on, [](void* var, int32_t v) {
            lv_obj_set_style_bg_color((lv_obj_t*)var, lv_color_t{.full = (uint16_t)v}, 0);
        });
        lv_anim_start(&anim_on);
        
        // Schedule off
        lv_timer_create(
            [](lv_timer_t* timer) {
                auto* self = (LvglDisplay*)timer->user_data;
                self->clear_led();
                
                // Schedule next pulse
                lv_timer_create(
                    [](lv_timer_t* timer) {
                        auto* self = (LvglDisplay*)timer->user_data;
                        self->heartbeat_led(self->last_led_color_, 1200);
                    },
                    1000,  // pulse_off_ms
                    timer->user_data);
            },
            pulse_on_ms,
            this);
    }
    
    void set_led(LEDColor color) {
        last_led_color_ = color;
        lv_obj_set_style_bg_color(led_indicator_, get_led_color(color), 0);
    }
    
    void clear_led() {
        lv_obj_set_style_bg_color(led_indicator_, lv_color_black(), 0);
    }
    
private:
    lv_color_t get_led_color(LEDColor color) {
        switch (color) {
            case LED_RED:    return lv_color_red();
            case LED_GREEN:  return lv_color_green();
            case LED_ORANGE: return lv_color_make(255, 165, 0);  // Orange
            default:         return lv_color_black();
        }
    }
};
```

**Key Differences:**
- ✅ Uses LVGL's object system with circular radius styling
- ✅ Animations built with `lv_anim_t` (same as other features)
- ✅ Color blending handled by LVGL color interpolation
- ✅ Positioned using LVGL alignment system (LV_ALIGN_TOP_RIGHT)
- ⚠️ Heartbeat mode requires careful timer management for repetition

**Challenge:** Smooth color fade interpolation

LVGL's color interpolation works on integer values, not gradual RGB changes. To get smooth fades like TFT (pre-calculated gradient), we have two options:

**Option A: Pre-calculated Gradient Array (Matches TFT)**
```cpp
// Store gradient from RED→BLACK, GREEN→BLACK, ORANGE→BLACK
uint16_t led_red_fade_gradient[51];    // 50 fade steps
uint16_t led_green_fade_gradient[51];
uint16_t led_orange_fade_gradient[51];

void flash_led(LEDColor color) {
    uint16_t* gradient = (color == LED_RED) ? led_red_fade_gradient : 
                        (color == LED_GREEN) ? led_green_fade_gradient :
                        led_orange_fade_gradient;
    
    // Animate through gradient values
    for (int step = 0; step <= 50; step++) {
        lv_obj_set_style_bg_color(led_indicator_, 
            lv_color_hex(gradient[step]), 0);
        lv_timer_handler();  // Pump display
    }
}
```

**Option B: LVGL Animation with Intermediate Callbacks** (Cleaner)
```cpp
// Let LVGL handle interpolation; update at each frame
lv_anim_t anim;
lv_anim_init(&anim);
lv_anim_set_var(&anim, led_indicator_);
lv_anim_set_values(&anim, 0, 50);  // 0-50 gradient steps
lv_anim_set_time(&anim, 500);      // 500ms fade
lv_anim_set_exec_cb(&anim, [](void* var, int32_t step) {
    uint16_t color = get_led_gradient_color(step);
    lv_obj_set_style_bg_color((lv_obj_t*)var, lv_color_hex(color), 0);
});
```

**Chosen Approach:** **Option B** (LVGL Animation with gradient lookup)
- ✅ Reuses fade gradients from TFT implementation
- ✅ Cleaner code with LVGL animation framework
- ✅ Executes at each task_handler() call for smooth animation

---

### 2.6 FEATURE: Error States

**TFT Implementation:**
```cpp
void show_error_state() {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("ERROR", 130, 75);
}

void show_fatal_error(const char* component, const char* message) {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("FATAL ERROR", 90, 20);
    tft.drawString(component ? component : "", 20, 60);
    tft.drawString(message ? message : "", 20, 100);
}
```

**LVGL Implementation:**
```cpp
void LvglDisplay::show_error_state() override {
    LOG_WARN("LVGL", "Showing error state");
    
    lv_obj_t* error_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(error_scr, lv_color_red(), 0);
    
    lv_obj_t* label = lv_label_create(error_scr);
    lv_label_set_text(label, "ERROR");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    
    lv_scr_load(error_scr);  // Immediate, no animation
}

void LvglDisplay::show_fatal_error(const char* component, 
                                   const char* message) override {
    LOG_ERROR("LVGL", "Showing fatal error: [%s] %s", component, message);
    
    lv_obj_t* error_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(error_scr, lv_color_red(), 0);
    
    // Title
    lv_obj_t* title = lv_label_create(error_scr);
    lv_label_set_text(title, "FATAL ERROR");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    
    // Component
    lv_obj_t* comp_label = lv_label_create(error_scr);
    lv_label_set_text(comp_label, component ? component : "");
    lv_obj_align(comp_label, LV_ALIGN_TOP_LEFT, 20, 60);
    lv_obj_set_style_text_color(comp_label, lv_color_white(), 0);
    
    // Message
    lv_obj_t* msg_label = lv_label_create(error_scr);
    lv_label_set_text(msg_label, message ? message : "");
    lv_obj_align(msg_label, LV_ALIGN_TOP_LEFT, 20, 100);
    lv_obj_set_style_text_color(msg_label, lv_color_white(), 0);
    
    lv_scr_load(error_scr);  // Immediate, no animation
}
```

---

## SECTION 3: LVGL-SPECIFIC IMPLEMENTATION DETAILS

### 3.1 Display Driver (lvgl_driver.h/.cpp)

**Current Status:** Header exists, implementation needed

**Required Implementation:**

```cpp
// lvgl_driver.cpp - Implementation

namespace HAL {
namespace Display {

TFT_eSPI* LvglDriver::tft_ = nullptr;
lv_disp_drv_t LvglDriver::disp_drv_;
lv_disp_draw_buf_t LvglDriver::disp_buf_;
lv_disp_t* LvglDriver::disp_ = nullptr;
lv_color_t* LvglDriver::buf1_ = nullptr;
lv_color_t* LvglDriver::buf2_ = nullptr;
uint8_t LvglDriver::current_backlight_ = 0;

bool LvglDriver::init(TFT_eSPI& tft) {
    LOG_INFO("LVGL_DRIVER", "Initializing LVGL...");
    
    tft_ = &tft;
    
    // Initialize LVGL
    lv_init();
    
    // Initialize hardware
    init_hardware();
    
    // Allocate display buffers (PSRAM-backed)
    if (!allocate_buffers()) {
        LOG_ERROR("LVGL_DRIVER", "Failed to allocate display buffers");
        return false;
    }
    
    // Initialize display driver
    lv_disp_drv_init(&disp_drv_);
    disp_drv_.hor_res = 320;
    disp_drv_.ver_res = 170;
    disp_drv_.flush_cb = flush_cb;
    disp_drv_.draw_buf = &disp_buf_;
    disp_drv_.full_refresh = 0;  // Partial refresh enabled
    
    // Register display driver
    disp_ = lv_disp_drv_register(&disp_drv_);
    
    LOG_INFO("LVGL_DRIVER", "LVGL initialized successfully");
    return true;
}

bool LvglDriver::allocate_buffers() {
    // LVGL color is 16-bit RGB565
    const size_t buf_size = 320 * 170 * sizeof(lv_color_t);
    
    // Allocate from PSRAM with fallback to internal RAM
    buf1_ = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!buf1_) {
        buf1_ = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!buf1_) {
            LOG_ERROR("LVGL_DRIVER", "Failed to allocate display buffer");
            return false;
        }
        LOG_WARN("LVGL_DRIVER", "Using internal RAM for display buffer (PSRAM unavailable)");
    }
    
    // Double buffering for smoother animations
    buf2_ = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!buf2_) {
        buf2_ = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    
    // Initialize draw buffer descriptor
    lv_disp_draw_buf_init(&disp_buf_, buf1_, buf2_, 320 * 170);
    
    LOG_INFO("LVGL_DRIVER", "Display buffers allocated: buf1=%p, buf2=%p",
            buf1_, buf2_);
    
    return true;
}

void LvglDriver::flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, 
                          lv_color_t* color_p) {
    // Push modified area to TFT display
    // This is called by LVGL when pixels need to be updated
    
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    // Set display window
    tft_->setAddrWindow(area->x1, area->y1, w, h);
    
    // Push pixel data
    tft_->pushColors((uint16_t*)color_p, w * h, false);
    
    // Tell LVGL we're done
    lv_disp_flush_ready(disp);
}

void LvglDriver::init_hardware() {
    // Same hardware setup as TFT implementation
    // Set up GPIO, PWM, display power, backlight
    
    // Backlight PWM initialization
    pinMode(HardwareConfig::GPIO_BACKLIGHT, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_BACKLIGHT, LOW);
    
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcSetup(HardwareConfig::BACKLIGHT_PWM_CHANNEL,
             HardwareConfig::BACKLIGHT_FREQUENCY_HZ,
             HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
    ledcAttachPin(HardwareConfig::GPIO_BACKLIGHT,
                 HardwareConfig::BACKLIGHT_PWM_CHANNEL);
    #else
    ledcAttach(HardwareConfig::GPIO_BACKLIGHT,
              HardwareConfig::BACKLIGHT_FREQUENCY_HZ,
              HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
    #endif
    
    LOG_DEBUG("LVGL_DRIVER", "Hardware initialized");
}

void LvglDriver::task_handler() {
    // Must be called periodically from main loop
    // Processes animations, redraws, input events
    
    lv_timer_handler();
}

void LvglDriver::animate_backlight_to(uint8_t target, uint32_t duration_ms) {
    // Create LVGL animation for backlight
    
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &current_backlight_);
    lv_anim_set_values(&anim, current_backlight_, target);
    lv_anim_set_time(&anim, duration_ms);
    lv_anim_set_exec_cb(&anim, [](void* var, int32_t v) {
        uint8_t brightness = (uint8_t)v;
        set_backlight(brightness);
    });
    
    lv_anim_start(&anim);
}

void LvglDriver::set_backlight(uint8_t brightness) {
    current_backlight_ = brightness;
    
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, brightness);
    #else
    ledcWrite(HardwareConfig::GPIO_BACKLIGHT, brightness);
    #endif
}

// Custom memory allocators for PSRAM
void* lv_custom_mem_alloc(size_t size) {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!ptr) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

void lv_custom_mem_free(void* ptr) {
    heap_caps_free(ptr);
}

void* lv_custom_mem_realloc(void* ptr, size_t new_size) {
    void* new_ptr = lv_custom_mem_alloc(new_size);
    if (new_ptr && ptr) {
        // Note: original size unknown; caller must handle size tracking
        memcpy(new_ptr, ptr, new_size);
        heap_caps_free(ptr);
    }
    return new_ptr;
}

} // namespace Display
} // namespace HAL
```

---

### 3.2 LVGL Display Class (lvgl_display.h/.cpp)

**Current Status:** Header exists, implementation needed

**Required Implementation:**

```cpp
// lvgl_display.cpp - Complete implementation

namespace Display {

class LvglDisplay : public IDisplay {
private:
    // Screen objects
    lv_obj_t* splash_screen_ = nullptr;
    lv_obj_t* ready_screen_ = nullptr;
    lv_obj_t* main_screen_ = nullptr;
    
    // Splash screen resources
    lv_img_dsc_t splash_img_dsc_;
    uint16_t* splash_img_buf_ = nullptr;
    
    // SOC display
    lv_obj_t* soc_label_ = nullptr;
    uint16_t soc_color_gradient_[501];
    float last_soc_ = -1.0f;
    
    // Power bar widget
    PowerBarWidget power_bar_;
    
    // Color gradients
    void init_color_gradients() {
        pre_calculate_color_gradient(TFT_RED, Display::AMBER, 167,
                                   &soc_color_gradient_[0]);
        pre_calculate_color_gradient(Display::AMBER, Display::LIME, 167,
                                   &soc_color_gradient_[167]);
        pre_calculate_color_gradient(Display::LIME, TFT_GREEN, 166,
                                   &soc_color_gradient_[334]);
    }
    
public:
    bool init() override {
        LOG_INFO("LVGL", "Initializing LVGL display...");
        
        // Initialize LVGL driver
        if (!HAL::Display::LvglDriver::init(tft)) {
            LOG_ERROR("LVGL", "Failed to initialize LVGL driver");
            return false;
        }
        
        // Initialize color gradients
        init_color_gradients();
        
        LOG_INFO("LVGL", "LVGL display initialized");
        return true;
    }
    
    void display_splash_with_fade() override {
        // See Section 2.1 for detailed implementation
        // ... (detailed code shown earlier)
    }
    
    void display_initial_screen() override {
        // See Section 2.2 for detailed implementation
        // ... (detailed code shown earlier)
    }
    
    void update_soc(float soc_percent) override {
        // See Section 2.3 for detailed implementation
        // ... (detailed code shown earlier)
    }
    
    void update_power(int32_t power_w) override {
        if (!main_screen_) {
            // Create main screen on first call
            main_screen_ = lv_obj_create(nullptr);
            lv_obj_set_style_bg_color(main_screen_, lv_color_black(), 0);
            
            // Create SOC label in top region
            lv_obj_t* top_region = lv_obj_create(main_screen_);
            lv_obj_set_size(top_region, 320, 113);  // 2/3 of 170
            lv_obj_align(top_region, LV_ALIGN_TOP_MID, 0, 0);
            
            soc_label_ = lv_label_create(top_region);
            lv_obj_align(soc_label_, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_text_font(soc_label_, &lv_font_montserrat_28, 0);
            
            // Initialize power bar in bottom region
            lv_obj_t* bottom_region = lv_obj_create(main_screen_);
            lv_obj_set_size(bottom_region, 320, 57);  // 1/3 of 170
            lv_obj_align(bottom_region, LV_ALIGN_BOTTOM_MID, 0, 0);
            power_bar_.init(bottom_region);
            
            lv_scr_load(main_screen_);
        }
        
        power_bar_.update(power_w, LayoutSpec::PowerBar::MAX_POWER_W);
    }
    
    void show_status_page() override {
        LOG_INFO("LVGL", "Showing status page...");
        
        lv_obj_t* status_scr = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(status_scr, lv_color_black(), 0);
        
        lv_obj_t* label = lv_label_create(status_scr);
        lv_label_set_text(label, "Status Page");
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_text_color(label, lv_color_green(), 0);
        
        lv_scr_load(status_scr);
    }
    
    void show_error_state() override {
        // See Section 2.5 for detailed implementation
    }
    
    void show_fatal_error(const char* component, const char* message) override {
        // See Section 2.5 for detailed implementation
    }
    
    void task_handler() override {
        // Must be called periodically from main loop
        HAL::Display::LvglDriver::task_handler();
    }
};

} // namespace Display
```

---

### 3.3 Memory and Performance Considerations

**LVGL Memory Requirements:**

| Component | Size | Notes |
|-----------|------|-------|
| LVGL core (lv_init) | ~50KB | Internal LVGL structures |
| Display buffers (2x) | ~220KB | RGB565, 320x170 each |
| PSRAM pool (lv_mem) | 128KB | LVGL dynamic allocations |
| Widget tree | ~20KB | Screen objects, labels |
| **Total** | **~420KB** | Manageable on ESP32-S3 |

**Performance:**

| Metric | TFT | LVGL | Target |
|--------|-----|------|--------|
| Frame time | Instant | ~16-20ms | ✅ Similar |
| Animation quality | Manual loops | Built-in | ✅ Better |
| Memory efficiency | <20% | <25% | ✅ Good |
| Backlight control | Synchronous | Asynchronous | ⚠️ Different |

---

## SECTION 4: IMPLEMENTATION ROADMAP

### Phase 1: Driver & Infrastructure (2-3 days)
**Objective:** Get LVGL rendering to display

- [ ] Implement lvgl_driver.cpp (flush callback, buffer allocation)
- [ ] Implement custom memory allocators for PSRAM
- [ ] Test basic LVGL initialization and screen rendering
- [ ] Verify display buffers allocate from PSRAM
- [ ] Build configuration for `receiver_lvgl` environment
- [ ] **Deliverable:** LVGL renders basic objects to display

**Success Criteria:**
- Simple label displays correctly
- LVGL main loop pumps without crashes
- Memory usage tracked and logged

---

### Phase 2: Splash Screen & Basic Animations (2-3 days)
**Objective:** Replicate splash screen with fade animations

- [ ] Implement JPEG decoding (reuse from TFT implementation)
- [ ] Create splash screen object with image widget
- [ ] Implement screen load animations (fade-in, fade-out)
- [ ] Implement backlight animations
- [ ] Test animation timing (3s fade-in, 2s hold, 3s fade-out)
- [ ] Implement LED indicator widget (circular object)
- [ ] Implement LED flash and heartbeat animations
- [ ] **Deliverable:** Splash screen displays with matching fade behavior; LED indicator works

**Success Criteria:**
- Splash image loads and displays
- Fade-in/out animations complete in specified time
- Backlight syncs with screen animations
- LED circle displays at top-right corner
- LED flash and heartbeat modes work correctly
- No resource leaks or crashes

---

### Phase 3: Main Display Widgets (2-3 days)
**Objective:** SOC, Power bar, power text displays

- [ ] Implement SOC label with color gradient
- [ ] Implement power bar widget (bidirectional progress bars)
- [ ] Implement power text display
- [ ] Connect update_soc() and update_power() to widgets
- [ ] Test with rapid updates (SOC 0→100%, power swings)
- [ ] **Deliverable:** Full display with all data indicators

**Success Criteria:**
- SOC displays with correct gradient colors
- Power bar animates in both directions
- Power text updates correctly
- No lag or visual artifacts

---

### Phase 4: Error States & Status Pages (1-2 days)
**Objective:** Error handling and additional screens

- [ ] Implement show_error_state()
- [ ] Implement show_fatal_error()
- [ ] Implement show_status_page()
- [ ] Test transitions between screens
- [ ] **Deliverable:** Complete error handling and status display

**Success Criteria:**
- Error screens display immediately
- Text formatting matches TFT implementation
- Screen transitions are clean

---

### Phase 5: Testing & Validation (2-3 days)
**Objective:** Verify functional equivalence with TFT

- [ ] Side-by-side comparison: TFT vs LVGL output
- [ ] Performance profiling (frame time, memory)
- [ ] Edge case testing (rapid updates, memory pressure)
- [ ] Build both binaries (USE_TFT and USE_LVGL)
- [ ] Validate timings match (3s fades, animation frame rates)
- [ ] **Deliverable:** Both backends produce identical output

**Success Criteria:**
- Visual output matches TFT implementation
- Performance within acceptable range
- No memory leaks or crashes
- Both binaries compile and run successfully

---

### Phase 6: Documentation & Optimization (1 day)
**Objective:** Code review and optimization

- [ ] Document LVGL-specific patterns
- [ ] Code review for consistency with TFT backend
- [ ] Optimize if performance issues found
- [ ] Create LVGL_IMPLEMENTATION_COMPLETE.md
- [ ] **Deliverable:** Production-ready LVGL backend

---

## SECTION 5: KEY TECHNICAL CHALLENGES & SOLUTIONS

### Challenge 1: Asynchronous vs Synchronous Execution

**Problem:** TFT rendering is blocking (function waits for animation). LVGL is event-driven (function returns immediately).

**Impact:** Code calling `display_splash_with_fade()` must not assume splash is finished when function returns.

**Solution:**
```cpp
// Main loop - MUST pump task handler regularly
void main_loop() {
    display->task_handler();  // Call every iteration
    
    // Other work happens here while animations run
}

// Display manager handles wait logic if needed
// (e.g., wait_for_splash_complete() that pumps until animations finish)
```

---

### Challenge 2: Screen Lifecycle & Memory

**Problem:** Creating/destroying screens on each update could fragment memory.

**Solution:**
```cpp
// Create screens once, reuse them
static lv_obj_t* main_screen = nullptr;

void on_data_update() {
    if (!main_screen) {
        main_screen = lv_obj_create(nullptr);
        // ... configure once ...
    }
    
    // Update widgets, don't recreate screen
}
```

---

### Challenge 3: Image Display Format

**Problem:** JPEG file format, LVGL image descriptor format.

**Solution:**
```cpp
// Reuse JPEG decoding from TFT implementation
// Output is RGB565 pixel buffer
// Create LVGL image descriptor pointing to buffer
lv_img_dsc_t dsc;
dsc.header.cf = LV_IMG_CF_TRUE_COLOR;  // RGB565
dsc.header.w = width;
dsc.header.h = height;
dsc.data = (uint8_t*)pixel_buffer;
lv_img_set_src(img_obj, &dsc);
```

---

### Challenge 4: Font Selection & Text Rendering

**Problem:** TFT uses FreeSansBold fonts, LVGL uses Montserrat fonts.

**Solution:**
```cpp
// Map TFT fonts to LVGL fonts based on size:
// FreeSansBold 18pt → lv_font_montserrat_20 or _24
// FreeSansBold 12pt → lv_font_montserrat_14 or _16
// FreeSansBold 9pt → lv_font_montserrat_12

// Or: Generate custom fonts with LVGL font tool if exact match needed
```

---

### Challenge 5: Color Format Consistency

**Problem:** LVGL uses lv_color_t (abstracted), TFT uses uint16_t RGB565.

**Solution:**
```cpp
// lv_conf.h configures:
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1  // Matches TFT_eSPI::setSwapBytes(true)

// Pre-calculated gradients use RGB565 uint16_t:
uint16_t soc_gradient[501];
lv_color_t lvgl_color = lv_color_hex(soc_gradient[idx]);
```

---

### Challenge 6: Animation Timing Precision

**Problem:** TFT uses fixed 16ms frame time; LVGL uses fixed animation duration.

**Solution:**
```cpp
// LVGL animations automatically distribute frames over duration
lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 3000, 0, false);
// LVGL calculates frame spacing internally

// Backlight animation:
lv_anim_set_time(&anim, 3000);  // Total 3000ms
// LVGL interpolates values at each task_handler() call
```

---

## SECTION 6: BUILD SYSTEM INTEGRATION

### platformio.ini Configuration

```ini
[env:receiver_tft]
description = TFT Display Backend (Synchronous Rendering)
board = lilygo-t-display-s3
build_flags =
    -DUSE_TFT
    -DESP_IDF_VERSION_VAL=50000

[env:receiver_lvgl]
description = LVGL Display Backend (Event-Driven Rendering)
board = lilygo-t-display-s3
lib_deps =
    lvgl/lvgl @ ^8.4.0
build_flags =
    -DUSE_LVGL
    -DESP_IDF_VERSION_VAL=50000
    ; LVGL optimizations
    -DLV_CONF_PATH="${PROJECT_SRC_DIR}/src/lv_conf.h"
    -DLV_MEM_CUSTOM=1

[env:receiver_hybrid]
description = Hybrid Mode (TFT + LVGL Selectable at Runtime) [Future]
```

### Source Organization

```
src/
  display/
    ├── display_interface.h          # Common interface
    ├── display_layout_spec.h        # Shared layout/timing
    ├── display.cpp/.h               # Manager, dispatches to backend
    ├── tft_impl/
    │   ├── tft_display.h/.cpp       # TFT implementation
    │   └── ...
    └── lvgl_impl/
        ├── lvgl_display.h/.cpp      # LVGL implementation
        ├── power_bar_widget.h/.cpp  # LVGL-specific widgets
        └── ...
  hal/
    display/
      ├── hardware_config.h          # Hardware definitions
      ├── tft_espi_display_driver.h/.cpp  # TFT HAL
      └── lvgl_driver.h/.cpp         # LVGL HAL
  lv_conf.h                          # LVGL configuration
```

---

## SECTION 7: FEATURE PARITY COMPARISON

| Feature | TFT | LVGL | Status |
|---------|-----|------|--------|
| **Splash Screen** | 3s fade-in | 3s fade-in | ✅ |
| **Splash Hold** | 2s display | 2s display | ✅ |
| **Splash Fade-out** | 3s fade-out | 3s fade-out | ✅ |
| **Ready Screen** | Text display | Text display | ✅ |
| **SOC Display** | Gradient color | Gradient color | ✅ |
| **Power Bar** | Bidirectional bars | Bidirectional bars | ✅ |
| **Power Text** | Numeric value | Numeric value | ✅ |
| **LED Indicator** | Circle with fade animation | Circle with fade animation | ✅ |
| **LED Flash** | Fade in/out loop | LVGL animation | ✅ |
| **LED Heartbeat** | Pulse mode (on 20%) | Pulse mode (on 20%) | ✅ |
| **Error State** | Red screen + text | Red screen + text | ✅ |
| **Fatal Error** | Red screen + details | Red screen + details | ✅ |
| **Animations** | Manual loops | LVGL animations | ✅ |
| **Backlight** | PWM sync'd with fade | PWM animated | ✅ |

---

## SECTION 8: TESTING STRATEGY

### Unit Tests
```cpp
// Test color gradient calculation
void test_soc_color_gradient() {
    // Verify gradient matches TFT implementation
}

// Test power bar calculations
void test_power_bar_count() {
    // Verify bar count matches TFT
}

// Test animation timing
void test_animation_duration() {
    // Verify animations complete in correct time
}
```

### Integration Tests
```cpp
// Test full splash sequence
void test_splash_sequence() {
    // Verify: decode → fade-in → hold → fade-out → cleanup
}

// Test data updates
void test_soc_update_rapid() {
    // Rapid SOC updates, verify no artifacts
}

void test_power_swap() {
    // Switch between charging/discharging, verify smooth transition
}
```

### Comparison Tests
```cpp
// Side-by-side TFT vs LVGL
void test_visual_parity() {
    // Screenshot TFT output
    // Screenshot LVGL output
    // Verify pixel-perfect match (allowing for font differences)
}
```

---

## SECTION 9: ESTIMATED TIMELINE

| Phase | Duration | Start | End | Status |
|-------|----------|-------|-----|--------|
| Phase 1: Driver & Infrastructure | 2-3 days | Day 1 | Day 3 | Planning |
| Phase 2: Splash Screen + LED | 2-3 days | Day 4 | Day 6 | Planning |
| Phase 3: Main Widgets | 2-3 days | Day 7 | Day 9 | Planning |
| Phase 4: Error States | 1-2 days | Day 10 | Day 11 | Planning |
| Phase 5: Testing & Validation | 2-3 days | Day 12 | Day 14 | Planning |
| Phase 6: Documentation | 1 day | Day 15 | Day 15 | Planning |
| **Total** | **10-15 days** | | | |

**With efficient parallel work:** 7-10 days

---

## SECTION 10: RESOURCE REQUIREMENTS

### Memory Budget

```
Internal RAM (327KB total):
  ├── LVGL core structures: ~50KB
  ├── Widget tree: ~20KB
  ├── Stack: ~30KB
  └── Free/Buffer: ~227KB

PSRAM (16MB available):
  ├── Display buffers (2x RGB565): ~220KB
  ├── LVGL memory pool: ~128KB
  ├── Image buffers (JPEG→RGB565): ~100KB
  └── Free for other: ~15.5MB
```

**Memory Efficient:** Uses PSRAM effectively, leaves headroom for future features.

---

## SECTION 11: RISKS & MITIGATION

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|-----------|
| Animation timing precision | Medium | Medium | Careful testing of LVGL frame timing; synchronization with hardware timer if needed |
| Memory fragmentation | Low | High | Pre-allocate screens, reuse objects, monitor heap usage |
| Font rendering differences | Medium | Low | Accept font differences or generate custom fonts |
| Screen transition glitches | Medium | Medium | Thorough testing of screen cleanup; proper object deletion |
| PSRAM allocation failures | Low | High | Always provide internal RAM fallback; test under memory pressure |

---

## SECTION 12: SUCCESS CRITERIA

### Functional Equivalence
- [ ] Splash screen displays with 3s fade-in, 2s hold, 3s fade-out
- [ ] SOC display shows correct color gradient (RED→AMBER→LIME→GREEN)
- [ ] Power bar shows bidirectional indicators (charge/discharge)
- [ ] Power text updates correctly
- [ ] Error states display properly
- [ ] All animations complete in specified time

### Performance
- [ ] Frame time ≤ 16ms (60 FPS)
- [ ] Memory usage < 25% RAM, < 30% Flash
- [ ] No resource leaks
- [ ] Smooth animations without stuttering or tearing

### Code Quality
- [ ] Passes code review (readability, maintainability)
- [ ] Consistent with TFT backend style
- [ ] Properly documented
- [ ] Clean compilation (zero warnings)

### Build Integration
- [ ] Both binaries compile successfully
- [ ] `USE_TFT` and `USE_LVGL` backends fully separated
- [ ] Platform environment correctly selects backend
- [ ] Binary sizes reasonable

---

## CONCLUSION

The LVGL implementation is **technically feasible** and follows a clear roadmap. Key differences from TFT (asynchronous operation, built-in animations) actually provide advantages in terms of code cleanliness and animation quality.

**Next Steps:**
1. Begin Phase 1: Driver & Infrastructure
2. Build incrementally, testing after each phase
3. Maintain parity with TFT through side-by-side testing
4. Document LVGL-specific patterns for team knowledge

The shared layout specification (display_layout_spec.h) ensures both backends can evolve independently while maintaining a common semantic contract.

---

**Document Version:** 1.0  
**Created:** March 3, 2026  
**Next Review:** After Phase 2 (Splash Screen) completion

