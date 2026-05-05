# Power Bar LVGL Enhancement Ideas (2026-04-20)

## Scope
This review examines the current LCD runtime power-bar implementation and proposes three higher-impact visual/UX upgrades that make better use of LVGL’s rendering, animation, and style pipeline.

Primary implementation reviewed:
- `espnowreceiver_LCD/src/ui/runtime/ui_backend_lvgl.cpp`

---

## Current Power-Bar Design (what exists now)

### What it does well
- Clean bidirectional concept: left side for charging, right side for discharging.
- Clear center marker at near-zero power.
- Segment count scales linearly with absolute power.
- Basic ripple feedback exists for value changes in same direction.

### Current technical behavior
- 30 segments per side (`BAR_SEGMENTS_PER_SIDE = 30`), rectangular bars with radius = 0.
- Bars are separate LVGL objects, hidden/shown every refresh cycle for current state.
- For ripple animation, bar opacity is modulated by repeatedly redrawing all active segments.
- Direction color ramps are simple RGB blends (charging: cyan→green, discharging: blue→red).

### Practical limitations
1. **Visual style is very hard-edged** (square bars, no depth, no smoothing).
2. **Frequent object-level hide/show churn** can create unnecessary invalidation work.
3. **No peak-memory behavior**: operator cannot quickly see recent maximum power excursion.
4. **Linear watt scaling only** can feel less expressive for low-mid range changes.

---

## Three Upgrade Concepts

## 1) “Soft Capsule Segments” with style transitions and glow

### Idea
Keep segmented bars, but convert each segment into a rounded capsule with subtle gradient and soft opacity transitions. Add a low-alpha glow layer for active segments.

### LVGL features to leverage
- `lv_obj_set_style_radius(..., LV_RADIUS_CIRCLE, ...)`
- `lv_obj_set_style_bg_grad_color`, `lv_obj_set_style_bg_grad_dir`
- Style transitions (`lv_style_transition_dsc_t`) for smooth active/inactive fades
- Optional shadow/glow via small `shadow_width` and low `shadow_opa`

### Why it improves things
- Immediate visual polish with minimal architecture change.
- Better perceived smoothness without high animation complexity.
- Uses LVGL style engine rather than repeated manual state flips.

### Suggested implementation notes
- Keep existing geometry and object arrays.
- Introduce two style presets: `segment_inactive_style`, `segment_active_style`.
- Toggle style states instead of hide/show where possible.
- Use slightly lower contrast inactive rail (e.g., dark gray with ~30–40% opacity).
- Apply 120–180 ms transition for color/opa to avoid “strobing” behavior.

### Estimated effort/risk
- **Effort:** low (0.5–1.5 days)
- **Risk:** low

---

## 2) Linear dB-style bar with peak-hold marker (requested behavior)

### Idea
Replace current “segment count only” emphasis with a ballistic meter model:
- Fast attack when power rises.
- Peak marker pushed to latest max.
- Hold peak for ~1 second.
- Controlled fade/decay back down.

This matches familiar pro-audio/VU meter behavior and gives much stronger operator feedback.

### LVGL features to leverage
- `lv_bar` as base continuous meter (or keep segmented rail + derived fill width)
- `lv_anim_t` for attack/decay easing
- Separate thin peak marker object (`lv_obj_t`) moving along rail
- Optional color zones (green/yellow/red) via gradients or layered objects

### Suggested meter math
Treat absolute power as normalized amplitude:

- $x = \text{clamp}(|P| / P_{max}, 0, 1)$
- Optional dB mapping for perceptual spread:
  $$dB = 20\log_{10}(\max(x, \epsilon))$$
  then remap to 0..1 for display window (e.g., -40 dB to 0 dB).

Ballistics:
- **Attack:** near-instant (50–120 ms)
- **Peak hold:** 1000 ms (as requested)
- **Release:** exponential or two-stage linear (e.g., fast to 70%, slower below)

### Why it improves things
- Adds memory of recent extremes (critical for transient events).
- Looks alive and professional.
- More informative than instantaneous segment count alone.

### Suggested implementation notes
- Add state variables:
  - `display_level`
  - `peak_level`
  - `peak_hold_until_ms`
- Update on each UI tick:
  1. Apply attack/release to `display_level` toward input level.
  2. If new level exceeds `peak_level`, set peak and `hold_until = now + 1000`.
  3. After hold, decay `peak_level` toward `display_level`.
- Keep left/right direction semantics by using mirrored bars or sign-based placement.

### Estimated effort/risk
- **Effort:** medium (1.5–3 days)
- **Risk:** low-medium (tuning constants for “feel”)

---

## 3) Hybrid “rail + transient trail” meter using canvas/layer effects

### Idea
Build a more modern instrument look:
- A stable base rail indicates current level.
- A short trailing “comet tail” (fading alpha) visualizes movement speed and direction.
- Optional micro-grid and threshold markers for context.

### LVGL features to leverage
- `lv_canvas` or custom draw events for lightweight trail rendering
- Layered composition: base rail object + overlay trail object + peak marker
- Palette interpolation and alpha blending for motion persistence

### Why it improves things
- Encodes both **value** and **velocity of change**.
- Strong visual identity with minimal extra text.
- Better use of LVGL’s draw stack than static object toggling.

### Suggested implementation notes
- Keep the core meter as one or two objects (for direction split).
- Draw trail in a fixed-width offscreen buffer and decay alpha each frame.
- Invalidate only the meter strip region (not whole screen).
- Clamp frame update budget to current ~30 fps path.

### Estimated effort/risk
- **Effort:** medium-high (2–4 days)
- **Risk:** medium (careful tuning needed to avoid overdraw cost)

---

## Recommended path

If you want best impact for least risk:
1. **Phase 1 (quick win):** Soft Capsule Segments (Concept 1)
2. **Phase 2 (feature win):** Peak-Hold dB-style ballistic behavior (Concept 2)
3. **Phase 3 (signature UX):** Add transient trail overlay (Concept 3)

This sequence gives immediate visual improvement, then adds the requested “push-hold-fade” behavior, then introduces advanced LVGL effects once baseline behavior is proven stable.

---

## Acceptance criteria ideas

1. Corners softened and segment transitions are visibly smoother.
2. Peak marker holds at max excursion for 1000 ms ± 100 ms before release.
3. Release/fade is monotonic and visually smooth (no stepping artifacts).
4. UI remains stable at existing refresh cadence (target ~30 fps) with no obvious stutter.
5. Meter still clearly differentiates charge vs discharge direction.

---

## Notes for integration points

Most changes can be isolated inside these functions in `ui_backend_lvgl.cpp`:
- `apply_power_bars(...)`
- `update_power_state(...)`
- `animate_power_ripple(...)` (or replace with ballistic/peak animator)
- `set_bar_obj(...)` and `init_bar_geometry()` for shape/layout refinements

This keeps transport/network code untouched and limits risk to the display runtime layer.