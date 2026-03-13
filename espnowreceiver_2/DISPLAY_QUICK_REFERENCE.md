## Display Architecture - Quick Reference

**For the impatient developer:** Use this when you need answers fast.

---

## Quick Facts

| Question | Answer |
|----------|--------|
| **How do I switch between TFT and LVGL?** | Change `platformio.ini`: `-DUSE_TFT` or `-DUSE_LVGL` |
| **Where is the abstract interface?** | [src/display/display_interface.h](src/display/display_interface.h) |
| **Where is TFT implementation?** | [src/display/tft_impl/](src/display/tft_impl/) |
| **Where is LVGL implementation?** | [src/display/lvgl_impl/](src/display/lvgl_impl/) |
| **How do I display something?** | Call `Display::g_display->your_method()` |
| **What methods must I implement?** | All pure virtuals in `IDisplay` class |
| **How big is the interface?** | 10 methods total |
| **Can I add more implementations?** | Yes! Create new folder, implement `IDisplay` |

---

## Implementation Checklist

```
[ ] Step 1: init_hardware() - Copy from tft_espi_display_driver.cpp
[ ] Step 2: init_backlight() - Copy from lvgl_driver.cpp
[ ] Step 3: set_backlight() - Copy from lvgl_driver.cpp
[ ] Step 4: animate_backlight() - Custom TFT version
[ ] Step 5: load_and_draw_splash() - Copy from display_splash_lvgl.cpp
[ ] Step 6: display_splash_with_fade() - Orchestrate sequence
[ ] Step 7: display_initial_screen() - Show "Ready"
[ ] Step 8: update_soc() / draw_soc() - Simple text for now
[ ] Step 9: update_power() / draw_power() - Simple text for now
[ ] Step 10: show_status_page() - Full layout
[ ] Step 11: show_error_state() - Red screen
[ ] Step 12: show_fatal_error() - Red screen with details

[ ] Compile without errors
[ ] Run on hardware
[ ] Verify splash sequence
[ ] Check backlight fades smoothly
[ ] Test data updates
[ ] Test error displays
```

---

## Code Snippets

### How to use display in application code:

```cpp
// Initialize
init_display();

// Show splash
displaySplashWithFade();

// Show ready screen
displayInitialScreen();

// Show main content
show_status_page();

// Update data
display_soc(75.5);
display_power(5000);

// Handle errors
show_error_state();
display_fatal_error("WiFi", "Connection failed");
```

### How to implement a method:

```cpp
// In tft_display.cpp

void TftDisplay::my_new_method() {
    // Step 1: Log what you're doing
    LOG_DEBUG("TFT", "Doing something...");
    
    // Step 2: Use tft_ member variable
    tft_.fillScreen(TFT_BLACK);
    tft_.drawString("Hello", 10, 10);
    
    // Step 3: Log result
    LOG_DEBUG("TFT", "Done!");
}
```

### How to add includes:

```cpp
// At top of tft_display.cpp:

#include "tft_display.h"
#include "../../common.h"
#include "../../helpers.h"
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <JPEGDecoder.h>
#include <esp_heap_caps.h>
```

---

## Common Tasks

### Extract code from another file:
1. Find the source file and method
2. Copy the implementation
3. Paste into corresponding stub
4. Update comments if needed
5. Verify includes are present
6. Compile and test

### Add a new method to interface:
1. Add pure virtual to `IDisplay` class
2. Add declaration to `TftDisplay` class
3. Add declaration to `LvglDisplay` class
4. Implement in both .cpp files
5. Update documentation

### Test compile-time selection:
```bash
# Test TFT only
pio run -e esp32s3-tft

# Test LVGL only  
pio run -e esp32s3-lvgl

# Both should work, but not if you define both flags
# (should error: "Must define either USE_TFT or USE_LVGL")
```

### Debug a method:
1. Add `LOG_DEBUG()` calls throughout
2. Add `LOG_INFO()` at key points
3. Compile with `-DLOG_LEVEL=DEBUG`
4. Upload and run
5. Monitor with: `pio run -t monitor`
6. Look for your messages in the log

---

## File Locations

```
✅ Interface Definition:
   src/display/display_interface.h

✅ Dispatcher:
   src/display/display.cpp
   src/display/display.h (public API)

✅ TFT Implementation:
   src/display/tft_impl/tft_display.h
   src/display/tft_impl/tft_display.cpp

🔲 LVGL Implementation (TODO):
   src/display/lvgl_impl/lvgl_display.h
   src/display/lvgl_impl/lvgl_display.cpp

📖 Documentation:
   DISPLAY_ARCHITECTURE_SUMMARY.md (you are here)
   DISPLAY_ARCHITECTURE_PROGRESS.md (detailed progress tracking)
   TFT_IMPLEMENTATION_GUIDE.md (step-by-step code extraction)
```

---

## Common Errors & Solutions

| Error | Solution |
|-------|----------|
| `#error "Display: Must define either USE_TFT or USE_LVGL"` | Add `-DUSE_TFT` or `-DUSE_LVGL` to platformio.ini |
| `error: undefined reference to 'Display::g_display'` | Make sure display.cpp is compiled |
| `error: tft_ is not a member of TftDisplay` | Check that tft_ is declared in tft_display.h private section |
| `error: cannot convert 'uint16_t' to 'lv_color_t'` | You're mixing TFT and LVGL code - use correct implementation |
| `Splash image displays with wrong colors` | Check `tft_.setSwapBytes(true)` is called |
| `Backlight doesn't fade smoothly` | Check `animate_backlight()` timing and step calculation |
| `JPEG splash won't load` | Check file path `/BatteryEmulator4_320x170.jpg` exists in LittleFS |

---

## Tips & Tricks

### For faster development:
- Copy all the code at once, then compile
- Fix compilation errors in order
- Use search/replace for consistent changes
- Test frequently to catch problems early

### For better debugging:
- Add `LOG_DEBUG()` at method entry and exit
- Log variable values before/after changes
- Check serial monitor for messages
- Add timestamps to debug output

### For cleaner code:
- Keep methods focused on one task
- Use helper methods for repeated logic
- Update documentation as you code
- Follow the existing code style

### For performance:
- Minimize full-screen redraws
- Use partial updates when possible
- Cache calculated values
- Profile with timing logs

---

## Key Concepts

### What is `IDisplay`?
An abstract base class (pure virtual interface) that defines all display operations. Both TFT and LVGL must implement all these methods.

### What is compile-time selection?
Using `#ifdef USE_TFT` and `#elif USE_LVGL` to choose which implementation to compile. Only one is included in the final binary.

### What is the dispatcher?
The `display.cpp` file that handles selection, instantiation, and provides the global `Display::g_display` instance to the application.

### Why separate implementations?
- No mixing of concerns (TFT code and LVGL code never interact)
- Each can be optimized independently
- Binary is smaller (no unused code)
- Easy to add new implementations

### Why use a global instance?
Matches Arduino patterns, simple access from anywhere, easy migration from old code. Can be replaced with dependency injection in the future.

---

## Next Steps

1. **Read full progress guide**: [DISPLAY_ARCHITECTURE_PROGRESS.md](DISPLAY_ARCHITECTURE_PROGRESS.md)
2. **Follow implementation guide**: [TFT_IMPLEMENTATION_GUIDE.md](TFT_IMPLEMENTATION_GUIDE.md)
3. **Implement TFT methods** using code extraction instructions
4. **Test on hardware** with step-by-step verification
5. **Create LVGL parallel** implementation
6. **Test both implementations**
7. **Add to build variants** in platformio.ini

---

## Emergency Rollback

If something goes wrong:

```bash
# See what changed
git status

# Revert to last known good state
git checkout src/display/

# Or revert specific file
git checkout src/display/tft_impl/tft_display.cpp

# Continue from checkpoint
```

---

## Questions?

| What? | Where? |
|-------|--------|
| **Why was this refactored?** | See DISPLAY_ARCHITECTURE_PROGRESS.md - Benefits section |
| **How do I implement TFT?** | See TFT_IMPLEMENTATION_GUIDE.md - Step by step |
| **What methods do I need?** | See display_interface.h - All virtual methods |
| **How do I test?** | See DISPLAY_ARCHITECTURE_PROGRESS.md - Testing Checklist |
| **Can I add a new display type?** | Yes! Copy TFT impl structure, create new folder, implement IDisplay |

---

**Status**: 🟢 Ready to implement TFT methods

**Time to complete TFT**: 2-4 hours
**Time to complete everything**: 7-14 hours

**Start with**: Step 1 in [TFT_IMPLEMENTATION_GUIDE.md](TFT_IMPLEMENTATION_GUIDE.md)
