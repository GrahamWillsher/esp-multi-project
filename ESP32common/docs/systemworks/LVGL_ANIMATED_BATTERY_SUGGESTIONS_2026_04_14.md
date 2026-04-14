# LVGL Animated Battery Concepts for SOC Display

Date: 2026-04-14  
Target: Waveshare ESP32-S3 LCD (800x480), LVGL 8.4

## Goal
Provide three practical battery UI concepts that are easy to animate in LVGL and map directly to SOC (%).

---

## Suggestion 1 — Vertical 2-Terminal Battery (Classic EV Style)

### Concept
A tall battery outline centered on screen, with a total battery width of **50% of display width** and two top terminals that are **physically connected** to the body. Fill rises from bottom to top based on SOC.

### Diagram (not to scale)

```text
        +----+  +----+
        | T1 |  | T2 |
     .--+----+--+----+--.
     |                  |
     |      74.5%       |  <- physical SOC text (white)
     |                  |
     |       FILL       |  <- dynamic filled area (bottom-up)
     |                  |
     '------------------'
       (centered)
     width = 50% of display
```

### Animation ideas
- Fill level tweened smoothly when SOC updates (e.g., 600–900 ms).
- Optional subtle shine band moving upward every 2–3 seconds.
- Use the **same SOC color gradient currently used by the SOC text** so color semantics stay consistent across the UI.
- Keep in-battery SOC text white at all times for readability over changing fill colors.

### LVGL implementation notes
- Create one parent battery group object centered horizontally.
- Set battery body width to `SCREEN_WIDTH / 2` and height to ~45–60% of screen height.
- Build terminals as child objects attached to the top edge of the body (small overlap or connector bridge) so they are visually continuous.
- `lv_obj_create()` for outline container with border only.
- `lv_obj_create()` child for fill object, anchored to bottom.
- On SOC update: change fill height to `inner_h * soc / 100` with `lv_anim_t`.
- Apply fill color from the same SOC color function used by the text (single shared color-mapping helper).
- Add a centered label inside the battery body for physical SOC (e.g., `74.5%`) in white, large font, and with optional dark shadow/outline for contrast.

### Suggested dimensions (800x480 target)
- Battery body width: 400 px (50% of 800)
- Battery body height: 250–300 px
- Terminal width: each 56–72 px
- Terminal height: 20–28 px
- Gap between terminals: 20–32 px
- SOC text font: Montserrat 48–64 (white)

### Why this is good
- Familiar battery metaphor.
- Very readable from distance.
- Easy to style for warning/normal states.
- Direct color consistency with existing SOC text logic reduces user confusion.

---

## Suggestion 2 — Horizontal Single-Cell Battery (Simple and Robust)

### Concept
A wide horizontal battery with one terminal pin at the right. Fill grows left-to-right.

### Diagram (not to scale)

```text
  .--------------------------------------.----.
  |##########################            | PIN |
  |##########################            |-----'
  '--------------------------------------'
   ^ fill increases left-to-right with SOC
```

### Animation ideas
- Main fill interpolation on SOC change (300–700 ms).
- Add striped moving overlay only while charging.
- Brief pulse glow when crossing thresholds (20%, 50%, 80%).

### LVGL implementation notes
- Outer shell: one `lv_obj_create()` with border.
- Pin: separate fixed object on right side.
- Fill bar: inner object width set from SOC.
- Optional charging stripes: lightweight repeating rectangles in a clipped child container.

### Why this is good
- Lowest implementation complexity.
- Minimal CPU/GPU overhead.
- Works well even at smaller widget sizes.

---

## Suggestion 3 — Segmented Battery Cell Stack (10 Segments)

### Concept
Battery body split into discrete segments (e.g., 10). Number of lit segments maps to SOC.

### Diagram (not to scale)

```text
   .----------------------------------. [PIN]
   |[■][■][■][■][■][■][ ][ ][ ][ ]   |
   '----------------------------------'
      0   10  20  30  40  50 ... 100%
```

### Animation ideas
- Segment-by-segment “chase” fill when SOC rises.
- Slow heartbeat on last segment when SOC < 10%.
- During charging, animate a traveling highlight over active segments.

### LVGL implementation notes
- Pre-create 10 child segment objects.
- Compute active count as `round(soc / 10.0)`.
- For updates, toggle each segment visibility/opacity or background color.
- Optional animation: stagger per-segment delay (e.g., 30–50 ms).

### Why this is good
- Very stable and deterministic appearance.
- Great for noisy SOC signals (hysteresis per segment).
- Clear digital-like feedback.

---

## Recommended order to prototype
1. Horizontal single-cell (Suggestion 2) for quickest win.
2. Vertical two-terminal (Suggestion 1) for premium look.
3. Segmented stack (Suggestion 3) if SOC signal flicker needs visual damping.

---

## Practical LVGL guidance
- Use one reusable battery widget module in `espnowreceiver_LCD/src/ui/`.
- Keep the animation clock independent of telemetry update frequency.
- Add SOC hysteresis (e.g., update visual only if change >= 0.5%).
- Keep style definitions centralized to support day/night themes.

---

## Suggested default for your current screen
Use **Suggestion 1 (Vertical 2-Terminal)** as the primary centered battery widget, with:
- centered placement horizontally
- width fixed at 50% of screen width
- connected top terminals (not floating caps)
- smooth bottom-up fill using the same SOC gradient as current SOC text
- white in-battery SOC text (large, always readable)

This provides the best “at-a-glance” battery identity while keeping color meaning consistent across text and battery fill.

---

## Completeness suggestions for implementation
- Use one shared `soc_color()` path for both SOC text and battery fill to avoid drift.
- Add hysteresis (e.g., 0.3–0.5%) before triggering fill animation to prevent tiny visual jitter.
- Clamp text updates to one decimal place and keep fill animation duration independent from telemetry cadence.
- Validate contrast at low SOC (dark red fill) and high SOC (bright green fill); keep white text plus subtle dark text shadow if needed.
- Keep battery geometry constants in config to support future screen-size variants.
