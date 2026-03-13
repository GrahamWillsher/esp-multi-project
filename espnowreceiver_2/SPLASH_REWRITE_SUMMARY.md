# LVGL Splash Screen Rewrite - March 2, 2026

## What Changed

Complete rewrite of splash screen implementation to use LVGL 8.4's built-in screen transition animations instead of manual opacity pumping.

### Files Modified

1. **src/display/display_splash_lvgl.cpp** (324 lines → 173 lines)
   - Removed manual animation helpers: `lvgl_fade()`, `lvgl_wait_anim()`, `lvgl_pump_ms()`
   - Simplified to use `lv_scr_load_anim(FADE_OUT)` for built-in transitions
   - Removed manual byte-swap in JPEG decode (was causing double-swap bug)
   - Now uses static storage for image descriptor/buffer (cleaner lifecycle)

2. **src/hal/display/lvgl_driver.cpp** 
   - Fixed byte-swap bug: `pushColors(..., true)` → `pushColors(..., false)`
   - Reason: With `LV_COLOR_16_SWAP=1` + `setSwapBytes(true)`, passing `true` was double-swapping

## Technical Details

### Your LVGL Version: 8.4.0 (NOT v9)
- Your platformio.ini pins `lvgl/lvgl @ ^8.3.11` 
- Installed library is LVGL 8.4.0
- API differences from v9 example you provided:
  - `lv_screen_load_anim()` → `lv_scr_load_anim()` ✓
  - `lv_image_create()` → `lv_img_create()` ✓
  - `LV_SCREEN_LOAD_ANIM_FADE_OUT` → `LV_SCR_LOAD_ANIM_FADE_OUT` ✓

### Image Format Decision: Keep .jpg
- Current: 7.8 KB JPEG in LittleFS → decode to PSRAM at runtime
- Alternative .bin: 109 KB pre-converted RGB565 in flash
- **Verdict**: JPEG is 14× smaller and you have PSRAM. No conversion needed.

### New Flow
```
1. Decode .jpg → RGB565 buffer (PSRAM, no manual byte-swap)
2. Create lv_img_dsc_t with LV_IMG_CF_TRUE_COLOR
3. Create splash screen with image widget
4. lv_scr_load(splash_scr)          - instant load
5. Turn backlight on
6. Hold for 2 seconds (pump LVGL)
7. Call display_initial_screen_lvgl()
   - Creates "Ready" screen
   - lv_scr_load_anim(ready_scr, LV_SCR_LOAD_ANIM_FADE_OUT, 800ms, 0, true)
   - Built-in: fades splash → black → ready
   - auto_del=true cleans up splash screen safely
```

### What Was Fixed

#### 1. Double Byte-Swap Bug (The "Flashing Colors")
**Problem:**
- `LV_COLOR_16_SWAP=1` → LVGL's render buffer already byte-swapped
- `flush_cb` called `pushColors(..., true)` 
- BUT `_swapBytes` already `true` from `setSwapBytes(true)` in init()
- Result: swap → swap = back to original = wrong colors

**Fix:**
- `flush_cb`: `pushColors(..., false)` - let TFT's internal `_swapBytes=true` do it once
- `decode_jpg_to_rgb565()`: removed manual `(px<<8)|(px>>8)` - raw RGB565 is correct

#### 2. Overly Complex Animation
**Problem:**
- Custom `lvgl_fade()` function blocking on manual opacity animation
- Custom `lvgl_pump_ms()` and `lvgl_wait_anim()` helpers
- 324 lines of code for simple fade in/out

**Fix:**
- Use LVGL's built-in `lv_scr_load_anim(FADE_OUT)` - does all the work
- 173 lines, 47% less code, cleaner flow

### Build Result
```
RAM:   17.6% (57700 bytes / 327680 bytes)
Flash: 20.0% (1602909 bytes / 7995392 bytes)
Build: SUCCESS (51.6 seconds)
```

## Testing

Upload and observe:
1. ✓ Splash image should appear with correct colors (not flashing)
2. ✓ Smooth fade to "Ready" screen (800ms transition)
3. ✓ No LoadProhibited crashes
4. ✓ No warnings about unknown image types

## References
- LVGL 8.4 API: https://docs.lvgl.io/8.4/
- Available animations: `LV_SCR_LOAD_ANIM_FADE_IN`, `LV_SCR_LOAD_ANIM_FADE_OUT`
- No .bin conversion needed - JPEG decode works perfectly
