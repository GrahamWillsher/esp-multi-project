# Splash Fade Review (TFT-only build)

**Date:** March 17, 2026  
**Project:** `espnowreceiver_2`  
**Focus:** Verify whether previous fade findings are relevant when running TFT build only.

---

## Executive answer

**Mostly no:** the prior LVGL-specific findings are not the primary path for your current TFT build.

For `receiver_tft`, LVGL splash files are excluded by build filter, and the active fade behavior comes from the TFT display implementation.

---

## What is active in TFT build

### Build config confirms LVGL splash is excluded

In [espnowreceiver_2/platformio.ini](espnowreceiver_2/platformio.ini), environment `receiver_tft` excludes:

- `display/display_splash_lvgl.cpp`
- `hal/display/lvgl_driver.cpp`
- `display/lvgl_impl/*`
- `display/display_splash.cpp` (legacy splash implementation)

So your current path is not the LVGL splash animation flow.

### Active runtime path

- `initlittlefs()` in [src/config/littlefs_init.cpp](espnowreceiver_2/src/config/littlefs_init.cpp) calls `displaySplashWithFade()` for non-LVGL.
- The linked `displaySplashWithFade()` is from [src/display/display.cpp](espnowreceiver_2/src/display/display.cpp), which dispatches via `Display::g_display`.
- Under `USE_TFT`, `Display::g_display` is `TftDisplay`.
- Actual fade logic is in [src/display/tft_impl/tft_display.cpp](espnowreceiver_2/src/display/tft_impl/tft_display.cpp), method `animate_backlight()`.

---

## TFT-specific findings for the visible jump

## Finding 1: Linear PWM duty is not perceptually linear

Current code interpolates brightness linearly in duty space (`0..255`). Human perception is nonlinear.

**Impact:** transition can feel uneven near end of fade-in and start of fade-out, even if math is correct.

---

## Finding 2: 8-bit PWM quantization can expose step edges

Backlight uses 8-bit PWM (`0..255`) with 2 kHz in [src/hal/hardware_config.h](espnowreceiver_2/src/hal/hardware_config.h).

**Impact:** coarse step granularity can produce visible stepping depending on panel/backlight driver characteristics.

---

## Finding 3: Frame cadence is approximately 16 ms but quantized by scheduler tick

`animate_backlight()` uses frame delay from layout timing constants (~16 ms). On FreeRTOS timing granularity, this can produce small cadence irregularities.

**Impact:** tiny timing unevenness can make endpoint transitions feel less smooth.

---

## Finding 4: Existing interpolation is already better than old float-step approach

The active TFT method uses integer interpolation and stable start/end capture in `animate_backlight()`.

**Impact:** this already fixes many classic end-segment jump problems. Remaining artifact is likely perceptual/PWM-related rather than obvious arithmetic bug.

---

## Is “number of steps” the core issue?

**Not primarily.**  
Step count matters, but with current ~60 FPS logic over multi-second fades, the dominant issue is usually:

1) perceptual nonlinearity, and/or  
2) PWM quantization/driver response.

---

## Recommended improvements (TFT path)

## Option A (best immediate): gamma-corrected backlight mapping

Keep current interpolation, but map logical brightness `b` to PWM duty via gamma:

$$
\text{duty} = \left(\frac{b}{255}\right)^\gamma \cdot 255
$$

Start with $\gamma \in [2.0, 2.4]$ and tune visually.

**Why:** usually removes endpoint “jump feel” most effectively on TFT/backlight fades.

---

## Option B: use ease-in-out curve in logical brightness progression

Instead of purely linear `progress`, use smoothstep/ease-in-out before mapping to duty.

**Why:** slows movement near boundaries, reducing perceived abruptness at fade-in end and fade-out start.

---

## Option C: increase effective PWM resolution behavior

If hardware/SDK path allows practical 10-bit or 12-bit backlight control without side effects, increase duty granularity.

**Why:** smaller brightness increments reduce visible stepping.

---

## Option D: optional temporal dithering near boundaries

Alternate adjacent duty levels across frames (e.g., 252/253) for fractional effective brightness.

**Why:** can smooth quantization artifacts without major architecture changes.

---

## Recommended implementation order

1. Add gamma mapping to `set_backlight()` in TFT path.  
2. Add ease-in-out to `animate_backlight()` progress.  
3. Re-test visually before changing PWM bit depth.

This is low-risk and likely to resolve the jump you described.

---

## Conclusion

- Previous LVGL-focused report is **not the active-path diagnosis** for your current TFT-only build.
- Your symptom is still real and likely due to **perceptual + PWM characteristics** in the active TFT backlight fade path.
- Best fix: **gamma-corrected + eased backlight animation** in [src/display/tft_impl/tft_display.cpp](espnowreceiver_2/src/display/tft_impl/tft_display.cpp).
