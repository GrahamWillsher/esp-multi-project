# espnowreceiver_LCD – Complete Code Review (2026-04-14)

## Scope Reviewed
Static review of `C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD` including:
- build/config: `platformio.ini`, `include/*.h`
- app/runtime: `src/main.cpp`, `src/app/*`, `src/ui/*`, `src/ui/runtime/*`, `src/hal/*`
- both backends (`LVGL` and `non-LVGL`) and startup/splash path

Build health: no current compile/lint errors reported.

---

## Progress Update (Implemented on 2026-04-14)

Status: **LVGL-only runtime path is now active and validated.**

### Completed in code
1. **LVGL made the default and only active build path**
   - `platformio.ini` now defaults to `waveshare_esp32s3_lcd7_lvgl`.
   - Legacy non-LVGL source files have been physically removed from the repository.

2. **SOC debug instrumentation removed from production path**
   - Removed debug-only SOC tracing path from `ui_backend_lvgl.cpp`.
   - `soc_pipeline_color()` is now a single production implementation.

3. **Power bar redraw efficiency fix applied**
   - Added change-gating so bars are redrawn only when bar state changes (or first initialization), rather than every tick.
   - This reduces unnecessary invalidation and helps with intermittent flicker.

4. **Unused non-LVGL constants removed**
   - Removed stale constants from `include/app_config.h` that were tied to the old non-LVGL renderer.

5. **LVGL config warning fixed and config trimmed**
   - Fixed the recurring LVGL `lv_conf.h` warning by adding the standard `LV_CONF_H` sentinel wrapper to `include/lv_conf.h`.
   - Trimmed unused LVGL config options so the project defaults better match the actual runtime UI usage.
   - Disabled unused items including extra theme variants, unused widget toggles, and example builds.

6. **Validation performed**
   - Full build completed successfully after cleanup (`pio run -j 2`).
   - Full build also completed successfully after the `lv_conf.h` warning fix and config-trimming pass.

### Remaining optional cleanup
- Optional future pass: reduce bring-up logging in `main.cpp`.
- Optional future pass: optimize splash backlight fade implementation.
- Optional future pass: trim further unused LVGL features if binary size becomes a priority.

---

## Executive Summary
The codebase is functional and well-structured for a small embedded UI project. Main improvements are about **cleanup**, **avoiding redundant redraws**, and **reducing technical debt from migration/debugging**.

### Top priorities
1. **Remove temporary SOC debug pipeline code** from LVGL backend.
2. **Stop unconditional power-bar redraw every tick** (likely contributor to intermittent flicker and unnecessary work).
3. **Decide LVGL-only vs dual-backend strategy** and remove/segregate unused code accordingly.
4. **Trim unused config constants** and old migration leftovers.

---

## Findings & Recommendations

## 1) Debug instrumentation left in production path (High)
**File:** `src/ui/runtime/ui_backend_lvgl.cpp`

### Findings
The SOC color troubleshooting instrumentation is still present:
- `struct SocColorDebug`
- `compute_soc_color_debug()`
- `rgb565_swapped`, `hex_swapped`, and debug-only fields
- `Serial.printf("SOC_DEBUG ...")` in `update_soc()`

This adds noise, maintenance overhead, and avoidable runtime cost.

### Recommendation
Remove all debug-only SOC trace code and keep a minimal production function:
- one `soc_pipeline_color(float)` implementation returning final `lv_color_t`
- no serial logging in normal runtime path

### Redundant code to remove
- `SocColorDebug` struct
- `compute_soc_color_debug()` function
- `SOC_DEBUG` `Serial.printf(...)`
- debug-only swapped/hex fields and related conversions

---

## 2) Power bars are redrawn even when state has not changed (High)
**File:** `src/ui/runtime/ui_backend_lvgl.cpp`

### Findings
`update_power_state()` currently calls `apply_power_bars(...)` whenever ripple is not active, even if power value and bar state are unchanged. `apply_power_bars()` hides all bars and re-shows active ones, causing repeated style/object invalidations.

This is not functionally wrong, but it is inefficient and can contribute to visible flicker under load.

### Recommendation
Add change gating for bar redraws:
- redraw bars only when one of these changes: `current_bar_count_`, direction, zero/non-zero mode, or ripple frame index
- keep label update logic separate (already mostly change-gated)

Expected outcome: lower render churn and smoother UI.

---

## 3) Config drift after LVGL migration (Medium)
**Files:**
- `include/app_config.h`
- `platformio.ini`

### Findings
Some constants in `app_config.h` are not used by current LVGL path:
- `LED_MODE_ROTATE_MS`
- `LED_COLOR_ROTATE_MS`
- `BAR_SEGMENT_W`
- `BAR_SEGMENT_GAP`
- `FRAME_INTERVAL_MS` (used by non-LVGL backend only)

Also, `platformio.ini` default env is currently non-LVGL, while active development appears LVGL-focused.

### Recommendation
If LVGL is now canonical:
- remove or move non-LVGL-only constants behind backend-specific config blocks
- set `default_envs = waveshare_esp32s3_lcd7_lvgl` to reduce accidental wrong builds

If dual-backend support is required, keep both but split config by backend to avoid ambiguity.

### Redundant code candidates (if LVGL-only)
- non-LVGL-only constants in `app_config.h`

---

## 4) Legacy non-LVGL rendering path still present (Strategic cleanup)
**Files:**
- `src/ui/runtime/ui_backend_non_lvgl.cpp`
- `src/ui/soc_widget.*`
- `src/ui/power_bar_widget.*`
- `src/ui/led_widget.*`

### Findings
The legacy renderer remains alongside LVGL backend. This is fine for fallback, but it duplicates rendering logic and increases maintenance surface.

### Recommendation
Pick one strategy:

1. **LVGL-only product path** (recommended if migration is complete):
   - remove legacy widgets/backend files
   - simplify constants and tests to LVGL path only

2. **Dual-backend intentionally supported**:
   - move legacy code into clearly named `legacy_non_lvgl/`
   - enforce compile guards and backend-specific docs/tests

### Redundant code to remove (if LVGL-only)
- `ui_backend_non_lvgl.cpp`
- `soc_widget.*`, `power_bar_widget.*`, `led_widget.*`
- related non-LVGL-only config constants

---

## 5) Startup sequence is very blocking and I2C-heavy (Medium)
**Files:**
- `src/ui/runtime/splash_sequence.cpp`
- `src/hal/lgfx_waveshare_7.h`

### Findings
Splash fade uses software PWM by repeatedly toggling backlight over I2C in tight loops with `delayMicroseconds`. This works, but blocks CPU during splash and generates many I2C transactions.

### Recommendation
- Keep simple behavior for now if startup UX is acceptable.
- For cleaner architecture, prefer either:
  - fixed fade steps at lower rate (fewer I2C writes), or
  - hardware PWM-capable brightness control path (if hardware supports).

Not urgent, but a good cleanup/perf improvement.

---

## 6) Logging policy should be tightened (Low-Medium)
**File:** `src/main.cpp`

### Findings
Runtime prints to both `Serial` and `Serial0` every second, plus boot logs.
Good for bring-up, but noisy for production and can affect timing on constrained systems.

### Recommendation
Introduce log levels/macros (`DEBUG`, `INFO`, `ERROR`) and compile-time suppression for production.

### Redundant code candidate
- Duplicate always-on mirror logging to both serial ports (keep optional via macro)

---

## 7) Minor architecture hygiene opportunities (Low)

### Findings / Suggestions
- `display = new lgfx_custom::LGFX_Waveshare7();` in `main.cpp` can be static object to avoid heap dependency in startup path.
- Consider centralizing duplicated gradient math to shared helpers to avoid drift between backends.
- Add small unit checks for color mapping and bar index math to protect future refactors.

---

## Redundant Code Removal Checklist (Actionable)

## Immediate (safe)
1. Remove LVGL SOC debug instrumentation:
   - `SocColorDebug`
   - `compute_soc_color_debug()`
   - `SOC_DEBUG` serial trace
2. Remove/guard duplicated bring-up logs in `main.cpp` for release builds.
3. Remove clearly unused constants from `app_config.h` (or move to non-LVGL section).

## Conditional (depends on product direction)
4. If LVGL-only going forward, remove legacy non-LVGL backend + legacy widgets.
5. If dual backend remains, move legacy path to dedicated folder + explicit backend docs.

---

## Suggested Cleanup Sequence
1. **Pass 1 (no behavior change):** remove debug instrumentation + unused constants + log macros.
2. **Pass 2 (efficiency):** add redraw gating for power bars.
3. **Pass 3 (structure):** decide and execute LVGL-only vs dual-backend cleanup.
4. **Pass 4 (optional):** optimize splash brightness control path.

---

## Final Assessment
The codebase is close to clean. The main blockers are migration residue and redundant redraw work, not core architectural flaws. With the above cleanup passes, this project can be made materially leaner, easier to maintain, and less prone to UI flicker/perf regressions.

---

## Current Status Snapshot
- **Pass 1 (debug + unused constants):** ✅ completed
- **Pass 2 (power bar redraw gating):** ✅ completed
- **Pass 3 (LVGL-only runtime path):** ✅ completed (active build path and legacy files deleted)
- **Pass 4 (LVGL config hardening):** ✅ completed (`lv_conf.h` warning fixed, unused config toggles reduced, build validated)
- **Pass 5 (optional splash optimization):** ⏳ pending (not required for functional LVGL-only transition)
