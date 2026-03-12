# Power Bar & SOC Font Findings

## Scope Correction — 2026-03-11

This document originally captured earlier LVGL-oriented findings. The current receiver issue raised on 2026-03-11 is a TFT rendering issue, and the requirement is:

- The center of the first power bar should be centered horizontally between the left and right sides of the screen
- When the power magnitude is small, the central power bar should always remain visible
- Existing pulse behaviour and colour progression are otherwise acceptable

### Reviewed Conclusion

- The earlier concern about the TFT power bar being vertically centered was based on a misunderstanding of the requirement and is **not** the primary issue
- The real TFT defect is **horizontal center-marker behaviour**, especially for small charging values
- The existing LVGL notes below remain useful, but they are a separate topic from the TFT receiver power bar issue
- The TFT-specific center-marker and outward-growth refactor has now been implemented in the current branch

---

## TFT Receiver Power Bar Review

### Relevant Files

- [src/display/widgets/power_bar_widget.cpp](src/display/widgets/power_bar_widget.cpp)
- [src/display/widgets/power_bar_widget.h](src/display/widgets/power_bar_widget.h)
- [src/display/pages/status_page.cpp](src/display/pages/status_page.cpp)

### Implementation Status

- **Implemented for TFT** in the current branch
- The centre marker is now treated as a permanent dedicated cell
- Charge and discharge bars now grow outward from that fixed marker
- `fillRect` has been retained as the clear strategy because it remains the most efficient way to clear stale bar regions on this TFT path
- Redundant TFT widget state related to the old center-marker handling has been removed

### Post-Implementation Regression (2026-03-11)

During live testing, a decrement/shrink artefact was observed:

- when power bar count decreases, the clear region is offset by approximately half a bar cell
- this leaves a visible partial segment and makes shrink transitions look misaligned

### Regression Fix Status (Implemented)

This specific decrement alignment issue has now been fixed in the TFT widget implementation.

- The half-cell offset bias was removed from `clear_bar_range()`.
- The redundant `half_width` variable used only by that old arithmetic was removed.
- `fillRect` remains the clear mechanism (by design), but now uses bar-cell-aligned start coordinates.

Applied in:

- [src/display/widgets/power_bar_widget.cpp](src/display/widgets/power_bar_widget.cpp)

#### Where it occurs

- [src/display/widgets/power_bar_widget.cpp](src/display/widgets/power_bar_widget.cpp)
- Function: `clear_bar_range(bool negative, int start_bar_index, int count)`

#### Current geometry in code

The previous clear region was computed using a fixed half-cell assumption:

```cpp
const int width = count * bar_char_width_;
const int left = negative
  ? (x_ - ((start_bar_index + count) * bar_char_width_))
  : (x_ + ((start_bar_index + 1) * bar_char_width_));
```

#### Root cause (confirmed)

The clear path assumes each rendered `"-"` glyph is perfectly centered and symmetric around the logical bar center with exact half-width bounds. In practice with FreeFont + `MC_DATUM` + integer raster alignment, rendered glyph bounds are not guaranteed to match that exact `±bar_char_width_/2` model on every frame.

Result:

- shrink clear regions can start/end half a bar-cell off from the actually drawn pixels
- this is most visible during decreases (where only removed bars are cleared)

#### Implemented fix

Keep `fillRect` (correct choice for efficiency), but remove the half-cell start bias in the x-coordinate calculation for decrement clears.

The clear rectangle now begins at the dynamic bar-cell start coordinate (not half a cell earlier), which aligns shrink clearing with the bar grid used for drawing.

This preserves the low-redraw `fillRect` strategy while removing the observed half-cell decrement artefact.

### Correct Requirement Interpretation

The display should treat the screen midpoint as the visual zero-crossing point for power flow:

- a dedicated centre marker should remain present at the horizontal midpoint of the 320px screen at all times
- charge bars should begin immediately to the left of that marker and grow outward leftwards
- discharge bars should begin immediately to the right of that marker and grow outward rightwards
- the centre marker is not part of the animated magnitude bars and should never be overwritten by them

### Findings

#### 1. Discharge path currently overwrites the centre position

In [src/display/widgets/power_bar_widget.cpp](src/display/widgets/power_bar_widget.cpp#L95-L99), the discharge case draws bar index 0 at `x_`:

```cpp
int bar_x = negative ? (x_ - (barIndex + 1) * bar_char_width_) : (x_ + barIndex * bar_char_width_);
```

- For discharge (`negative == false`), bar 0 is drawn at `x_`
- Under the clarified requirement, `x_` should be reserved for the permanent centre marker
- This means the current discharge rendering model is not correct because it replaces the centre marker rather than growing outward from it

#### 2. Charge path geometry is closer to the desired model, but only half-correct

The charging case draws the first bar at `x_ - bar_char_width_`, not at `x_`.

- For small negative power values, the display shows the first green bar one position left of centre
- That left-side offset is actually the correct outward-growth geometry for a permanent centre marker design
- The problem is that the centre marker itself is not independently maintained, so the layout is asymmetric between charge and discharge

#### 3. Zero-state centre bar is only guaranteed on zero transition

In the zero/near-zero path, the blue centre marker is only drawn when entering zero state, not on every zero-state redraw.

- If another redraw operation disturbs that pixel region while power remains near zero, the centre marker is not explicitly restored each frame
- This makes the zero-crossing indicator less robust than intended

#### 4. Direction-change clearing can erase the centred bar

When transitioning from discharge to charge, the code clears the old right-side region starting at `x_`.

- That clear begins at the centre position
- It can wipe the centred reference bar before the left-side charging bars are drawn
- Result: the centre appears blank during small charging magnitudes and the transition is visually harsher than necessary

#### 5. Current pulse logic redraws more bars than necessary

In the pulse/ripple path, the widget redraws every visible bar on each ripple step.

- This is functional, but it is not the lowest-redraw approach
- For a stable centre-marker design, the marker should remain untouched during pulse animation
- The smoothest implementation would update only the currently highlighted bar and restore only the previously highlighted bar, rather than repainting the entire active side every frame

#### 6. Vertical positioning is not the issue for this requirement

The current TFT widget y-position in [src/display/pages/status_page.cpp](src/display/pages/status_page.cpp#L13-L18) may still be a design choice worth tuning, but it is not the cause of the corrected problem statement.

- No change is required purely to satisfy the horizontal-centering requirement
- Any vertical layout change should be treated as a separate UX decision

### Implemented TFT Resolution

#### Implemented behaviour

- The centre marker is a permanent, dedicated display element at `x_`
- Charge bars start at `x_ - bar_char_width_` and discharge bars start at `x_ + bar_char_width_`
- Dynamic magnitude bars no longer use the centre-marker cell
- `fillRect` clearing is constrained to dynamic side regions and excludes the centre marker
- The centre marker is redrawn on power-bar render passes that affect the area
- Redraws are reduced by updating only changed outer bars and only the bars touched by the pulse step

#### Code changes applied

1. In [src/display/widgets/power_bar_widget.cpp](src/display/widgets/power_bar_widget.cpp), `x_` is now reserved exclusively for the centre marker and discharge bar index 0 has been shifted to `x_ + bar_char_width_`
2. The charging start position remains at `x_ - bar_char_width_`, giving symmetric outward growth from the marker
3. The centre marker is now drawn as its own dedicated render step instead of being reused by discharge bars
4. Side-clearing paths now use targeted `fillRect` calls that exclude the centre marker cell
5. The old full-span ripple repaint has been replaced with an incremental pulse update that redraws only the affected pulse bar(s)
6. Redundant TFT widget state from the previous center-marker model has been removed
7. The current `status_page.cpp` vertical placement has been left unchanged, as it is unrelated to this corrected TFT requirement

#### Lowest-redraw transition model

The smoothest TFT approach is a three-region model:

- left dynamic region for charging bars
- one fixed centre-marker cell
- right dynamic region for discharging bars

With that model:

- zero → charge/discharge: draw only the newly required side bars; centre marker remains untouched
- charge/discharge magnitude increase: draw only the newly added outer bars
- charge/discharge magnitude decrease: clear only the removed outer bars
- charge ↔ discharge direction flip: clear only the previously active side, keep centre marker in place, then draw the new side
- pulse with unchanged bar count: redraw only one or two bars per animation step instead of repainting the entire active span

### Reviewed Final Assessment

| Area | Status | Notes |
|------|--------|-------|
| Permanent centre marker at midpoint | Incorrect | `x_` is reused by discharge rendering and side clearing |
| First discharge bar grows outward from marker | Incorrect | Currently starts at `x_` instead of one cell to the right |
| First charge bar grows outward from marker | Partially correct | Geometry is right, but marker preservation is not |
| Centre marker visible for small values | Incorrect | Not reliably preserved across zero hold and direction changes |
| Redraw efficiency during transitions | Could be improved | Current pulse path repaints the full active side |
| Decrement clear alignment | Incorrect | Shrink clear region can be offset by ~half a bar cell |
| Pulse behaviour | Correct | No issue found from this review |
| Colour progression with magnitude | Correct | No issue found from this review |
| Vertical placement relevance | Not root cause | Separate layout concern only |

### Recommended Priority

1. **High**: Convert the midpoint into a permanent dedicated marker that is never reused by dynamic bars
2. **High**: Shift discharge rendering one cell right so both directions grow outward symmetrically
3. **High**: Exclude the centre marker cell from all clear operations and redraw it on every affected pass
4. **High**: Replace half-width clear arithmetic with render-aligned decrement clear bounds
5. **Medium**: Reduce pulse animation redraws to changed bars only
6. **Low**: Review vertical placement only if you want a separate visual layout refinement

---

## TFT Rectangle Model to Fully Fill Center→Edge Space (2026-03-11)

### Request Summary

Goal: move the TFT dynamic bars to a rectangle-based renderer (not `"-"` glyphs), and make each side fill all available pixels from the center outward to the screen edge with a fixed number of bars.

This section proposes a deterministic geometry model that:

- uses identical math for draw and clear
- supports exact edge coverage
- avoids half-bar residual artefacts on recede
- gives predictable value-per-bar scaling

### Why this resolves the current issue class

The previous artefacts occurred because draw (FreeFont glyph) and clear (`fillRect`) used different geometry primitives. The fix is to use rectangles for both operations, with one shared bounds function per bar index.

If draw and clear both use `Rect(i, side)`, then any removed bar is erased exactly by repainting the same rectangle with background.

### Proposed Geometry

Assume:

- `screen_w = 320`
- `center_x = screen_w / 2 = 160`
- center marker drawn as a thin line (`center_marker_w = 2`)

Per-side drawable pixels:

$$
side\_pixels = \min(center\_x,\; screen\_w - center\_x)
$$

For 320-wide symmetric display:

$$
side\_pixels = 160
$$

Choose either:

1. Fixed bar count (`bars_per_side`) and compute pixel widths to exactly fill side, or
2. Fixed bar width (`slot_px`) and accept remainder distribution.

Recommended for exact fill: **fixed bar count with remainder distribution**.

### Exact-Fill Pixel Allocation (No Gaps)

Given:

- `bars_per_side = N`
- `side_pixels = S`

Compute:

$$
base = \left\lfloor \frac{S}{N} \right\rfloor, \quad rem = S \bmod N
$$

Allocate each bar width:

- first `rem` bars: `base + 1` pixels
- remaining bars: `base` pixels

This guarantees:

$$
\sum_{i=0}^{N-1} width_i = S
$$

So center→edge is fully covered with zero unassigned pixels.

### Worked Example (as requested)

If center→edge is `100px`, max value is `5000W`, choose `N = 10` bars:

- `value_per_bar = 5000 / 10 = 500W`
- `base = 100 / 10 = 10`, `rem = 0`
- each bar width = `10px`

So each bar represents `500W` and all 10 bars exactly fill `100px`.

If side was `103px` with `N=10`:

- `base=10`, `rem=3`
- widths = `[11,11,11,10,10,10,10,10,10,10]`
- total = `103px` exact fill.

### Bar Rect Construction

Define cumulative prefix widths from center outward:

$$
prefix_0 = 0, \quad prefix_{i+1} = prefix_i + width_i
$$

Right side (discharge) bar `i`:

- `x0 = center_x + prefix_i`
- `x1 = center_x + prefix_{i+1} - 1`

Left side (charge) bar `i`:

- `x1 = center_x - 1 - prefix_i`
- `x0 = center_x - prefix_{i+1}`

Then:

- draw: `fillRect(x0, y, x1-x0+1, h, color)`
- clear: `fillRect(x0, y, x1-x0+1, h, bg)`

Same rectangle in both operations = deterministic erase.

### Power→Bars Mapping

Use clamped magnitude:

$$
mag = \min(|power|,\; max\_power)
$$

Bars visible:

$$
bars = \left\lfloor \frac{mag \cdot N}{max\_power} \right\rfloor
$$

Optional minimum visibility:

- if `mag > 0` and `bars == 0`, set `bars = 1`

### Transition Rules (Minimal Redraw)

- Increase same side: draw `i = previous_abs ... bars-1`
- Recede same side: clear `i = bars ... previous_abs-1`
- Direction flip: clear all previous side bars, draw new side bars
- Zero: clear previous side bars, keep center marker

No side-span clear is required.

### Suggested TFT Implementation Plan

Files:

- [src/display/widgets/power_bar_widget.h](src/display/widgets/power_bar_widget.h)
- [src/display/widgets/power_bar_widget.cpp](src/display/widgets/power_bar_widget.cpp)

Add/replace methods:

1. `compute_bar_geometry()`
  - computes `bars_per_side_`, `bar_widths_[N]`, `bar_prefix_[N+1]`
2. `get_bar_rect(int i, bool negative)`
  - returns exact rectangle bounds for side/index
3. `draw_dynamic_bar_rect(int i, bool negative, uint16_t color)`
4. `clear_dynamic_bar_rect(int i, bool negative)`

Replace glyph drawing path:

- stop using `drawString("-")` for dynamic bars
- keep center marker as vertical line or narrow fixed rectangle

### Validation Checklist

1. At max discharge, right bars end exactly at right edge pixel.
2. At max charge, left bars end exactly at left edge pixel.
3. Recede by 1 bar repeatedly: no residual pixels.
4. Rapid direction flips: no center corruption, no ghosts.
5. Zero hold: center marker remains visible.

### Final Recommendation

Implement the rectangle geometry model above with exact per-bar rectangles and remainder-distributed widths. This is the clean resolution because draw and clear use identical primitives and identical bounds, removing the root cause rather than tuning offsets.

---

## Issue 1: Power Bar Width

### Current Implementation
**File**: [src/display/widgets/power_widget_lvgl.cpp](src/display/widgets/power_widget_lvgl.cpp#L34-L55)

The power bar containers are sized as follows:
```cpp
// Left bar container (charging)
lv_obj_set_size(left_bar_container_, center_x, BAR_HEIGHT + 10);
lv_obj_align(left_bar_container_, LV_ALIGN_LEFT_MID, 0, 0);

// Right bar container (discharging)  
lv_obj_set_size(right_bar_container_, ::Display::SCREEN_WIDTH - center_x, BAR_HEIGHT + 10);
lv_obj_align(right_bar_container_, LV_ALIGN_RIGHT_MID, 0, 0);
```

**Analysis**: 
- Left bar container width: `center_x` (160px, half screen width)
- Right bar container width: `320 - center_x = 160px` (half screen width)
- The bars **DO NOT** extend to the screen edges because:
  - Each bar is only 8px wide (`BAR_WIDTH = 8`)
  - There's 3px spacing between bars (`BAR_SPACING = 3`)
  - Maximum bars per side: 30
  - Maximum width used: `30 * (8 + 3) = 330px` theoretical, but:
    - Left container actual width = 160px
    - Right container actual width = 160px
  - Bar positioning adds offsets that leave gaps at edges

**Calculation of Unused Space**:
- At maximum (30 bars on each side):
  - Left bars: 30 × (8 + 3) = 330px needed → only 160px available = **170px wasted**
  - Right bars: 30 × (8 + 3) = 330px needed → only 160px available = **170px wasted**

### Recommendations

**Option A: Increase Container Width** (Recommended - Simplest)
- Set `left_bar_container_` width to full screen width (320px)
- Set `right_bar_container_` width to full screen width (320px)
- Modify alignment to expand containers while keeping bars centered

**Option B: Reduce Bar/Spacing Dimensions** (Alternative)
- Reduce `BAR_WIDTH` from 8px to 6px
- Reduce `BAR_SPACING` from 3px to 2px
- This allows ~60 bars per side to fit 320px width
- More granular power representation but smaller visual indicators

**Option C: Use Full-Width Container** (Advanced)
- Create single full-width bar container
- Position bars from center outward using absolute positioning
- Cleaner layout, bars truly extend to edges

**Recommended Fix**: Option A
- Easiest implementation
- Maintains current bar visibility
- Bars extend naturally to screen edges
- No visual reduction in bar size

---

## Issue 2: SOC Font Pixelation

### Current Implementation
**File**: [src/display/widgets/soc_widget_lvgl.cpp](src/display/widgets/soc_widget_lvgl.cpp#L40)

```cpp
// Apply Montserrat 28pt font (equivalent to FreeSansBold18pt7b at size 2)
lv_obj_set_style_text_font(label_, &lv_font_montserrat_28, 0);
```

**Analysis**:
- Using LVGL built-in `lv_font_montserrat_28` font
- This is a bitmap font (rasterized at fixed size)
- Pixelation appears because:
  - LVGL's built-in fonts are bitmap fonts, not vector fonts
  - No anti-aliasing at edges
  - Limited vertical resolution at smaller sizes

### LVGL Built-in Font Options

| Font | Size | Style | Notes |
|------|------|-------|-------|
| `lv_font_montserrat_12` | 12pt | Bitmap | Very small, readable |
| `lv_font_montserrat_14` | 14pt | Bitmap | Small, basic readability |
| `lv_font_montserrat_16` | 16pt | Bitmap | Moderate size |
| `lv_font_montserrat_18` | 18pt | Bitmap | Slightly larger |
| `lv_font_montserrat_20` | 20pt | Bitmap | Good readable size |
| `lv_font_montserrat_22` | 22pt | Bitmap | Larger |
| `lv_font_montserrat_24` | 24pt | Bitmap | **BEST for SOC** |
| `lv_font_montserrat_26` | 26pt | Bitmap | Large |
| `lv_font_montserrat_28` | 28pt | Bitmap | **Current - PIXELATED** |
| `lv_font_montserrat_30` | 30pt | Bitmap | Very large, pixelation increases |

**Why Montserrat 24 is Better**:
- Still large enough for readability (not too small)
- Bitmap fonts typically look better at their native rendering sizes
- 24pt is a standard LVGL font with optimized anti-aliasing
- Smaller than 28pt reduces pixelation artifacts
- Still prominent for battery SOC display

### Recommendations

**Option A: Use Montserrat 24pt** (Recommended - Best Balance)
```cpp
lv_obj_set_style_text_font(label_, &lv_font_montserrat_24, 0);
```
- Reduces pixelation significantly
- Maintains excellent readability
- Standard rendering size = better quality
- Still visually prominent

**Option B: Use Montserrat 20pt** (Compact Alternative)
```cpp
lv_obj_set_style_text_font(label_, &lv_font_montserrat_20, 0);
```
- Even smoother appearance
- Smaller vertical footprint
- Good for tight layouts

**Option C: Add Custom TrueType Font** (Advanced - Future)
- LVGL supports FreeType for vector fonts
- Requires additional font files and heap memory
- Would eliminate pixelation entirely
- Currently disabled in lv_conf.h due to ESP32 constraints

**Option D: Use Montserrat Bold** (if available)
```cpp
lv_obj_set_style_text_font(label_, &lv_font_montserrat_24, 0);
// Keep montserrat_24 - it's the best compromise
```

### Font Comparison at Display Size (320×170)

```
Current (28pt):    "75.3%"  ← Large but visibly pixelated edges
Recommended (24pt): "75.3%" ← Clean, smooth, highly readable
Compact (20pt):     "75.3%" ← Very clean, smaller footprint
```

**Why Smaller Fonts Look Less Pixelated**:
- Bitmap fonts are pre-rendered at specific sizes
- Larger sizes amplify rendering artifacts
- 24pt and 20pt are closer to native resolution
- LVGL's font rendering is optimized for these standard sizes

---

## Summary Table

| Component | Current | Problem | Recommended Fix | Expected Result |
|-----------|---------|---------|-----------------|-----------------|
| **Power Bar Width** | 160px each side | Stops short of screen edges | Increase containers to 320px full width | Bars extend naturally to screen edges |
| **SOC Font** | Montserrat 28pt | Pixelated/jagged edges | Switch to Montserrat 24pt | Smooth, anti-aliased text appearance |

## Implementation Priority

1. **High**: Change SOC font to 24pt (1-line change)
2. **High**: Extend power bar containers to full width (3-line changes)
3. **Optional**: Fine-tune bar dimensions if needed

---

## Files to Modify

1. **Power Bar Width**:
   - [src/display/widgets/power_widget_lvgl.cpp](src/display/widgets/power_widget_lvgl.cpp#L34-L55)
   - Change container widths from `center_x` and `width - center_x` to `SCREEN_WIDTH`

2. **SOC Font**:
   - [src/display/widgets/soc_widget_lvgl.cpp](src/display/widgets/soc_widget_lvgl.cpp#L40)
   - Change `lv_font_montserrat_28` to `lv_font_montserrat_24`

