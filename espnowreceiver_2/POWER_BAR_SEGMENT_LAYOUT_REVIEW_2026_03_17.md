# Power Bar Ghosting Review (TFT) — 2026-03-17

## Scope
Reviewed the current TFT power-bar implementation in:
- `src/display/tft_impl/tft_display.cpp`
- `src/display/layout/display_layout_spec.h`

Goal requested: use 21 fixed rectangular segments (10 left + 1 center + 10 right), 2 px gap, full display width usage, center segment always visible, no ghost bars when power recedes, pulse retained when bar count is unchanged.

---

## Findings

### 1) Why the current implementation leaves "ghost half bars"
Current bars are rendered as text glyphs (`"-"`) with `tft.drawString()` and `MC_DATUM` center anchoring.

This creates three compounding issues:

1. **Glyph geometry is font-dependent, not pixel-locked**
   - Width comes from `tft.textWidth("-")`.
   - Actual rendered pixels can vary by font metrics/anti-aliasing.

2. **Clear math assumes rectangular bar extents derived from text width**
   - Clearing uses `clear_rect()` blocks based on multiples of `bar_char_width`.
   - But drawn glyph edges are not guaranteed to align exactly with those blocks.

3. **Center-anchored text + integer stepping causes edge mismatch**
   - Bars are placed by center point (`center_x ± i*bar_char_width`).
   - Clear regions are axis-aligned rectangles from aggregate widths.
   - This can leave a residual edge on recede (seen as half-ghost).

Conclusion: ghosting is a direct side effect of using font glyphs for bars instead of deterministic rectangles.

---

### 2) Your proposed 21-segment rectangular layout is correct
Using explicit rectangles is the right fix and simplifies all cleanup logic.

For display width `W = 320`, segments `N = 21`, gap `G = 2`:

- Total gap pixels: `(N - 1) * G = 20 * 2 = 40`
- Pixel budget for bars: `W - 40 = 280`
- Segment width: `SEG_W = floor(280 / 21) = 13`
- Used width: `21*13 + 20*2 = 313`
- Remaining margin: `320 - 313 = 7`
  - Left margin: `3`
  - Right margin: `4`

This gives deterministic, full-width use with tiny symmetric outer padding.

---

## Recommended geometry model

Define constants (layout spec):
- `SEGMENTS_TOTAL = 21`
- `SEGMENTS_PER_SIDE = 10`
- `SEGMENT_GAP_PX = 2`
- `SEGMENT_W_PX = 13` (derived)
- `SEGMENT_H_PX = 10`
- `SEGMENT_Y_PX = 115`
  - Derived from: `BAR_Y − (SEGMENT_H_PX / 2) = 120 − 5 = 115`
  - Keeps the visual centre of the segment aligned with the existing `BAR_Y = 120` baseline.
- `SEGMENT_MARGIN_LEFT_PX = 3` (derived)

Indexing:
- Segment indices: `0..20`
- Center index: `10` (always lit)

Position formula:
- `x(i) = margin_left + i * (SEGMENT_W + GAP)`

Power mapping:
- `bars_side = map(abs(power), 0..MAX_POWER, 0..10)`
- Charging (<0): fill center + indices `9..(10-bars_side)`
- Discharging (>0): fill center + indices `11..(10+bars_side)`
- Zero: center only

---

## Cleanup strategy to remove recede ghosts completely

Use **segment-state based rendering** rather than area-clearing heuristics.

1. Keep previous state:
   - `prev_direction` (charging/discharging/zero)
   - `prev_bars_side` (0..10)

2. On update:
   - Compute new active segment set.
   - Compute previous active segment set.
   - Clear exactly `previous - current` segments using `fillRect(x(i), y, SEG_W, SEG_H, background)`.
   - Draw exactly `current - previous` segments.

This guarantees no half ghosts, regardless of recede direction or sign change.

---

## Pulse effect retention

Pulse should remain when count and direction are unchanged.

Recommended pulse implementation with rectangles:
- Keep current active set fixed.
- Animate one segment at a time on active side only:
  - draw dim color on segment `k`
  - short delay
  - redraw normal color on segment `k`
- Keep center always normal (or optionally pulse first depending on design choice).

This reproduces current ripple behavior but with deterministic pixels.

---

## Suggested implementation plan

1. Add segment-layout constants and derivation helpers in layout spec.
2. Replace text-based bar drawing (`drawString("-")`) with rectangle drawing.
3. Replace area clear logic with per-segment diff clear/draw.
4. Keep existing power-to-count mapping and pulse timing constant.
5. Validate scenarios:
   - increase/decrease same sign
   - sign flip at same magnitude
   - max to zero and zero to max
   - repeated same value (pulse path)

---

## Final recommendation

Proceed with the 21 fixed-rectangle segment architecture. It directly addresses the ghosting root cause, uses full width cleanly, simplifies recede cleanup math, and preserves pulse behavior with less rendering ambiguity than font glyph bars.
