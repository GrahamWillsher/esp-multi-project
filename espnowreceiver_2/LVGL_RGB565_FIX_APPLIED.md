# LVGL 8.x Custom RGB565 Buffer — Quick Fix Implemented

**Date**: March 1, 2026  
**Status**: ✅ Fix Applied  
**File Modified**: `src/display/display_splash_lvgl.cpp`  
**Lines Changed**: 117-139 (MCU copy loop)  

---

## The Problem

JPEG decoded to RGB565 in PSRAM buffer, but image doesn't display on screen even though:
- ✅ Logs show successful JPEG decode
- ✅ LVGL widget created and positioned
- ✅ Widget visible flag set
- ❌ **Image doesn't appear** or shows corrupted colors

---

## Root Cause

**Byte order mismatch** between JPEG decoder output and LVGL's expected format:

```
Your Configuration:
├─ LV_COLOR_16_SWAP = 1         (in lv_conf.h)
├─ TFT_eSPI.setSwapBytes(true)  (in lvgl_driver.cpp)
└─ JPEGDecoder output: Standard RGB565 format

Problem:
├─ JPEGDecoder gives:    0xRRRRRGGG|GGGBBBBB (standard)
└─ LVGL expects (swap=1): 0xGGGBBBBB|RRRRRGGG (byte-swapped)

Result: Every pixel's bytes are reversed → corrupted colors or black rectangle
```

---

## The Fix

**Applied to**: `src/display/display_splash_lvgl.cpp` lines 117-139

**Before**:
```cpp
rgb565_buffer[dst_idx] = pImg[y * mcu_w + x];  // Direct copy — WRONG
```

**After**:
```cpp
uint16_t pixel = pImg[y * mcu_w + x];
rgb565_buffer[dst_idx] = __builtin_bswap16(pixel);  // Byte swap — CORRECT
```

**What this does**:
- Takes each RGB565 pixel from JPEG decoder
- Swaps the two bytes: `0xAABB` → `0xBBAA`
- Stores in buffer matching LVGL's expected byte-swapped format
- Performance: Single GCC built-in instruction (~1 CPU cycle per pixel)

---

## Complete Fixed Function

Location: [src/display/display_splash_lvgl.cpp](src/display/display_splash_lvgl.cpp#L117-L139)

```cpp
// Convert JPEG MCU blocks to RGB565 buffer WITH BYTE SWAP
// CRITICAL: LV_COLOR_16_SWAP=1 requires byte-swapped RGB565
// JPEGDecoder outputs standard RGB565, so we must swap: 0xAABB → 0xBBAA
uint32_t pixel_idx = 0;
while (JpegDec.read()) {
    uint16_t* pImg = JpegDec.pImage;
    uint16_t mcu_w = JpegDec.MCUWidth;
    uint16_t mcu_h = JpegDec.MCUHeight;
    uint32_t mcu_x = JpegDec.MCUx * mcu_w;
    uint32_t mcu_y = JpegDec.MCUy * mcu_h;
    
    // Copy MCU block to RGB565 buffer
    for (uint16_t y = 0; y < mcu_h; y++) {
        if (mcu_y + y >= img_height) break;
        
        for (uint16_t x = 0; x < mcu_w; x++) {
            if (mcu_x + x >= img_width) break;
            
            uint32_t dst_idx = (mcu_y + y) * img_width + (mcu_x + x);
            
            // Byte swap for LV_COLOR_16_SWAP=1
            // Converts standard RGB565 → LVGL byte-swapped format
            uint16_t pixel = pImg[y * mcu_w + x];
            rgb565_buffer[dst_idx] = __builtin_bswap16(pixel);
        }
    }
}
```

---

## Why This Works

### Configuration Verification ✅

Your setup is correct:
```cpp
// lv_conf.h line 28-32
#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP        1    // ← Tells LVGL to expect byte-swapped pixels
```

```cpp
// hal/display/lvgl_driver.cpp
tft.setSwapBytes(true);  // ← Tells hardware to swap bytes when displaying
```

### Missing Link ← **THIS WAS THE PROBLEM**

The pixel **data itself** also needed to be byte-swapped to match:

```
Flow:
RGB565 Buffer (with byte swap) → LVGL Image Widget → Hardware (setSwapBytes=true)
                    ✅ Now added        ✅ Existing        ✅ Existing
```

---

## Testing & Verification

### Expected Results After Fix

1. **Image appears on screen** ✅
2. **Colors are correct** (not inverted/corrupted) ✅
3. **No performance degradation** (byte swap is ~1 CPU cycle per pixel) ✅
4. **No memory issues** (same allocation scheme as before) ✅

### If Image Still Doesn't Display

Check this priority list:

1. **Memory allocation failed silently**
   ```cpp
   if (!rgb565_buffer) {
       LOG_ERROR("LVGL_SPLASH", "RGB565 buffer allocation failed");
       return nullptr;
   }
   ```

2. **Image descriptor not fully initialized**
   ```cpp
   LOG_DEBUG("IMG", "always_zero=%d (must be 0)", img_dsc->header.always_zero);
   LOG_DEBUG("IMG", "CF=0x%02X (expect 0x04)", img_dsc->header.cf);
   ```

3. **Byte swap direction is opposite**
   - If image colors still look wrong, try without byte swap:
   ```cpp
   rgb565_buffer[dst_idx] = pImg[y * mcu_w + x];  // No swap
   ```
   - Then change `LV_COLOR_16_SWAP` to `0` in `lv_conf.h`

4. **PSRAM not working**
   ```cpp
   // Verify PSRAM allocation:
   uint16_t* test = (uint16_t*)heap_caps_malloc(100, MALLOC_CAP_SPIRAM);
   if (!test) LOG_ERROR("PSRAM not available");
   heap_caps_free(test);
   ```

---

## Technical Details

### Byte Swap Operation

```cpp
// What __builtin_bswap16() does:
// Input:  0b 1111_1111 0000_0000 (0xFF00)
// Output: 0b 0000_0000 1111_1111 (0x00FF)

// For Red (RGB565):
// Standard RGB565: 1111_1000_0000_0000 = 0xF800
// Byte swapped:    0000_0000_1111_1000 = 0x00F8
```

### Performance Impact

- **No visible performance impact**
- Each pixel: 1 swap operation (1 CPU cycle on modern processors)
- For 320×170 image: 54,400 operations total
- Time: ~0.5ms on ESP32-S3 at 240MHz
- **Negligible** compared to JPEG decode time (hundreds of ms)

---

## Reference: What Changed

| Aspect | Before | After | Impact |
|--------|--------|-------|--------|
| Pixel copy | `pImg[i]` → buffer | `bswap16(pImg[i])` → buffer | ✅ Correct byte order |
| Buffer contents | Wrong byte order | Correct byte order | ✅ Image displays |
| Performance | N/A | +1 op/pixel (~0.5ms) | ✅ Negligible |
| Memory | Same | Same | ✅ No change |

---

## Files Reference

### Modified
- [src/display/display_splash_lvgl.cpp](src/display/display_splash_lvgl.cpp#L117-L139)

### Related Files (No changes needed)
- `src/lv_conf.h` — Configuration is correct ✅
- `src/hal/display/lvgl_driver.cpp` — Hardware setup is correct ✅
- `src/display/display_splash_lvgl.h` — Header is correct ✅

---

## Summary

✅ **Problem Identified**: Missing byte swap in MCU→buffer copy  
✅ **Solution Implemented**: Added `__builtin_bswap16()` to pixel copy loop  
✅ **Testing**: Rebuild and test with JPEG splash screen  
✅ **Expected**: Image displays with correct colors

**Status**: Ready for compilation and testing 🎉

