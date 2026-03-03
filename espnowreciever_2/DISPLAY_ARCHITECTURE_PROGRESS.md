## Display Architecture Redesign - Progress Summary

**Date**: Current Work Session  
**Status**: Phase 1-2 Complete, Phase 3-4 In Progress

---

## COMPLETED TASKS ✅

### Phase 1: Display Interface Created ✅
- **File**: [src/display/display_interface.h](src/display/display_interface.h)
- **Status**: Complete and documented
- **What it provides**:
  - Abstract `IDisplay` base class defining all display operations
  - Clear separation between interface and implementation
  - Comprehensive docstrings explaining each method
  - Includes `task_handler()` for periodic processing (LVGL needs this)

### Phase 2: TFT Implementation Skeleton Created ✅
- **Files**:
  - Header: [src/display/tft_impl/tft_display.h](src/display/tft_impl/tft_display.h)
  - Implementation: [src/display/tft_impl/tft_display.cpp](src/display/tft_impl/tft_display.cpp)
- **Status**: Skeleton complete with method stubs
- **What's included**:
  - Full interface implementation
  - Hardware initialization (init_hardware, init_backlight)
  - Splash screen with fade (display_splash_with_fade)
  - Data updates (update_soc, update_power)
  - Error display methods
  - Helper methods for backlight animation
  - JPEG splash image loading stub
  - LOG_DEBUG/LOG_INFO/LOG_ERROR calls for debugging

### Phase 2b: Display Dispatcher Already In Place ✅
- **File**: [src/display/display.cpp](src/display/display.cpp)
- **Status**: Complete and working
- **What it does**:
  - Conditional compilation: `#ifdef USE_TFT` / `#elif USE_LVGL`
  - Creates global `Display::g_display` instance
  - Provides C-style wrapper API: `init_display()`, `displaySplashWithFade()`, etc.
  - Allows application code to be display-agnostic

---

## TODO: REMAINING WORK 📋

### Phase 3: Implement TFT Methods (HIGH PRIORITY)
Must extract from existing TFT code and fill in these stubs:

#### Critical Methods (needed for splash/startup):
1. **`TftDisplay::init_hardware()`**
   - Extract TFT initialization from existing setup code
   - Setup GPIO pins (CS, DC, RST, MOSI, MISO, CLK)
   - Initialize TFT_eSPI display controller
   - Set rotation/orientation

2. **`TftDisplay::init_backlight()`**
   - Setup GPIO for backlight PWM
   - Configure timer/PWM parameters
   - Start with brightness = 0

3. **`TftDisplay::load_and_draw_splash()`**
   - Read `/BatteryEmulator4_320x170.jpg` from LittleFS
   - Use JPEGDecoder to decode
   - Use `tft_.pushImage()` to render

4. **`TftDisplay::animate_backlight()`**
   - Smooth fade from current to target brightness
   - Calculate step size: `steps = duration_ms / frame_time`
   - Use `delay()` between steps (~16ms for 60fps)

5. **`TftDisplay::set_backlight()`**
   - Set PWM duty cycle for backlight GPIO
   - Valid range: 0-255

#### Data Display Methods (needed for runtime updates):
6. **`TftDisplay::draw_soc()`** - Draw SOC percentage on screen
7. **`TftDisplay::draw_power()`** - Draw power value on screen
8. **`TftDisplay::update_soc()` and `update_power()`** - Call draw methods

#### Status/Error Display:
9. **`TftDisplay::show_status_page()`** - Full status screen layout
10. **`TftDisplay::show_error_state()`** - Red screen with error indicator
11. **`TftDisplay::show_fatal_error()`** - Red screen with component/message

#### Integration:
12. **`TftDisplay::display_splash_with_fade()`** - Orchestrate splash sequence
13. **`TftDisplay::display_initial_screen()`** - Show "Ready" message

---

### Phase 4: Create LVGL Implementation
Once TFT is working, create parallel LVGL implementation:

- **Path**: `src/display/lvgl_impl/`
- **Files**: `lvgl_display.h`, `lvgl_display.cpp`
- **Approach**: 
  - Same interface (IDisplay)
  - Initialize LVGL with: `lv_init()`, `lv_disp_drv_t`, etc.
  - Use LVGL styles and objects instead of direct pixel writing
  - Implement animations using LVGL animations (non-blocking)
  - `task_handler()` calls `lv_task_handler()` for message loop

---

### Phase 5: Update platformio.ini
Add build environments for both variants:

```ini
[env:esp32s3-tft]
build_flags = 
    -DUSE_TFT
    -DUSE_BACKLIGHT_PWM
    
[env:esp32s3-lvgl]
build_flags =
    -DUSE_LVGL
    ...LVGL libs...
```

---

### Phase 6: Update main.cpp Integration
Ensure main.cpp properly calls:

```cpp
void setup() {
    init_display();                  // Initialize display system
    displaySplashWithFade();         // Show splash with fade
    displayInitialScreen();          // Show "Ready"
    show_status_page();              // Enter main status view
}

void loop() {
    // ... other tasks ...
    display_task_handler();          // Call periodically
    
    // Update data when available
    if (new_soc_available) {
        display_soc(soc_value);
    }
    if (new_power_available) {
        display_power(power_value);
    }
}
```

---

## IMMEDIATE NEXT STEPS 🎯

1. **Extract TFT Code** (1-2 hours)
   - Find existing TFT initialization code
   - Find existing splash image loading code
   - Find existing backlight PWM control code
   - Copy into TFT implementation stubs

2. **Test TFT Compilation** (30 min)
   - Add `#define USE_TFT` to platformio.ini
   - Run `pio run -e esp32s3-tft`
   - Fix any compilation errors

3. **Test TFT on Hardware** (1 hour)
   - Flash to device
   - Verify splash displays correctly
   - Verify backlight fades in/out
   - Verify SOC/Power updates work

4. **Create LVGL Implementation** (2-3 hours)
   - Create parallel structure
   - Implement same interface
   - Test LVGL rendering

---

## FILE STRUCTURE CREATED

```
src/display/
├── display_interface.h          ← Abstract base (IDisplay)
├── display.cpp                  ← Dispatcher & global API
├── display.h                    ← Public C-style API
├── tft_impl/
│   ├── tft_display.h
│   └── tft_display.cpp          ← TFT implementation (stubs)
└── lvgl_impl/
    ├── lvgl_display.h           ← TODO: Create
    └── lvgl_display.cpp         ← TODO: Create
```

---

## KEY DESIGN DECISIONS

1. **Compile-Time Selection** - Choose between TFT/LVGL at build time, not runtime
   - Avoids binary bloat
   - Avoids runtime overhead
   - Cleaner code paths

2. **Pure Implementation Classes** - Each implementation completely independent
   - No shared state/logic
   - Can evolve separately
   - Easy to test in isolation

3. **Global Instance Pattern** - Single `Display::g_display` pointer
   - Simple access from anywhere
   - Matches existing Arduino patterns
   - Easy transition from old code

4. **Blocking Rendering for TFT** - Keeps code simple
   - TFT doesn't need async rendering
   - Animations are simple loops with delay
   - No message queue/event loop needed

5. **Task Handler for LVGL** - Will need message loop
   - LVGL needs `lv_task_handler()` called periodically
   - Interface includes `task_handler()` for this
   - One loop handles both implementations

---

## COMPILATION PROCESS

When building for TFT:
1. CMake includes: `display_interface.h`
2. CMake includes: `display.cpp`
3. CMake includes: `tft_impl/tft_display.cpp`
4. Conditional compilation skips: `lvgl_impl/*.cpp`
5. Linker produces: binary with TFT code only (~20KB)

When building for LVGL:
1. CMake includes: `display_interface.h`
2. CMake includes: `display.cpp`
3. CMake includes: `lvgl_impl/lvgl_display.cpp`
4. Conditional compilation skips: `tft_impl/*.cpp`
5. Linker produces: binary with LVGL code only (~50KB)

---

## TESTING CHECKLIST

- [ ] TFT compilation succeeds with `USE_TFT` defined
- [ ] LVGL compilation succeeds with `USE_LVGL` defined
- [ ] Cannot compile with both defined (should error)
- [ ] Cannot compile with neither defined (should error)
- [ ] TFT splash displays correctly on hardware
- [ ] TFT backlight fades in/out smoothly
- [ ] TFT SOC/Power values update in real-time
- [ ] LVGL splash displays with LVGL animations
- [ ] LVGL task_handler() pumps message loop correctly
- [ ] Switching between implementations (rebuild and flash)

---

## MIGRATION PATH

For teams wanting to switch from TFT to LVGL:

1. Build TFT version with new code → `env:esp32s3-tft`
2. Build LVGL version in parallel → `env:esp32s3-lvgl`
3. Test both on same hardware
4. Switch build environment: Change platformio.ini default
5. Flash devices with new environment
6. No application code changes needed!

---

## BENEFITS OF THIS ARCHITECTURE

✅ **Clear Interface** - IDisplay defines exact contract
✅ **Implementation Isolation** - TFT and LVGL never mixed
✅ **Compile-Time Safety** - Errors caught during build, not runtime
✅ **Code Clarity** - No ifdef blocks in application code
✅ **Easy Testing** - Can test each implementation separately
✅ **Future-Proof** - Can add more implementations (ePaper, OLED, etc.)
✅ **Binary Size** - No unused code in final binary
✅ **Performance** - No runtime dispatch overhead

---

This architecture provides a solid foundation for a modern, maintainable display system that can support multiple hardware configurations without compromises.
