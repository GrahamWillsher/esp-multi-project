# LVGL Splash Screen Architecture Analysis

**Date:** March 2, 2026  
**Status:** Critical Issues Identified  
**Priority:** HIGH

---

## Executive Summary

The splash screen is **not working correctly** due to **critical gaps between the implementation and LVGL's animation architecture**. While LVGL is enabled in `lv_conf.h` and animations are configured, **the fade transition is not actually being rendered** because:

1. **LVGL animations require continuous `lv_timer_handler()` calls** to progress through frames
2. **The splash screen is displayed, held, and then transitioned away** without allowing LVGL's internal animation state machine to process
3. **Blocking delays prevent the animation engine from running** during the transition period
4. **The fade-out animation is initiated but never completes** before the "Ready" screen becomes visible

---

## Technical Root Causes

### Issue 1: Splash Screen Display Flow Blocks Animation Engine

**Location:** [src/display/display_splash_lvgl.cpp](src/display/display_splash_lvgl.cpp#L174-L191)

**Current Code:**
```cpp
void display_splash_lvgl() {
    // ... setup code ...
    
    // Load screen while backlight is off
    lv_scr_load(splash_scr);

    // Run a couple of LVGL cycles so first frame is fully pushed before backlight on
    for (int i = 0; i < 2; i++) {
        lv_timer_handler();
        smart_delay(16);
    }

    HAL::Display::LvglDriver::set_backlight(255);
    smart_delay(50);

    // Hold splash for 2 seconds
    const uint32_t hold_end = millis() + 2000;
    while (millis() < hold_end) {
        lv_timer_handler();
        smart_delay(50);
    }
}
```

**Problems:**
1. Uses `lv_scr_load()` instead of `lv_scr_load_anim()` — **no animation is queued**
2. `smart_delay(50)` is too long — LVGL needs ~20ms ticks for smooth animation (50 FPS per `LV_ANIM_RESOLUTION=20`)
3. The 2-second hold calls `smart_delay(50)`, which means LVGL only gets serviced every 50ms instead of every 20ms
4. **No fade-in animation** — splash just appears instantly with backlight ON

---

### Issue 2: Transition from Splash to "Ready" Screen Doesn't Wait for Animation

**Location:** [src/display/display_splash_lvgl.cpp](src/display/display_splash_lvgl.cpp#L210-L238)

**Current Code:**
```cpp
void display_initial_screen_lvgl() {
    // ... create Ready screen ...
    
    // Load Ready screen with FADE_OUT animation (800ms fade)
    lv_scr_load_anim(ready_scr, LV_SCR_LOAD_ANIM_FADE_OUT, 800, 0, true);

    // Pump LVGL until animation completes
    while (lv_anim_count_running() > 0) {
        lv_timer_handler();
        smart_delay(10);
    }
}
```

**Problems:**
1. FADE_OUT fades the **old screen OUT** (to black), then shows the new screen — but this requires:
   - Old splash screen to still exist and be visible
   - Continuous rendering of the fade animation
   - Proper opacity updates

2. **The animation is initiated but timing is wrong**:
   - `smart_delay(10)` is marginally acceptable but adds 10ms latency per frame
   - If animation takes 800ms and we get ~50 FPS (20ms per frame), we need ~40 frames
   - Actual execution may get fewer frames due to the 10ms delay + lv_timer_handler overhead

3. **No fade-in for splash**:
   - Splash appears instantly with no opacity animation
   - Transition is jarring instead of smooth

---

### Issue 3: Backlight Timing vs Animation Timing Mismatch

**Location:** [src/display/display_splash_lvgl.cpp](src/display/display_splash_lvgl.cpp#L127, #L184)

**Current Code:**
```cpp
// Backlight OFF, screen loads, then 2 render cycles, then backlight ON immediately
HAL::Display::LvglDriver::set_backlight(0);
lv_scr_load(splash_scr);
for (int i = 0; i < 2; i++) {
    lv_timer_handler();
    smart_delay(16);
}
HAL::Display::LvglDriver::set_backlight(255);  // Instantly goes to full brightness
```

**Problems:**
1. **Backlight jumps from 0 to 255 with no animation**
   - Should fade from 0 to 255 over 300-500ms for smooth fade-in effect
   - LVGL has an animation system but backlight PWM is a raw `ledcWrite()` call

2. **Image visibility depends on backlight**:
   - Fade-out animation tries to fade screen opacity, but if backlight is at 255, it won't look like fade
   - Need coordinated backlight PWM animation + screen opacity animation

---

### Issue 4: LVGL Task Handler in Main Loop Insufficient

**Location:** [src/main.cpp](src/main.cpp#L240-L248)

**Current Code:**
```cpp
void loop() {
    RxHeartbeatManager::instance().tick();
    SystemStateManager::instance().update();
    
    #ifdef USE_LVGL
    lvgl_task_handler();
    #endif
    
    smart_delay(10);  // 10ms delay
}
```

**Problems:**
1. Main loop calls `lvgl_task_handler()` once every 10ms
2. During splash display (first ~3 seconds), main loop is **blocked** in `display_splash_lvgl()` and `display_initial_screen_lvgl()`
3. No other code can run — heart beats don't tick, state manager doesn't update
4. After startup, loop runs every 10ms, which may miss frames if there are other delays

---

### Issue 5: LVGL Animation Configuration Requirements Not Fully Understood

**Location:** [src/lv_conf.h](src/lv_conf.h#L195-L200)

**Current Config:**
```c
#define LV_USE_ANIMATION        1
#if LV_USE_ANIMATION
#define LV_ANIM_RESOLUTION      20      /* 50 FPS */
#endif
```

**What's Missing:**
1. Animation refresh is tied to `lv_timer_handler()` calls
2. Every LVGL timer call needs to happen at ~20ms intervals (or faster) to maintain 50 FPS
3. **Current code doesn't guarantee this timing**:
   - `smart_delay(50)` in splash hold breaks the rhythm
   - `smart_delay(10)` in transition loop adds variability

---

## LVGL Animation System Requirements

### How LVGL Animations Actually Work

```
1. lv_scr_load_anim(new_screen, LV_SCR_LOAD_ANIM_FADE_OUT, 800, delay, auto_del)
   - Queues an animation in LVGL's animation system
   - Sets up opacity keyframes for the old and new screens
   - Schedules the animation to run for 800ms

2. lv_timer_handler()
   - Processes all queued animations
   - Updates animation progress based on elapsed time
   - Updates object properties (opacity, position, etc.)
   - Marks dirty regions for re-render
   - Calls flush callback to push updated pixels to display

3. lv_anim_count_running()
   - Returns how many animations are still in progress
   - Animation is removed from queue when:
     - Time expires (animation_time has passed)
     - All keyframes are processed
     - Animation is manually deleted

4. For a smooth fade:
   - Need 40+ frames for 800ms at 50 FPS
   - Each frame must update within ±5ms of ideal 20ms interval
   - If delays are irregular, frame drops or stuttering occurs
```

### What LVGL Expects From the Application

1. **Call `lv_timer_handler()` regularly** — ideally every 20ms or faster (±5ms tolerance)
2. **Keep `smart_delay()` calls minimal** during animations — max 20ms
3. **Don't block the main loop** — let LVGL get CPU time continuously
4. **Use `lv_anim_count_running()`** to wait for animations to finish, not sleep() directly
5. **Coordinate backlight PWM** with screen opacity if fade effect is desired

---

## Current Architecture Summary

### Splash Display Sequence (Current Broken Implementation)

```
1. display_splash_lvgl() called from setup
   ├─ Backlight = 0 (OFF)
   ├─ Create splash screen with lv_scr_load() [NO ANIMATION]
   ├─ Load screen (instant load, no fade-in)
   ├─ Render 2 frames (2 × lv_timer_handler() calls)
   ├─ Backlight = 255 (INSTANT ON)
   ├─ Hold for 2000ms with lv_timer_handler() every 50ms
   └─ Return to setup()

2. display_initial_screen_lvgl() called from setup
   ├─ Create "Ready" screen
   ├─ Load with FADE_OUT animation (800ms)
   ├─ Wait for animations: lv_anim_count_running() > 0
   │  └─ Pump LVGL with lv_timer_handler() every 10ms
   ├─ Free splash image buffer
   └─ Return to setup()

3. Main loop starts (after ~3 seconds of setup)
   └─ Call lvgl_task_handler() every 10ms
```

### File Structure

```
src/
├── main.cpp
│   ├── Calls displaySplashWithFade()
│   ├── Calls displayInitialScreen()
│   └── Calls lvgl_task_handler() in loop()
│
├── display/
│   ├── display_core_lvgl.cpp
│   │   └── Wrapper functions (displaySplashWithFade, displayInitialScreen)
│   │   └── lvgl_task_handler() → HAL::Display::LvglDriver::task_handler()
│   │
│   ├── display_splash_lvgl.cpp
│   │   ├── display_splash_lvgl() [BROKEN: instant load, no fade-in]
│   │   └── display_initial_screen_lvgl() [BROKEN: timing issues]
│   │
│   └── pages/
│       └── status_page_lvgl.cpp
│           └── show() → lv_scr_load_anim(screen_, LV_SCR_LOAD_ANIM_NONE, 0, 0, true)
│
└── hal/
    └── display/
        └── lvgl_driver.cpp
            ├── init() → tft_->init(), buffer allocation, driver registration
            ├── task_handler() → lv_timer_handler()
            └── flush_cb() → tft_->pushColors()
```

---

## Observed Symptoms

1. **Splash image appears instantly** (no fade-in) — because no animation is queued in `display_splash_lvgl()`
2. **Transition to "Ready" screen is instant or very fast** (not 800ms fade) — because:
   - Timing delays prevent smooth frame rate
   - Animation may complete, but fade effect not perceptible due to timing jitter
3. **No smooth backlight fade** — backlight PWM jumps from 0 to 255 instantly
4. **Setup takes longer than expected** — blocking 2+ seconds in display functions

---

## Solution Overview

### Required Fixes (3 Critical Changes)

#### Fix 1: Add Fade-In Animation to Splash Screen

**Change:** Use `lv_scr_load_anim()` with `FADE_IN` instead of `lv_scr_load()`

**What it does:**
- Queues a fade-in animation (0ms → 100% opacity over 300ms)
- Splash screen appears smoothly from black background
- Backlight PWM can be animated in parallel

**Pseudocode:**
```cpp
// Queue fade-in animation (300ms) with 0ms delay
lv_scr_load_anim(splash_scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);

// Animate backlight from 0 to 255 over 300ms
lv_anim_t a;
lv_anim_init(&a);
lv_anim_set_values(&a, 0, 255);
lv_anim_set_time(&a, 300);
lv_anim_set_exec_cb(&a, backlight_anim_callback);
lv_anim_start(&a);

// Wait for animation to complete
while (lv_anim_count_running() > 0) {
    lv_timer_handler();
    smart_delay(10);  // Keep LVGL pumped
}
```

**Impact:** Splash screen fades in smoothly over 300ms while backlight also fades in

---

#### Fix 2: Improve Splash Hold Duration with Proper Animation Timing

**Change:** Maintain consistent 20ms interval for `lv_timer_handler()` calls

**What it does:**
- Ensures LVGL animation engine gets consistent input
- Maintains 50 FPS animation refresh rate
- Prevents frame drops and stuttering

**Pseudocode:**
```cpp
// Hold splash for 2000ms with proper timing
const uint32_t hold_end = millis() + 2000;
while (millis() < hold_end) {
    lv_timer_handler();
    // Calculate remaining time and delay appropriately
    uint32_t elapsed = millis() - hold_start;
    if (elapsed < hold_duration) {
        uint32_t remaining = hold_duration - elapsed;
        // Sleep at most 20ms to maintain frame rate
        smart_delay(remaining > 20 ? 20 : remaining);
    }
}
```

**Impact:** Splash screen stays visible for exactly 2 seconds with smooth animation if needed

---

#### Fix 3: Improve Transition Timing and Add Blocking on Animation Completion

**Change:** Wait for all animations to complete before returning from `display_initial_screen_lvgl()`

**What it does:**
- Ensures fade-out animation completes (800ms) before next screen is rendered
- Proper synchronization between splash deletion and Ready screen appearance

**Pseudocode:**
```cpp
void display_initial_screen_lvgl() {
    // Create Ready screen...
    
    // Load Ready screen with FADE_OUT animation (800ms)
    // auto_del=true: LVGL will safely delete splash after transition
    lv_scr_load_anim(ready_scr, LV_SCR_LOAD_ANIM_FADE_OUT, 800, 0, true);

    // Ensure animation completes
    const uint32_t anim_start = millis();
    while (lv_anim_count_running() > 0) {
        lv_timer_handler();
        smart_delay(10);  // Fine for transitions
        
        // Safety timeout (1 second should be more than enough for 800ms anim)
        if (millis() - anim_start > 1000) {
            LOG_WARN("SPLASH", "Animation timeout, continuing...");
            break;
        }
    }
    
    // Free splash image buffer (splash is now deleted by LVGL)
    if (s_img_buf) {
        heap_caps_free(s_img_buf);
        s_img_buf = nullptr;
    }
    
    LOG_INFO("SPLASH", "Ready screen active");
}
```

**Impact:** Smooth 800ms fade-out from splash to Ready screen, properly synchronized

---

## Detailed Solution Implementation

### Step 1: Implement Backlight Animation Helper

**File:** [src/hal/display/lvgl_driver.cpp](src/hal/display/lvgl_driver.cpp)

Add a backlight animation function:
```cpp
static void backlight_anim_cb(void* var, int32_t v) {
    HAL::Display::LvglDriver::set_backlight((uint8_t)v);
}

void LvglDriver::animate_backlight_to(uint8_t target, uint32_t duration_ms) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_values(&a, current_backlight_, target);
    lv_anim_set_time(&a, duration_ms);
    lv_anim_set_exec_cb(&a, backlight_anim_cb);
    lv_anim_start(&a);
}
```

---

### Step 2: Rewrite `display_splash_lvgl()` with Fade-In Animation

**File:** [src/display/display_splash_lvgl.cpp](src/display/display_splash_lvgl.cpp#L116-L192)

Replace entire function body with:
```cpp
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
    // auto_del=false: we'll keep splash around for transition
    lv_scr_load_anim(splash_scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);

    // Animate backlight from 0 to 255 over 300ms (synchronized with fade)
    HAL::Display::LvglDriver::animate_backlight_to(255, 300);

    // Wait for fade-in animation to complete
    const uint32_t fade_start = millis();
    while (lv_anim_count_running() > 0) {
        lv_timer_handler();
        smart_delay(10);
        
        // Safety timeout
        if (millis() - fade_start > 500) {
            LOG_WARN("SPLASH", "Fade-in timeout, continuing...");
            break;
        }
    }

    LOG_INFO("SPLASH", "Fade-in complete, holding splash for 2000ms");

    // Hold splash for 2 seconds with proper LVGL pumping
    const uint32_t hold_start = millis();
    const uint32_t hold_duration = 2000;
    while (millis() - hold_start < hold_duration) {
        lv_timer_handler();
        uint32_t elapsed = millis() - hold_start;
        uint32_t remaining = hold_duration - elapsed;
        smart_delay(remaining > 20 ? 20 : remaining);
    }

    LOG_INFO("SPLASH", "=== Splash END ===");
}
```

---

### Step 3: Improve `display_initial_screen_lvgl()` Transition Timing

**File:** [src/display/display_splash_lvgl.cpp](src/display/display_splash_lvgl.cpp#L194-L240)

Replace function body with:
```cpp
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
    LOG_DEBUG("SPLASH", "Starting FADE_OUT animation...");
    lv_scr_load_anim(ready_scr, LV_SCR_LOAD_ANIM_FADE_OUT, 800, 0, true);

    // Pump LVGL until animation completes
    const uint32_t anim_start = millis();
    uint32_t frame_count = 0;
    while (lv_anim_count_running() > 0) {
        lv_timer_handler();
        frame_count++;
        smart_delay(10);
        
        // Safety timeout (1.5 seconds should be more than enough for 800ms anim)
        if (millis() - anim_start > 1500) {
            LOG_WARN("SPLASH", "Animation timeout after %u frames, continuing...", frame_count);
            break;
        }
    }
    
    LOG_INFO("SPLASH", "Fade-out animation complete (%u frames, %u ms)", 
             frame_count, (unsigned)(millis() - anim_start));

    // Free splash image buffer now that splash screen is deleted
    if (s_img_buf) {
        heap_caps_free(s_img_buf);
        s_img_buf = nullptr;
        LOG_DEBUG("SPLASH", "Image buffer freed");
    }

    LOG_INFO("SPLASH", "Ready screen active");
}
```

---

## Testing Strategy

### Visual Verification Checklist

After implementing fixes, verify:

- [ ] **Splash fade-in**: Image appears smoothly from black over ~300ms (not instantly)
- [ ] **Backlight fade-in**: Backlight PWM increases smoothly from 0 to 255 over ~300ms
- [ ] **Splash hold**: Image stays visible for exactly ~2 seconds
- [ ] **Fade-out transition**: Image fades to black over ~800ms smoothly
- [ ] **Ready screen**: "Ready" text appears after fade completes
- [ ] **No stuttering**: Animation is smooth with no frame drops or jitter
- [ ] **No white flash**: Display never goes white or shows artifacts
- [ ] **Overall time**: Splash + transition completes in ~3.1 seconds (0.3 fade-in + 2.0 hold + 0.8 fade-out)

### Debug Logging

The solution includes frame counters and timing logs. Check serial output for:
```
[SPLASH] === Splash START ===
[SPLASH] Backlight set to 0
[SPLASH] JPEG /BatteryEmulator4_320x170.jpg  size=2048 bytes
[SPLASH] JPEG decoded in 45 ms -> 320x170
[SPLASH] Splash JPG attached to LVGL image object (320x170)
[SPLASH] Fade-in complete, holding splash for 2000ms
[SPLASH] === Splash END ===
[SPLASH] Loading Ready screen with fade transition...
[SPLASH] Ready screen created at 0x3fxxxxxx
[SPLASH] Creating 'Ready' label...
[SPLASH] Starting FADE_OUT animation...
[SPLASH] Fade-out animation complete (40 frames, 405 ms)
[SPLASH] Image buffer freed
[SPLASH] Ready screen active
```

---

## Root Cause Summary

| Issue | Root Cause | Fix |
|-------|-----------|-----|
| **No fade-in** | Uses `lv_scr_load()` instead of `lv_scr_load_anim()` | Add `FADE_IN` animation with 300ms duration |
| **Instant backlight** | `set_backlight(255)` is a raw PWM write, not animated | Add `animate_backlight_to()` with matching 300ms duration |
| **Rough animation** | `smart_delay(50)` is too long, breaks 20ms animation rhythm | Reduce to 20ms max delays during animations |
| **Transition timing** | Animation completes but timing constraints prevent smooth rendering | Improve synchronization in fade-out wait loop |
| **Slow frame rate** | Irregular `lv_timer_handler()` calls | Maintain consistent 20ms intervals with smart delay logic |

---

## Implementation Priority

**Phase 1 (Critical):** Implement backlight animation helper + splash fade-in  
**Phase 2 (High):** Improve splash hold timing with proper delay calculation  
**Phase 3 (High):** Rewrite transition timing and add safety timeouts  
**Phase 4 (Validation):** Test on hardware and verify smooth animation  

---

## References

- [LVGL Animation Documentation](https://docs.lvgl.io/master/overview/animation.html)
- [LVGL Screen Loading with Animation](https://docs.lvgl.io/master/widgets/core/scr.html)
- [LVGL Timer Handler](https://docs.lvgl.io/master/overview/timer.html)
- Current LVGL version: 8.4.x (as configured in platformio.ini)
- Animation resolution: 20ms (LV_ANIM_RESOLUTION = 20)
- Display refresh rate: 50 FPS (LV_DISP_DEF_REFR_PERIOD = 20ms)

---

## Conclusion

The splash screen architecture has **all the components** (LVGL animations enabled, image loading works, transitions are defined) but **lacks proper synchronization** between:

1. Animation state machine (`lv_timer_handler()` call frequency)
2. Backlight PWM changes
3. Screen loading animations
4. Hold duration timing

The fixes are **straightforward**: use LVGL's built-in animation system correctly with proper `lv_scr_load_anim()` calls, add a backlight animation function, and maintain consistent timer handler intervals.

**Estimated implementation time:** 30-45 minutes  
**Testing time:** 15-30 minutes  
**Total effort:** ~1 hour
