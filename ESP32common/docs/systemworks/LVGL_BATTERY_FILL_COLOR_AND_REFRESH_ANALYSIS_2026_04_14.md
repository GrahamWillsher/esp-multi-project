# LVGL Battery Fill Color and Refresh Analysis (espnowreceiver_LCD)

Date: 2026-04-14  
Project: `espnowreceiver_LCD`  
Branch: `feature/battery-emulator-migration`

## Scope of this review
User-reported issues:
1. Battery fill color is wrong (e.g., ~67–69% appears red/purple instead of yellow/green-ish).
2. Need review of refresh architecture (whether objects should refresh independently, and whether FreeRTOS tasks should be used).

---

## Findings

## 1) Color pipeline is currently ambiguous and fragile
In `src/ui/runtime/ui_backend_lvgl.cpp` there are **multiple SOC color paths**:
- `soc_color(float)` -> returns RGB565 (`uint16_t`)
- `soc_lv_color(float)` -> returns `lv_color_t` via `lv_color_make`
- `soc_text_compatible_lv_color(float)` -> converts `soc_color()` via `rgb565_to_hex()` then `lv_color_hex()`

This means the code can render with subtly different quantization/conversion behavior depending on which helper is used.

### Why this matters
A single, authoritative color path is required. Multiple conversion paths increase risk of hue drift and make debugging difficult.

---

## 2) Current SOC gradient implementation is mathematically Red -> Yellow -> Green
The current formula in `soc_color()` is:
- 0–50%: `(R=255, G=0..255, B=0)`
- 50–100%: `(R=255..0, G=255, B=0)`

This is a clean red→yellow→green ramp. If rendered correctly, 67–69% should **not** appear red/purple.

### Implication
If displayed color is red/purple at ~68%, the defect is likely in **render path/style composition**, not in SOC percentage generation itself.

---

## 3) SOC data source appears correct
`src/main.cpp` feeds:
- `UI::Runtime::set_state(demo_model.state().soc_percent, demo_model.state().power_w);`

`src/app/demo_model.cpp` keeps SOC in ~20..95 range.

So SOC input value itself does not look wrong for this symptom.

---

## 4) Battery fill style may still be affected by style stacking behavior
Battery fill object is configured repeatedly, but LVGL style state can still be influenced by:
- inherited/default style properties not fully normalized,
- blend/opa interactions,
- gradient state not fully reset at runtime,
- multiple helper functions writing the same style properties from different code paths.

Given user-observed hue jumps (red -> purple) around close SOC values, this is consistent with style/render path inconsistency, not with a monotonic SOC gradient formula.

---

## 5) Refresh architecture review: current model is fundamentally correct
Current approach:
- one UI thread of execution (`loop()` -> `UI::Runtime::tick()`)
- periodic `lv_tick_inc(delta)`
- `lv_timer_handler()` called in same context
- widgets updated as objects, not full-screen manual redraw

This is the right baseline for LVGL.

### About FreeRTOS tasks
Using separate FreeRTOS tasks for independent widget refresh is **not recommended** unless carefully serialized. LVGL is not thread-safe by default. Multiple tasks updating LVGL objects can create flicker/race/corruption unless protected by a strict single-owner GUI task and message queue.

---

## Recommendations

## A) Color rendering hardening (highest priority)
1. Keep exactly **one** SOC color function for all SOC visuals.
   - Preferred: one function returning `lv_color_t` directly.
2. Remove unused parallel paths (`soc_color`, `soc_lv_color`, `soc_text_compatible_lv_color` duplicates).
3. On battery fill object, force full style normalization once at creation:
   - no gradient, no recolor, no image recolor, no blend surprises,
   - explicit `bg_opa = LV_OPA_COVER`.
4. Apply fill color only from the single authoritative function.

## B) Add quick instrumentation for proof
For each SOC change (debug build), log:
- SOC value
- computed R/G/B
- final `lv_color_t` channel values (`LV_COLOR_GET_R/G/B`)

This will prove whether wrong color is produced before draw or during draw.

## C) Refresh/task architecture
1. Keep LVGL updates in **one GUI owner context** (current loop is acceptable).
2. If migrating to FreeRTOS:
   - create one dedicated GUI task,
   - feed state via queue/event group,
   - only GUI task touches LVGL objects.
3. Do not create per-widget tasks that call LVGL APIs directly.

## D) Redraw behavior and flicker
LVGL already refreshes invalidated regions. Do not force full-screen redraws.
If needed:
- continue with larger draw buffer in PSRAM,
- keep widget updates minimal and value-change driven,
- avoid unnecessary object re-alignment each tick.

---

## Practical implementation suggestion (next step)
Implement a strict `soc_to_lv_color(float)` with explicit zone anchors:
- 0%: red
- 50%: amber/yellow
- 100%: green

Then use that exact function for:
- battery fill color,
- SOC text color,
- any SOC-dependent accents.

This guarantees visual consistency and removes conversion ambiguity.

---

## Conclusion
- Repositioning improvement is valid and aligned with requested layout.
- Persistent wrong battery color is most likely due to **color/style pipeline inconsistency**, not SOC math.
- Current refresh architecture (single LVGL owner path) is conceptually correct; introducing multiple FreeRTOS UI tasks would likely make things worse unless strictly serialized through one GUI task.

This review recommends a focused cleanup to one color function + style normalization + debug instrumentation as the fastest reliable fix path.
