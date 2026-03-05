# Power Bar & SOC Font Findings

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

