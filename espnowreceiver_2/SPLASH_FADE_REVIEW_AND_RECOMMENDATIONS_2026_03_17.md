# Splash Fade In/Out Review and Recommendations

**Date:** March 17, 2026  
**Project:** `espnowreceiver_2`  
**Scope:** Review fade-in/fade-out behavior for splash sequence, identify jump causes, and propose improvements.

---

## 1) What was reviewed

Primary files inspected:

- `src/config/littlefs_init.cpp`
- `src/display/display_splash_lvgl.cpp`
- `src/display/display_splash.cpp`
- `src/display/layout/display_layout_spec.h`
- `src/display/tft_impl/tft_display.cpp`
- `src/hal/display/lvgl_driver.cpp`

---

## 2) Current active behavior (important)

### Backend routing

In `src/config/littlefs_init.cpp`:

- If `USE_LVGL` is enabled, startup calls `Display::display_splash_lvgl()` directly.
- If `USE_LVGL` is not enabled, startup uses `displaySplashWithFade()` from the TFT path.

This means your observed fade jump is most likely in the LVGL splash path (if you are building LVGL variant).

---

## 3) Findings: why the fade appears to jump

## Finding A — Intentional pauses around animation boundaries create visible discontinuities

In `src/display/display_splash_lvgl.cpp`, there are explicit `smart_delay(300)` pauses before both fade-in and fade-out, plus additional 500 ms pauses after transitions for debug observation.

**Effect:**
- End of fade-in can feel like a “snap/step then hold”.
- Start of fade-out can feel abrupt due to pre-fade pause and then immediate opacity movement.

---

## Finding B — Linear opacity progression is not perceptually linear

The animation uses `lv_anim_set_values(..., LV_OPA_0, LV_OPA_COVER)` and reverse for fade-out with default linear progression.

**Effect:**
- Human visual response is nonlinear.
- Near ends (close to full opaque / close to transparent), equal opacity steps are perceived as uneven, often seen as endpoint jump.

---

## Finding C — Frame cadence is driven by loop + delay, not strict frame scheduling

The fade waits with a loop using `lv_anim_count_running() > 0`, calling `lv_timer_handler()` with `smart_delay(10)`.

**Effect:**
- Potential timing jitter from task scheduling/logging load.
- Per-frame intervals are not guaranteed consistent under system load.

---

## Finding D — Debug logging and debug-stop style sequencing add runtime disturbance

The splash code still includes extensive debug flow and logging around animation phases and callback steps.

**Effect:**
- Extra runtime overhead and occasional frame pacing inconsistency.
- Visual smoothness suffers, especially around transitions.

---

## Finding E — Test-image mode is currently active in LVGL splash path

The LVGL splash code currently uses `create_test_image()` path in `display_splash_lvgl.cpp`.

**Effect:**
- This is good for diagnostics, but not ideal for production fade tuning.
- Real splash JPEG path timing/composition behavior may differ slightly from test pattern.

---

## 4) What is **not** the likely root cause

The TFT implementation (`src/display/tft_impl/tft_display.cpp`) already uses improved integer interpolation for backlight animation and comments explicitly addressing end-segment jump behavior.

So if you are running LVGL build, the TFT fade logic is unlikely to be the source of the jump you are seeing.

---

## 5) Recommended fixes (ordered)

## Option 1 — Fast stabilization (minimal code churn)

1. Remove/disable debug pauses around fade boundaries in LVGL splash (`smart_delay(300)`/`smart_delay(500)` debug holds).
2. Use easing path for both fade-in/fade-out (`ease-in-out`) instead of strictly linear.
3. Reduce logging inside animation execution callback.
4. Keep a stable frame pump cadence near ~16 ms where possible.

**Expected result:** noticeably smoother boundary behavior with minimal redesign.

---

## Option 2 — Better LVGL visual method: fade a black overlay, not image opacity

Instead of fading image opacity directly:

- Keep splash image fixed at full opacity.
- Add full-screen black overlay object on top.
- Fade overlay opacity:
  - Fade-in effect: overlay from 255 -> 0
  - Fade-out effect: overlay from 0 -> 255

**Why this is better:**
- More visually stable at endpoints.
- Avoids some perceived stepping artifacts from image alpha blending near full opacity.
- Easier to tune visually with less interaction from image content.

---

## Option 3 — Best overall smoothness: hardware backlight fade (gamma-corrected)

Use splash content static, and perform fade using PWM backlight only:

- Animate logical brightness in LVGL (or timer task).
- Convert logical brightness to PWM duty using gamma mapping (e.g. $\gamma \approx 2.0$ to $2.4$):

$$
\text{duty} = \left(\frac{b}{255}\right)^\gamma \cdot \text{PWM\_MAX}
$$

- Write mapped duty in `LvglDriver::set_backlight()`.

**Why this is strongest:**
- Smoothest perceived brightness transition.
- Lowest pixel blending artifact risk.
- Very robust for splash and all future fades.

---

## 6) Suggested implementation plan

### Phase 1 (quick win, 30–60 min)
- Remove debug delay stops around fade transitions.
- Add ease-in-out path for fade animations.
- Reduce per-step debug logging.

### Phase 2 (quality upgrade, 1–2 hrs)
- Switch to overlay fade method for LVGL splash.
- Validate no endpoint jumps at fade boundaries.

### Phase 3 (premium smoothness, 2–4 hrs)
- Introduce gamma-corrected backlight fading in `LvglDriver::set_backlight()`.
- Optionally increase effective PWM precision behavior via mapping/dithering strategy.

---

## 7) Recommendation

For your stated issue (jump at end fade-in and start fade-out), I recommend:

1) **Immediately apply Option 1**, then  
2) **Move to Option 2** for stable LVGL visual fades, and  
3) If you want the best possible effect, **adopt Option 3** (gamma-corrected hardware backlight fade).

If you want, I can implement Option 1 + Option 2 directly in code in the next step, with a compile/test pass.
