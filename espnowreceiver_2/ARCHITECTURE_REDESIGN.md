# Complete Architecture Redesign: Separate TFT and LVGL Implementations

**Date:** March 3, 2026  
**Status:** Critical Architectural Review  
**Priority:** CRITICAL - Full Codebase Restructuring Needed

---

## Executive Summary

The current approach of trying to make LVGL and TFT coexist through ifdef blocks is **fundamentally flawed**. These are two completely different graphics frameworks with incompatible rendering models, animation systems, and control flows.

**Core Problem:** 
- TFT-eSPI is **synchronous, blocking, and immediate**: You draw something, it appears now
- LVGL is **asynchronous, event-driven, and frame-based**: You queue animations and pump a message loop
- The current code tries to use TFT-style blocking waits and opacity changes with LVGL's async animation engine, which creates conflicting behaviors

**Solution:** 
Create **two completely separate implementations** from the ground up:
1. **TFT Implementation** - Pure TFT-eSPI with direct drawing (proven working)
2. **LVGL Implementation** - Pure LVGL with proper async architecture (from scratch)
3. **Build-time selection** - Choose which at compile time, no runtime mixing

---

## Part 1: Why Separate Implementations Are Essential

### 1.1 Fundamental Architectural Differences

#### **TFT-eSPI Model**
```
User calls:     drawImage() → setOpacity(val) → delay() → setOpacity(val)
Rendering:      Immediate (synchronous)
Animation:      Manual frame-by-frame loops with blocking delays
Control:        Function-based (blocking waits)
Memory:         Direct screen buffer writes
Timing:         Microsecond-level control via delay()
```

**Example TFT Fade-In:**
```cpp
void tft_fade_in_splash() {
    for (int opacity = 0; opacity <= 255; opacity += 5) {
        setOpacity(opacity);           // Direct write
        tft.pushImage(...);            // Push immediately
        delay(16);                     // Block and wait
    }
}
```

#### **LVGL Model**
```
User calls:     lv_scr_load_anim(...) → lv_timer_handler() → callback updates opacity
Rendering:      Deferred (asynchronous)
Animation:      State machine with automatic interpolation
Control:        Event-loop based (non-blocking)
Memory:         Dirty region tracking, partial updates
Timing:         Message loop pumping (20ms+ per iteration)
```

**Example LVGL Fade-In:**
```cpp
void lvgl_fade_in_splash() {
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
    // Animation is QUEUED, not executed yet
    
    while (lv_anim_count_running() > 0) {
        lv_timer_handler();  // Process one animation frame
        delay(10);           // Small delay, not blocking animation
    }
}
```

### 1.2 Why ifdef Mixing Fails

**Problem 1: Animation Model Conflict**
- TFT code calls `setOpacity()` directly → image appears instantly with that opacity
- LVGL code calls `lv_scr_load_anim()` → animation is queued but opacity not changed yet
- You can't call both simultaneously; one cancels the other

**Problem 2: Timing Model Conflict**
- TFT: `delay(16)` blocks the CPU, guarantees 16ms between frames
- LVGL: `smart_delay(10)` + message pump might take 10-50ms depending on other tasks
- Animations stutter and are unreliable

**Problem 3: Memory Model Conflict**
- TFT: Allocates full-screen double buffers, direct writes
- LVGL: Partial buffers (1/10 screen), dirty region tracking
- ifdef code wastes memory trying to support both

**Problem 4: Control Flow Conflict**
- TFT: All display logic is **synchronous** - wait for function to return means operation is complete
- LVGL: Display logic is **asynchronous** - function returns, animation continues in background
- Current code assumes sync behavior even with LVGL enabled

### 1.3 Examples of Current ifdef Mess

**display_splash_lvgl.cpp (Current Broken Code):**
```cpp
void display_splash_lvgl() {
    // TFT-style thinking: turn off backlight
    HAL::Display::LvglDriver::set_backlight(0);
    
    // Create screen (LVGL-style)
    lv_obj_t* splash_scr = lv_obj_create(NULL);
    
    // Try to animate with LVGL (async)
    lv_scr_load_anim(splash_scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
    
    // Try to animate backlight (TFT-style synchronous)
    HAL::Display::LvglDriver::animate_backlight_to(255, 300);
    
    // Wait for animations (LVGL-style async)
    while (lv_anim_count_running() > 0) {
        lv_timer_handler();
        smart_delay(10);  // But this breaks LVGL timing
    }
    
    // Hold splash (TFT-style blocking)
    const uint32_t hold_start = millis();
    while ((millis() - hold_start) < 2000) {
        lv_timer_handler();
        smart_delay(20);  // TFT blocking, wrong for LVGL
    }
}
```

**The Problem:** This tries to be both TFT and LVGL at the same time:
- Uses TFT's blocking `smart_delay()`
- Uses LVGL's async `lv_scr_load_anim()`
- Uses TFT's direct `set_backlight()`
- Uses LVGL's animation system

These conflict with each other, which is why the fade doesn't work.

---

## Part 2: Proposed Architecture

### 2.1 Directory Structure

```
src/
├── display/
│   ├── display_interface.h          ← Common abstract interface
│   ├── tft_impl/                    ← Pure TFT implementation
│   │   ├── tft_display.h
│   │   ├── tft_display.cpp
│   │   ├── tft_splash.cpp
│   │   ├── tft_widgets.cpp
│   │   └── ...
│   │
│   ├── lvgl_impl/                   ← Pure LVGL implementation
│   │   ├── lvgl_display.h
│   │   ├── lvgl_display.cpp
│   │   ├── lvgl_splash.cpp
│   │   ├── lvgl_widgets.cpp
│   │   ├── lvgl_animations.cpp
│   │   └── ...
│   │
│   ├── display.cpp                  ← Dispatcher (includes right impl)
│   └── ...
│
├── config/
│   └── display_config.h             ← Selects TFT vs LVGL
│
└── ...

platformio.ini                        ← Environments for tft_mode vs lvgl_mode
```

### 2.2 Display Interface (Abstract)

**File: `src/display/display_interface.h`**

This defines the common contract both implementations must satisfy:

```cpp
#pragma once

namespace Display {

/**
 * Abstract display interface - both TFT and LVGL implementations must satisfy this
 */
class IDisplay {
public:
    virtual ~IDisplay() = default;
    
    // Initialization
    virtual bool init() = 0;
    
    // Splash screen sequence
    virtual void display_splash_with_fade() = 0;
    virtual void display_initial_screen() = 0;
    
    // Status updates (called from data handlers)
    virtual void update_soc(float soc_percent) = 0;
    virtual void update_power(int32_t power_w) = 0;
    virtual void show_status_page() = 0;
    
    // Error display
    virtual void show_error_state() = 0;
    virtual void show_fatal_error(const char* component, const char* message) = 0;
    
    // Task handler (for async implementations)
    virtual void task_handler() = 0;
};

// Global instance
extern IDisplay* g_display;

} // namespace Display
```

### 2.3 TFT Implementation

**File: `src/display/tft_impl/tft_display.h`**

```cpp
#pragma once

#ifdef USE_TFT

#include "../display_interface.h"
#include <TFT_eSPI.h>

namespace Display {

class TftDisplay : public IDisplay {
public:
    TftDisplay();
    
    // Initialization
    bool init() override;
    
    // Splash screen sequence
    void display_splash_with_fade() override;
    void display_initial_screen() override;
    
    // Status updates
    void update_soc(float soc_percent) override;
    void update_power(int32_t power_w) override;
    void show_status_page() override;
    
    // Error display
    void show_error_state() override;
    void show_fatal_error(const char* component, const char* message) override;
    
    // Task handler (minimal for TFT - just for compatibility)
    void task_handler() override { /* No-op for TFT */ }
    
private:
    TFT_eSPI tft_;
    
    // Helper methods for TFT-specific operations
    void init_hardware();
    void draw_splash_image();
    void animate_backlight_tft(uint8_t target, uint32_t duration_ms);
};

} // namespace Display

#endif // USE_TFT
```

**File: `src/display/tft_impl/tft_display.cpp`**

This contains all the **proven working** TFT code:
- Direct pixel drawing
- Blocking animation loops
- Manual opacity control
- Backlight PWM changes
- No LVGL dependencies

Key principles:
- Use direct `tft.` calls
- Blocking `delay()` for precise timing
- Manual animation loops
- No async complexity

### 2.4 LVGL Implementation

**File: `src/display/lvgl_impl/lvgl_display.h`**

```cpp
#pragma once

#ifdef USE_LVGL

#include "../display_interface.h"
#include <lvgl.h>
#include <TFT_eSPI.h>

namespace Display {

class LvglDisplay : public IDisplay {
public:
    LvglDisplay();
    
    // Initialization
    bool init() override;
    
    // Splash screen sequence
    void display_splash_with_fade() override;
    void display_initial_screen() override;
    
    // Status updates
    void update_soc(float soc_percent) override;
    void update_power(int32_t power_w) override;
    void show_status_page() override;
    
    // Error display
    void show_error_state() override;
    void show_fatal_error(const char* component, const char* message) override;
    
    // Task handler (CRITICAL for LVGL - pumps message loop)
    void task_handler() override;
    
private:
    TFT_eSPI tft_;
    lv_disp_t* disp_;
    
    // LVGL-specific initialization
    void init_lvgl_core();
    void init_lvgl_driver();
    void init_hardware();
    
    // Animation helpers
    void animate_backlight_lvgl(uint8_t target, uint32_t duration_ms);
    void wait_for_animation(uint32_t timeout_ms);
};

} // namespace Display

#endif // USE_LVGL
```

**File: `src/display/lvgl_impl/lvgl_display.cpp`**

Key principles:
- **Never blocks unnecessarily** - LVGL animations run asynchronously
- **Pumps message loop regularly** - `lv_timer_handler()` called frequently
- **Uses async/await pattern** - Queue animation, then wait for completion with timer
- **No TFT-style direct writes** - All drawing through LVGL objects
- **Proper animation use** - Uses `lv_scr_load_anim()`, `lv_anim_t`, etc.

Example splash implementation:
```cpp
void LvglDisplay::display_splash_with_fade() {
    LOG_INFO("SPLASH", "=== LVGL Splash START ===");
    
    // Create splash screen (LVGL-style)
    lv_obj_t* splash_scr = create_splash_screen();
    
    // Load with fade-in animation (async)
    lv_scr_load_anim(splash_scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
    
    // Animate backlight (also async)
    animate_backlight_lvgl(255, 300);
    
    // Wait for animations (async-aware)
    wait_for_animation(500);  // 300ms animation + buffer
    
    // Hold splash (async - task handler continues processing)
    const uint32_t hold_start = millis();
    while (millis() - hold_start < 2000) {
        task_handler();  // Pump LVGL message loop
        smart_delay(20); // Sleep between pumps
    }
    
    LOG_INFO("SPLASH", "=== LVGL Splash END ===");
}
```

---

## Part 3: Build System Integration

### 3.1 platformio.ini Changes

Create separate environments:

```ini
[env:receiver_tft]
extends = esp32_base
build_flags = 
    -DUSE_TFT
    -DSCREEN_WIDTH=320
    -DSCREEN_HEIGHT=170
lib_ignore = 
    lvgl

[env:receiver_lvgl]
extends = esp32_base
build_flags = 
    -DUSE_LVGL
    -DSCREEN_WIDTH=320
    -DSCREEN_HEIGHT=170
    -DLV_CONF_INCLUDE_SIMPLE=1
lib_ignore = 
    # Nothing ignored - full LVGL stack

[esp32_base]
platform = espressif32@6.5.0
board = lilygo-t-display-s3
framework = arduino
# ... rest of shared config
```

### 3.2 Conditional Compilation

**File: `src/display/display.cpp`**

Single dispatcher file that selects implementation at compile time:

```cpp
#include "display_interface.h"

#ifdef USE_TFT
    #include "tft_impl/tft_display.h"
    using DisplayImpl = Display::TftDisplay;
#elif USE_LVGL
    #include "lvgl_impl/lvgl_display.h"
    using DisplayImpl = Display::LvglDisplay;
#else
    #error "Must define either USE_TFT or USE_LVGL"
#endif

// Global instance
Display::IDisplay* Display::g_display = nullptr;

void init_display() {
    if (!Display::g_display) {
        Display::g_display = new DisplayImpl();
    }
    if (!Display::g_display->init()) {
        LOG_ERROR("DISPLAY", "Failed to initialize display");
    }
}

void displaySplashWithFade() {
    if (Display::g_display) {
        Display::g_display->display_splash_with_fade();
    }
}

// ... other wrapper functions
```

**File: `src/main.cpp`**

No changes needed - just calls the common interface:

```cpp
void setup() {
    // ... existing code ...
    
    init_display();  // Uses either TFT or LVGL depending on build
    displaySplashWithFade();
    displayInitialScreen();
}

void loop() {
    // ... existing code ...
    Display::g_display->task_handler();  // Works for both TFT (no-op) and LVGL
    smart_delay(10);
}
```

---

## Part 4: TFT Implementation Details

### 4.1 Known Working Splash Sequence

We know the **TFT splash works**. The implementation should:

1. **Initialize backlight hardware** - PWM GPIO setup
2. **Load splash image from LittleFS** - Standard file operations
3. **Fade in** - Gradual backlight increase (manual loop)
4. **Display** - Direct `tft.pushImage()` or `tft.drawBitmap()`
5. **Hold** - Keep on screen for 2 seconds
6. **Fade out** - Gradual backlight decrease
7. **Transition** - Load next screen with optional fade effect

**Key code pattern:**
```cpp
void TftDisplay::display_splash_with_fade() {
    // 1. Prepare
    set_backlight(0);  // Off
    load_image_from_littlefs("/splash.jpg");
    
    // 2. Display immediately (TFT is synchronous)
    tft.fillScreen(TFT_BLACK);
    draw_splash_image();
    
    // 3. Fade in (manual loop, precise timing)
    for (int brightness = 0; brightness <= 255; brightness += 5) {
        set_backlight(brightness);
        delay(16);  // 16ms = ~60 FPS, blocking is OK for TFT
    }
    
    // 4. Hold (simple delay)
    delay(2000);
    
    // 5. Fade out (manual loop)
    for (int brightness = 255; brightness >= 0; brightness -= 5) {
        set_backlight(brightness);
        delay(16);
    }
}
```

### 4.2 Advantages of TFT Implementation

✅ **Proven working** - We know the original code works  
✅ **Simple logic** - Straightforward synchronous calls  
✅ **Predictable timing** - `delay()` guarantees frame intervals  
✅ **Low overhead** - Minimal CPU usage during animations  
✅ **Direct control** - Pixel-level precision available  

---

## Part 5: LVGL Implementation Details

### 5.1 LVGL-Native Splash Sequence

Pure LVGL architecture, **from the ground up**:

1. **Initialize LVGL core** - `lv_init()`
2. **Register display driver** - Flush callbacks, buffer management
3. **Create splash screen** - LVGL objects (lv_obj_t)
4. **Load image as LVGL image widget** - `lv_img_create()` + descriptor
5. **Queue fade-in animation** - `lv_scr_load_anim(..., FADE_IN, ...)`
6. **Queue backlight animation** - `lv_anim_t` with PWM callback
7. **Pump message loop** - `lv_timer_handler()` in loop
8. **Queue transition animation** - `lv_scr_load_anim(..., FADE_OUT, ...)`
9. **Wait for completion** - Check `lv_anim_count_running()`

**Key code pattern:**
```cpp
void LvglDisplay::display_splash_with_fade() {
    // 1. Create screen (LVGL object)
    lv_obj_t* splash = lv_obj_create(NULL);
    lv_obj_set_size(splash, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    
    // 2. Add image to screen (LVGL widget)
    lv_obj_t* img = lv_img_create(splash);
    lv_img_set_src(img, &splash_image_descriptor);
    lv_obj_center(img);
    
    // 3. Queue fade-in animation (non-blocking)
    lv_scr_load_anim(splash, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
    
    // 4. Queue backlight animation (LVGL animation system)
    animate_backlight_lvgl(255, 300);
    
    // 5. Wait for animations to complete (async-aware)
    wait_for_animation(500);  // 300ms anim + buffer
    
    // 6. Hold splash (keep pumping LVGL)
    hold_screen_for(2000);  // Pumps lv_timer_handler() continuously
}

void LvglDisplay::wait_for_animation(uint32_t timeout_ms) {
    const uint32_t start = millis();
    while (lv_anim_count_running() > 0 && millis() - start < timeout_ms) {
        lv_timer_handler();  // Process one animation frame
        smart_delay(10);     // Small sleep between pumps
    }
}

void LvglDisplay::hold_screen_for(uint32_t duration_ms) {
    const uint32_t start = millis();
    while (millis() - start < duration_ms) {
        lv_timer_handler();  // Keep rendering
        smart_delay(20);     // Reasonable pump interval
    }
}
```

### 5.2 Advantages of LVGL Implementation

✅ **Professional animation system** - Built-in fade/transition effects  
✅ **Scalable** - Can add complex widgets without rewriting  
✅ **Modern UI** - Touch input, themes, advanced widgets  
✅ **Memory efficient** - Partial screen updates, dirty tracking  
✅ **Async-native** - Designed for embedded event loops  

### 5.3 Disadvantages of LVGL (Be Honest)

⚠️ **Complexity** - Requires understanding LVGL's event model  
⚠️ **Configuration** - `lv_conf.h` has 300+ settings  
⚠️ **Learning curve** - Different paradigm than blocking TFT  
⚠️ **Debug harder** - Async behavior can be confusing  
⚠️ **Timing variability** - Message loop pump timing affects animations  

---

## Part 6: Migration and Testing Strategy

### 6.1 Phase 1: Create Both Implementations (Week 1-2)

**Step 1: TFT Implementation**
1. Extract all current TFT code into `src/display/tft_impl/`
2. Wrap in `TftDisplay` class
3. Implement `IDisplay` interface
4. Create `receiver_tft` environment in platformio.ini
5. **Test:** `pio run -e receiver_tft` - should compile and work identically to current code

**Step 2: LVGL Implementation**
1. Create `src/display/lvgl_impl/` directory
2. Implement `LvglDisplay` class from scratch (no copy-paste from TFT code)
3. Write LVGL-specific splash, animations, widgets
4. Create `receiver_lvgl` environment in platformio.ini
5. **Test:** `pio run -e receiver_lvgl` - compile and flash to hardware

### 6.2 Phase 2: Validation (Week 2-3)

**TFT Build Testing:**
```bash
pio run -e receiver_tft -t upload
# Flash and verify:
# - Splash fades in smoothly
# - Holds for 2 seconds
# - Fades to Ready screen
# - No visual artifacts
# - Timing consistent
```

**LVGL Build Testing:**
```bash
pio run -e receiver_lvgl -t upload
# Flash and verify same as above
# Plus:
# - LVGL animation looks professional
# - No stuttering
# - Memory usage reasonable
# - Task handler properly integrated
```

### 6.3 Phase 3: Feature Parity (Week 3-4)

Ensure both implementations support:
- ✅ Splash screen with fade
- ✅ Initial "Ready" screen
- ✅ SOC display
- ✅ Power display
- ✅ Error screen (red)
- ✅ Fatal error screen
- ✅ Backlight control
- ✅ Network status indicators
- ✅ All existing display callbacks

### 6.4 Phase 4: Documentation (Week 4)

Document:
- How to add new display feature to both implementations
- Architecture decisions and rationale
- Performance metrics (memory, CPU, timing)
- Known limitations of each approach
- Migration guidelines if switching in future

---

## Part 7: File Manifest

### Files to Create

```
src/display/
├── display_interface.h                 (NEW - 100 lines)
│
├── tft_impl/
│   ├── tft_display.h                   (NEW - 50 lines)
│   ├── tft_display.cpp                 (NEW - 300 lines, copy from existing)
│   ├── tft_splash.cpp                  (NEW - extracted from current code)
│   ├── tft_widgets.cpp                 (NEW - extracted from current code)
│   └── tft_status_page.cpp             (NEW - extracted from current code)
│
├── lvgl_impl/
│   ├── lvgl_display.h                  (NEW - 60 lines)
│   ├── lvgl_display.cpp                (NEW - 400 lines, new implementation)
│   ├── lvgl_splash.cpp                 (NEW - LVGL-native splash, ~250 lines)
│   ├── lvgl_animations.cpp             (NEW - animation helpers, ~150 lines)
│   ├── lvgl_widgets.cpp                (NEW - LVGL widgets, ~300 lines)
│   └── lvgl_status_page.cpp            (NEW - LVGL status page, ~200 lines)
│
└── display.cpp                         (MODIFIED - new dispatcher)

platformio.ini                          (MODIFIED - new environments)
src/main.cpp                            (MINIMAL CHANGES - add interface calls)
```

### Files to Remove

```
src/display/
├── display_splash_lvgl.cpp             (DELETE)
├── display_splash.cpp                  (Replace with interface wrapper)
├── display_core.cpp                    (DELETE)
├── display_core_lvgl.cpp               (DELETE)
└── ... (consolidate into tft_impl/ or lvgl_impl/)
```

---

## Part 8: Justification and Recommendation

### Why This Is Better

| Aspect | Current Approach | Proposed Approach |
|--------|------------------|-------------------|
| **Clarity** | Mixed ifdef logic | Two clear implementations |
| **Maintainability** | Complex conflicts | Each impl self-contained |
| **Debugging** | Hard to trace issues | Isolated issues easier to fix |
| **Testing** | Both systems tested together | Test each separately |
| **Performance** | Overhead from unused code | Only relevant code compiled |
| **Scalability** | Hard to add features | Easy to extend either system |
| **Documentation** | Confusing | Clear architecture |

### Recommendation

**Implement this architecture immediately:**

1. **Week 1:** Extract TFT code into separate module, ensure it still works
2. **Week 2:** Build LVGL implementation from scratch using proper patterns
3. **Week 3:** Validate both implementations work independently
4. **Week 4:** Document and clean up, consider deprecating whichever approach

This is **not more work** - it's actually less work because:
- No more debugging cryptic ifdef interactions
- Each implementation is simpler and clearer
- Testing is faster (build one, test one)
- Future features only need implementing once (not fighting both systems)

---

## Part 9: Risk Assessment

### Risk: LVGL Still Doesn't Work

**Mitigation:**
- TFT version is proven working fallback
- Can always ship with TFT while fixing LVGL
- No pressure to use LVGL if it's not ready

### Risk: Code Duplication

**Reality:**
- Some duplication is acceptable for clarity
- ~600 lines of TFT duplicated, ~800 lines of LVGL new
- Small price for eliminating ifdef mess
- Shared interface keeps API consistent

### Risk: Breaking Current TFT Functionality

**Mitigation:**
- Extract TFT code carefully, test after each step
- TFT implementation should be 99% copy of current code
- Use `receiver_tft` environment as smoke test

---

## Part 10: Success Criteria

✅ **Build succeeds:** Both `pio run -e receiver_tft` and `pio run -e receiver_lvgl` compile  
✅ **TFT works:** Identical behavior to current implementation  
✅ **LVGL works:** Smooth fade-in/out, professional animations  
✅ **No ifdef mess:** Display logic is clean and readable  
✅ **Interface satisfied:** Both implementations satisfy `IDisplay`  
✅ **Documentation:** Clear explanation of architecture  

---

## Conclusion

The current attempt to make LVGL work through ifdef patches and partial rewrites **cannot succeed** because the frameworks are fundamentally incompatible.

**The only correct solution is complete separation:**
1. Keep TFT as-is (proven working)
2. Build LVGL completely from scratch using its proper patterns
3. Use build-time configuration to choose, not runtime mixing

This is the professional, maintainable, scalable approach.

---

## Next Steps

Once you approve this architecture:

1. I will create the `display_interface.h` abstract class
2. Extract TFT code into `tft_impl/` directory
3. Create LVGL implementation from scratch in `lvgl_impl/`
4. Update platformio.ini with separate environments
5. Update main.cpp to use the interface
6. Create comprehensive implementation guide

Shall I proceed with this approach?
