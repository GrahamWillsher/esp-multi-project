# Receiver Splash Screen (TFT) Fade Rework Analysis — 2026-04-03

## Scope

This report executes the request in [esp32common/docs/systemworks/The receiver splash screen.md](esp32common/docs/systemworks/The%20receiver%20splash%20screen.md):

- analyze the current receiver splash fade issue
- investigate T-Display-S3 constraints/references
- propose a full TFT-only replacement approach
- define validation criteria

This document is **pre-code analysis only**.

---

## 1) Current TFT Implementation Findings

Primary implementation path:
- [espnowreceiver_2/src/display/tft_impl/tft_display.cpp](espnowreceiver_2/src/display/tft_impl/tft_display.cpp)

Timing contract source:
- [espnowreceiver_2/src/display/layout/display_layout_spec.h](espnowreceiver_2/src/display/layout/display_layout_spec.h)

Current fade sequence in `display_splash_with_fade()`:
1. backlight = 0
2. draw splash image
3. `animate_backlight(255, SPLASH_FADE_IN_MS)`
4. hold `SPLASH_HOLD_MS`
5. `animate_backlight(0, SPLASH_FADE_OUT_MS)`

### 1.1 Startup execution context and delay model (investigated)

Boot order confirms splash runs early, before receiver runtime tasks are created:

- `setup()` phase order in [espnowreceiver_2/src/main.cpp](espnowreceiver_2/src/main.cpp):
   - `display`
   - `filesystem` (calls `initlittlefs()`)
   - `tasks` (creates ESP-NOW worker, renderer, MQTT, LED, memory sampler)

- Splash call site in [espnowreceiver_2/src/config/littlefs_init.cpp](espnowreceiver_2/src/config/littlefs_init.cpp):
   - `displaySplashWithFade()` runs during `filesystem` phase.

Delay behavior is handled by `smart_delay()` in [espnowreceiver_2/src/helpers.cpp](espnowreceiver_2/src/helpers.cpp):

- If scheduler is running and task context exists: uses `vTaskDelay(...)`.
- Otherwise: falls back to Arduino `delay(...)`.

Practical conclusion for this splash path:

1. The splash runs before custom receiver tasks start, so there is no app-level contention yet.
2. Delay calls are still FreeRTOS-aware through `smart_delay()`.
3. Cadence smoothness should therefore be driven primarily by fade algorithm quality, not cross-task contention.

### Observed technical contributors to visible jump

1. **Step-quantized brightness path**
   - `animate_backlight()` increments/decrements by exactly 1 logical step.
   - PWM is 8-bit (`0..255`), so near-edge transitions can be visually coarse.

2. **Per-step delay rounding accumulates phase drift**
   - `step_delay_ms` is integer-rounded from `duration/transitions`.
   - This can bunch updates and produce perceptual non-uniformity near endpoints.

3. **Gamma + soft-knee + forced endpoint write creates discontinuity risk**
   - `set_backlight()` applies gamma mapping and top-end blend.
   - A final correction write (`if current != target`) can produce a visible last jump if previous frame is not perceptually adjacent.

4. **Temporal dithering state reset at boundaries**
   - `backlight_dither_error_` resets at min/max.
   - Boundary reset plus next-step mapping can produce the exact “end of fade-in / start of fade-out” artifact described.

5. **Step-delay pacing jitter despite low startup contention**
   - Fade loop uses per-step sleep (`smart_delay`) with integer step timing.
   - Even with minimal startup contention, quantized sleep pacing can still produce uneven perceptual cadence at endpoints.

---

## 2) T-Display-S3 Reference Findings (TFT relevant)

External references reviewed:
- LILYGO product page (T-Display-S3)
- LILYGO T-Display-S3 GitHub repository

Relevant findings:

1. **Panel/backlight control basics are consistent with current project**
   - ST7789 display path and board backlight control behavior align with this codebase’s GPIO usage.

2. **Display power enable sequencing matters**
   - Enabling panel power before active rendering/backlight transitions is required for stable startup behavior.

3. **Repository history indicates backlight-control method sensitivity**
   - Recent repo notes around removing invalid backlight control methods reinforce that LEDC control details can directly affect smoothness.

4. **TFT_eSPI setup correctness remains foundational**
   - Incorrect setup/profile selection can masquerade as rendering/fade defects.

---

## 3) Root-Cause Summary (Most Likely)

The fade artifact is most likely **not one single defect**, but the combined result of:

- integer-step fade scheduling,
- endpoint quantization behavior,
- gamma/knee remap interaction,
- dithering boundary reset,
- and blocking-loop cadence jitter.

This explains why the issue is visually strongest exactly at:
- end of fade-in,
- beginning of fade-out.

---

## 4) Proposed TFT-Only Replacement Design

## 4.1 Design intent

Create a **new separate fade function** dedicated to splash backlight transitions, while keeping existing duration constants unchanged.

Proposed new function (name suggestion):
- `animate_backlight_tft_smooth(uint8_t start, uint8_t target, uint32_t duration_ms)`

Keep existing timing constants exactly as-is:
- `SPLASH_FADE_IN_MS`
- `SPLASH_HOLD_MS`
- `SPLASH_FADE_OUT_MS`

## 4.2 Algorithm proposal (time-domain interpolation)

Instead of fixed per-step delay loops:

1. Capture `t0 = millis()` and `t_end = t0 + duration_ms`.
2. On each update tick, compute progress:
   - $p = \text{clamp}\left(\frac{t - t0}{duration_ms}, 0, 1\right)$
3. Compute logical brightness from continuous interpolation:
   - `logical = round(start + (target-start) * p)`
4. Apply existing gamma mapping path once per frame.
5. Write PWM only when value changes.
6. Force exact endpoint once at completion.

This keeps duration exact while making update pacing time-driven (not step-delay-driven).

## 4.2.1 Delay handling requirements for the new function

Because splash is an initial boot function, the new fade routine should explicitly enforce scheduler-safe timing behavior:

1. Use a fixed frame interval target (e.g. `ANIMATION_FRAME_TIME_MS`).
2. Use a monotonic-time loop (`millis()`) for interpolation and total-duration correctness.
3. Sleep between frames using `smart_delay(frame_ms)` so the routine remains valid both before and after runtime task startup.
4. Do not rely on `delay()` directly inside the new fade function.

This preserves the current requirement (“delays handled correctly via FreeRTOS-aware path”) while keeping startup compatibility.

## 4.3 Cadence policy

Use a stable frame cadence target (e.g., `ANIMATION_FRAME_TIME_MS`) to reduce visible pacing jitter.

Recommended cadence rule:

- frame interval target: `ANIMATION_FRAME_TIME_MS`
- end condition: elapsed-time based (not frame-count based)
- endpoint guarantee: single final write to exact target value

## 4.4 Endpoint smoothing rules

- Do not reset dithering/error accumulator abruptly at transition boundaries unless necessary.
- Ensure the last two perceptual writes are monotonic neighbors.
- Avoid extra corrective write unless mathematically required.

## 4.5 Integration plan

- Keep current `display_splash_with_fade()` sequence.
- Replace only fade execution calls with new function path.
- Preserve all durations and hold time.

---

## 5) Implementation Recommendations

1. Add new function in TFT backend only (separate from current `animate_backlight()`).
2. Keep legacy function available temporarily for A/B comparison during testing.
3. Leave image decode/draw path unchanged.
4. Keep hardware init order unchanged (power enable, TFT init, backlight control).

---

## 6) Validation Plan

## 6.1 Visual acceptance

Pass criteria:
- no visible jump at final 10% of fade-in,
- no visible jump at initial 10% of fade-out,
- no flash/glitch between splash draw and fade start.

## 6.2 Timing acceptance

- Measured fade-in duration matches configured `SPLASH_FADE_IN_MS`.
- Measured hold duration matches configured `SPLASH_HOLD_MS`.
- Measured fade-out duration matches configured `SPLASH_FADE_OUT_MS`.

## 6.3 Repeatability

- Test across minimum 10 consecutive cold boots.
- Confirm fade behavior remains smooth in startup phase before custom tasks begin.
- Optional secondary check after task startup to confirm no regression if fade helper is reused elsewhere.

## 6.5 Delay-path verification

Add log-level verification (temporary during development) to confirm:

1. splash function executes during `filesystem` phase,
2. `smart_delay()` is the only sleep primitive used by the new fade function,
3. configured fade durations match constants within tolerance.

## 6.4 Regression checks

- Ready-screen fade still behaves as expected.
- No regressions in backlight off/on boundary behavior.

---

## 7) Proposed Delivery Order

1. Implement `animate_backlight_tft_smooth()` (new function).
2. Wire splash sequence to use new function.
3. Keep legacy function for one test cycle.
4. Run validation matrix.
5. Remove/retain legacy path per result.

---

## 8) Investigation: Should fade duration be aligned to 255 step count?

Question investigated:

- Since backlight range is `0..255`, should fade duration be chosen to divide cleanly by 255 to simplify per-step timing and reduce artifacts?

## 8.1 Numerical analysis

For a 1-step-per-level fade, there are 255 transitions.

With current policy:

- fade duration = `3000 ms`
- per-step ideal delay = $3000/255 = 11.7647\,ms$

This is not an integer millisecond delay.

Nearest integer-delay durations are:

- `11 ms` × 255 = `2805 ms` (too fast, \(-6.5\%\))
- `12 ms` × 255 = `3060 ms` (slower, \(+2.0\%\))

So yes: if using strict integer per-step delay, a “clean” duration would be around `3060 ms`.

## 8.2 Practical impact

1. **If keeping old step-delay algorithm**
   - choosing `3060 ms` reduces timing quantization complexity,
   - but it does **not** fully solve endpoint visual artifacts (gamma/dither/boundary effects still apply).

2. **If using proposed time-domain interpolation algorithm**
   - exact divisibility by 255 is no longer required,
   - `3000 ms` can be preserved exactly,
   - visual smoothness is improved by continuous progress mapping rather than fixed step sleeps.

## 8.3 Recommendation

Primary recommendation:

1. **Keep fade policy at 3000 ms** (as currently required), and
2. **implement the new time-based fade function** so step divisibility is irrelevant.

Reason:

- This preserves user-visible timing policy,
- avoids artificial time drift,
- and addresses the real smoothness issue at boundaries.

Secondary (optional) recommendation if timing policy is later allowed to change:

- Use `3060 ms` for mathematically clean 12 ms per-step legacy stepping,
- but only as a fallback for legacy algorithm mode, not as the main fix path.

## 8.4 Final position for this project

Given the project constraints already set:

- do **not** retune fade duration just to match step count,
- keep `SPLASH_FADE_IN_MS` / `SPLASH_FADE_OUT_MS` unchanged,
- solve smoothness via the new TFT fade implementation.

## 8.5 Project decision (confirmed)

Decision:

- We will proceed with **Option 2 (cleaner engineering approach)**:
   - keep existing fade timings,
   - implement the new time-domain TFT fade function,
   - avoid duration retuning purely to fit 255 step divisibility.

---

## Conclusion

The current TFT splash fade artifacts are consistent with endpoint quantization and cadence behavior in the existing step-delay loop.

A **new, separate, time-interpolated TFT fade function** is the correct fix path and satisfies the requirement to keep timing constants unchanged while replacing only fade mechanics.