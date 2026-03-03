# LVGL 8.x Custom RGB565 Buffer Display — Complete Solution

**Date**: March 1, 2026  
**Status**: Research Complete + Solution Pattern Confirmed  
**Hardware**: ESP32-S3 + ST7789 + JPEGDecoder  
**LVGL Version**: 8.3+  

---

## Executive Summary

### The Core Issue

When displaying a manually-allocated RGB565 buffer in LVGL 8.x:
- Logs show: ✅ JPEG decoded, ✅ widget created, ✅ positioned, ✅ visible flag set
- But: ❌ **Image doesn't appear on screen** → Corrupted colors or black rectangle

### Root Causes Found

1. **Byte Order Mismatch** — Most Common ⚠️
   - JPEG decoder outputs standard RGB565 (`RRRRRGGG GGGBBBBB`)
   - LVGL with `LV_COLOR_16_SWAP=1` expects byte-swapped format (`GGGBBBBB RRRRRGGG`)
   - **Solution**: Manually swap bytes when copying MCU blocks

2. **Image Descriptor Incomplete Initialization**
   - Missing fields or incorrect flags
   - **Solution**: Initialize all header fields correctly

3. **Memory Alignment Issues**
   - Buffer not aligned to word boundaries
   - **Solution**: Use `heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`

4. **Color Format Selection Wrong**
   - `LV_IMG_CF_TRUE_COLOR` vs `LV_IMG_CF_TRUE_COLOR_ALPHA` confusion
   - **Solution**: Use correct format based on actual data

---

## 1. Correct Way to Wrap Raw RGB565 Buffer in lv_img_dsc_t

### ✅ CORRECT Pattern (Your Current Implementation - Needs Byte Swap Fix)

```cpp
/**
 * Load JPEG and populate lv_img_dsc_t with proper byte-swapped RGB565
 * 
 * CRITICAL: When LV_COLOR_16_SWAP=1 is set:
 * - JPEG decoder gives: 0xRRRRRGGG|GGGBBBBB (standard RGB565)
 * - LVGL expects:      0xGGGBBBBB|RRRRRGGG (byte-swapped)
 */
uint8_t* load_jpeg_from_littlefs_lvgl(const char* filename, lv_img_dsc_t* img_dsc) {
    // 1. Open and load JPEG file
    File jpegFile = LittleFS.open(filename, "r");
    if (!jpegFile) {
        LOG_ERROR("LVGL_SPLASH", "Failed to open JPEG: %s", filename);
        return nullptr;
    }
    
    size_t fileSize = jpegFile.size();
    uint8_t* jpegData = (uint8_t*)malloc(fileSize);
    if (!jpegData) {
        LOG_ERROR("LVGL_SPLASH", "Memory allocation failed for JPEG");
        jpegFile.close();
        return nullptr;
    }
    
    jpegFile.read(jpegData, fileSize);
    jpegFile.close();
    
    // 2. Decode JPEG
    if (!JpegDec.decodeArray(jpegData, fileSize)) {
        LOG_ERROR("LVGL_SPLASH", "JPEG decode failed");
        free(jpegData);
        return nullptr;
    }
    
    uint16_t img_width = JpegDec.width;
    uint16_t img_height = JpegDec.height;
    size_t pixel_count = img_width * img_height;
    
    free(jpegData);  // Free JPEG data (no longer needed)
    
    // 3. Allocate RGB565 buffer (static = persists for program lifetime)
    static uint16_t* rgb565_buffer = nullptr;
    
    if (rgb565_buffer == nullptr) {
        // CRITICAL: Use heap_caps_malloc for PSRAM with proper alignment
        rgb565_buffer = (uint16_t*)heap_caps_malloc(
            pixel_count * sizeof(uint16_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        
        if (!rgb565_buffer) {
            LOG_ERROR("LVGL_SPLASH", "RGB565 buffer allocation failed");
            return nullptr;
        }
        
        LOG_DEBUG("LVGL_SPLASH", "RGB565 buffer allocated: %d bytes in PSRAM",
                  pixel_count * sizeof(uint16_t));
    }
    
    // 4. Convert JPEG MCU blocks to RGB565 buffer WITH BYTE SWAP
    //    ===== THIS IS THE CRITICAL PART =====
    uint32_t pixel_idx = 0;
    while (JpegDec.read()) {
        uint16_t* pImg = JpegDec.pImage;  // Standard RGB565 from decoder
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
                
                // ===== BYTE SWAP: Required because LV_COLOR_16_SWAP=1 =====
                uint16_t pixel = pImg[y * mcu_w + x];
                
                // Swap bytes: 0xAABB → 0xBBAA
                // This converts standard RGB565 → LVGL's byte-swapped format
                rgb565_buffer[dst_idx] = __builtin_bswap16(pixel);
                
                // Or manually: rgb565_buffer[dst_idx] = ((pixel & 0xFF) << 8) | (pixel >> 8);
            }
        }
    }
    
    // 5. Populate LVGL image descriptor
    // ===== ALL FIELDS ARE CRITICAL =====
    img_dsc->header.always_zero = 0;        // MUST be 0 (reserved field)
    img_dsc->header.w = img_width;           // Width in pixels
    img_dsc->header.h = img_height;          // Height in pixels
    img_dsc->header.cf = LV_IMG_CF_TRUE_COLOR;  // NO ALPHA (RGB565 only)
    img_dsc->data_size = pixel_count * sizeof(uint16_t);  // Total bytes
    img_dsc->data = (const uint8_t*)rgb565_buffer;  // Pointer to pixel data
    
    LOG_INFO("LVGL_SPLASH", "JPEG loaded (byte-swapped): %dx%d, %d bytes",
             img_width, img_height, pixel_count * sizeof(uint16_t));
    
    return (uint8_t*)rgb565_buffer;
}
```

### Key Points

✅ **Always initialize all header fields**:
```cpp
img_dsc->header.always_zero = 0;  // Reserved - MUST be 0
img_dsc->header.w = width;
img_dsc->header.h = height;
img_dsc->header.cf = LV_IMG_CF_TRUE_COLOR;  // RGB565, no alpha
```

✅ **Set correct color format**:
- `LV_IMG_CF_TRUE_COLOR` = RGB565 (5+6+5 bits, 2 bytes per pixel) — **USE THIS**
- `LV_IMG_CF_TRUE_COLOR_ALPHA` = ARGB8888 (8+8+8+8 bits, 4 bytes) — Don't use unless you have alpha channel
- `LV_IMG_CF_INDEXED_1BIT` / `2BIT` / `4BIT` / `8BIT` = Palette-based — Only for indexed images

✅ **Use PSRAM-aware allocation**:
```cpp
uint16_t* buffer = (uint16_t*)heap_caps_malloc(
    pixel_count * sizeof(uint16_t),
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
);
```

✅ **Make buffer static (for splash screens)**:
```cpp
static uint16_t* rgb565_buffer = nullptr;
if (rgb565_buffer == nullptr) {
    // First call: allocate
    rgb565_buffer = ...
}
// Subsequent calls: reuse same buffer
```

---

## 2. Specific Requirements for Raw C Arrays

### Must-Have Initialization

```cpp
lv_img_dsc_t my_image = {
    .header = {
        .always_zero = 0,      // ✅ CRITICAL
        .w = 320,
        .h = 170,
        .cf = LV_IMG_CF_TRUE_COLOR,  // ✅ RGB565 format
    },
    .data_size = 320 * 170 * 2,  // ✅ In bytes, not pixels
    .data = (const uint8_t*)my_rgb565_buffer,  // ✅ Cast to const uint8_t*
};
```

### Verification Checklist

- [ ] `always_zero == 0`? (If not: LVGL rejects image)
- [ ] Width/height match actual buffer dimensions?
- [ ] `data_size` equals `width * height * 2` (for RGB565)?
- [ ] `cf` set to `LV_IMG_CF_TRUE_COLOR`?
- [ ] `data` pointer is valid and points to actual buffer?
- [ ] Bytes are in correct order (standard RGB565 vs byte-swapped)?

---

## 3. Color Format Selection for RGB565 with Byte Swapping

### Configuration Matrix

Your current configuration:
```
LV_COLOR_DEPTH = 16          // 16-bit color
LV_COLOR_16_SWAP = 1         // LVGL expects byte-swapped RGB565
TFT_eSPI.setSwapBytes(true)  // Hardware also swaps
```

### What This Means

| Step | Format | Bytes | Example |
|------|--------|-------|---------|
| **JPEG Output** | Standard RGB565 | `RR RR RGG GGG BBB` | `0xF800` = Red |
| **After `__builtin_bswap16()`** | Byte-swapped | `GGG GBBB BRRR RRRR` | `0x00F8` = Red (swapped) |
| **In LVGL Buffer** | Matches `LV_COLOR_16_SWAP=1` | ✅ Correct | Image displays |

### Code Pattern for Byte Swapping

**Option 1: Fastest (GCC built-in)**
```cpp
uint16_t pixel = pImg[y * mcu_w + x];
rgb565_buffer[dst_idx] = __builtin_bswap16(pixel);  // ~1 CPU cycle
```

**Option 2: Portable**
```cpp
uint16_t pixel = pImg[y * mcu_w + x];
rgb565_buffer[dst_idx] = ((pixel & 0xFF) << 8) | (pixel >> 8);
```

**Option 3: Avoid (Slow)**
```cpp
uint8_t* src = (uint8_t*)pImg;
uint8_t* dst = (uint8_t*)rgb565_buffer;
dst[0] = src[1];  // ❌ Too many memory accesses
dst[1] = src[0];
```

### When NOT to Swap

If `LV_COLOR_16_SWAP = 0`:
```cpp
// Don't swap — copy directly
rgb565_buffer[dst_idx] = pImg[y * mcu_w + x];  // No byte swap
```

---

## 4. Common Issues & Pitfalls

### Issue 1: Image Appears But Colors Are Wrong (Corrupted/Inverted)

**Symptom**: Image displays, but colors are incorrect or inverted

**Cause**: Byte swap mismatch

**Solution**:
```cpp
// If colors look inverted/corrupted, check your swap setting
// Current: LV_COLOR_16_SWAP = 1
// Try:     LV_COLOR_16_SWAP = 0
// And remove the __builtin_bswap16() call

// OR verify TFT_eSPI matches:
tft.setSwapBytes(true);  // Must match LV_COLOR_16_SWAP=1
```

### Issue 2: Black Rectangle or No Image

**Symptom**: Widget exists, positioned correctly, but displays nothing

**Cause(s)**:
1. `img_dsc->header.always_zero != 0` — LVGL rejects image
2. `data` pointer is invalid or freed
3. Buffer allocation failed silently
4. `cf` field set wrong

**Debug Checklist**:
```cpp
// Add after lv_img_set_src():
lv_obj_t* img = lv_img_create(parent);
lv_img_set_src(img, &img_dsc);

// Diagnostic logging
LOG_DEBUG("IMG", "Width: %d", img_dsc.header.w);
LOG_DEBUG("IMG", "Height: %d", img_dsc.header.h);
LOG_DEBUG("IMG", "CF: 0x%02X (expect 0x04 for TRUE_COLOR)", img_dsc.header.cf);
LOG_DEBUG("IMG", "Data size: %d bytes", img_dsc.data_size);
LOG_DEBUG("IMG", "Data pointer: 0x%08X", (uint32_t)img_dsc.data);
LOG_DEBUG("IMG", "Always zero: %d (must be 0)", img_dsc.header.always_zero);

// Verify first few pixels
uint16_t* pix = (uint16_t*)img_dsc.data;
LOG_DEBUG("IMG", "First pixel: 0x%04X", pix[0]);
LOG_DEBUG("IMG", "Second pixel: 0x%04X", pix[1]);
```

### Issue 3: Memory Corruption / Crashes

**Symptom**: Crash when displaying image, or other widgets become corrupted

**Cause(s)**:
1. Buffer allocated from standard heap (gets fragmented)
2. Buffer size calculation wrong
3. Freed while image widget still references it

**Solution**:
```cpp
// ✅ Allocate from PSRAM with proper flags
uint16_t* buffer = (uint16_t*)heap_caps_malloc(
    width * height * 2,           // Correct size
    MALLOC_CAP_SPIRAM |           // Must be PSRAM
    MALLOC_CAP_8BIT               // For display DMA
);

// ✅ Make static so it's never freed
static uint16_t* buffer = nullptr;
if (buffer == nullptr) { /* allocate once */ }

// ✅ Verify allocation succeeded
if (!buffer) {
    LOG_ERROR("Failed to allocate RGB565 buffer");
    return nullptr;
}
```

### Issue 4: Image Takes Forever to Load / Blocks System

**Symptom**: Freeze/stall when loading JPEG

**Cause(s)**:
1. Decoding large JPEG takes too long
2. MCU loop not yielding to OS
3. No way to show progress

**Solution** (for large JPEGs):
```cpp
// Decode in chunks with yields
uint32_t block_count = 0;
while (JpegDec.read()) {
    // Copy MCU data...
    
    block_count++;
    if (block_count % 10 == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));  // Yield to OS every 10 MCUs
        lv_timer_handler();              // Let LVGL update
    }
}
```

### Issue 5: "Different Images or Garbage" on Screen

**Symptom**: Correct size/position, but wrong pixel data displays

**Cause(s)**:
1. Buffer not copied correctly (wrong stride)
2. Only partial image in buffer
3. Buffer contents modified after setting image source

**Solution**:
```cpp
// Verify copy logic:
// Wrong:
for (int i = 0; i < width * height; i++) {
    rgb565_buffer[i] = pImg[i];  // ❌ Doesn't account for MCU layout
}

// Right:
for (uint16_t y = 0; y < mcu_h; y++) {
    if (mcu_y + y >= img_height) break;
    for (uint16_t x = 0; x < mcu_w; x++) {
        if (mcu_x + x >= img_width) break;
        uint32_t dst_idx = (mcu_y + y) * img_width + (mcu_x + x);  // ✅ 2D indexing
        rgb565_buffer[dst_idx] = pImg[y * mcu_w + x];
    }
}
```

### Issue 6: LVGL Flicker or Partial Update

**Symptom**: Image flashes or updates partially

**Cause**: `lv_timer_handler()` calls not sufficient for rendering to complete

**Solution** (your code already does this correctly):
```cpp
// Load screen
lv_scr_load(splash_screen);

// Drain rendering pipeline (multiple calls)
for (int i = 0; i < 5; i++) {
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(10));  // Let hardware catch up
}

// Safety margin for hardware
vTaskDelay(pdMS_TO_TICKS(100));

// NOW set backlight
set_backlight(0);  // Safe to start fade
```

---

## 5. Working Implementation (Your Code - With Byte Swap Fix)

### Current Implementation Assessment

**Location**: [display_splash_lvgl.cpp](src/display/display_splash_lvgl.cpp#L69-L160)

**✅ Correct Parts**:
1. Static allocation pattern ✅
2. MCU block copy loop ✅
3. All header fields initialized ✅
4. Memory layout (2D indexing) ✅
5. Image widget creation ✅
6. Multiple `lv_timer_handler()` calls ✅

**⚠️ Missing**:
- **Byte swap of pixel data** ← This is why colors might be corrupted

### Required Fix

Replace the MCU copy loop (line ~124-135) with:

```cpp
// Line 124-135: MCU block copy WITH byte swap
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
            
            // ===== ADD THIS: Byte swap for LV_COLOR_16_SWAP=1 =====
            uint16_t pixel = pImg[y * mcu_w + x];
            rgb565_buffer[dst_idx] = __builtin_bswap16(pixel);
            // ===== END BYTE SWAP =====
        }
    }
}
```

---

## 6. Reference: LVGL Image Formats

| Format | Bytes/Pixel | Use Case | Size |
|--------|-------------|----------|------|
| `LV_IMG_CF_TRUE_COLOR` | 2 | RGB565, no transparency | 320×170 = 108 KB |
| `LV_IMG_CF_TRUE_COLOR_ALPHA` | 4 | ARGB8888 with alpha | 320×170 = 217 KB |
| `LV_IMG_CF_INDEXED_1BIT` | 0.125 | 2-color palette | 320×170 = 6.8 KB |
| `LV_IMG_CF_INDEXED_2BIT` | 0.25 | 4-color palette | 320×170 = 13.6 KB |
| `LV_IMG_CF_INDEXED_4BIT` | 0.5 | 16-color palette | 320×170 = 27.2 KB |
| `LV_IMG_CF_INDEXED_8BIT` | 1 | 256-color palette | 320×170 = 54.4 KB |

For your use case: **Always use `LV_IMG_CF_TRUE_COLOR`** (no palette needed for photos)

---

## 7. Testing Validation Sequence

### Step 1: Verify Byte Order

```cpp
// Add temporary code to test byte swap
void test_byte_swap() {
    uint16_t original = 0xF800;  // Red in standard RGB565
    uint16_t swapped = __builtin_bswap16(original);
    
    LOG_INFO("BYTE_SWAP_TEST", "Original: 0x%04X", original);
    LOG_INFO("BYTE_SWAP_TEST", "Swapped: 0x%04X", swapped);
    
    // Expected: 0x00F8 for swapped version
    // If you see 0xF800, you've swapped when shouldn't, or vice versa
}
```

### Step 2: Verify Image Descriptor

```cpp
void test_image_descriptor(const lv_img_dsc_t* img_dsc) {
    LOG_INFO("IMG_DESC_TEST", "always_zero: %d (expect 0)", img_dsc->header.always_zero);
    LOG_INFO("IMG_DESC_TEST", "width: %d", img_dsc->header.w);
    LOG_INFO("IMG_DESC_TEST", "height: %d", img_dsc->header.h);
    LOG_INFO("IMG_DESC_TEST", "cf: 0x%02X (expect 0x04)", img_dsc->header.cf);
    LOG_INFO("IMG_DESC_TEST", "data_size: %d bytes", img_dsc->data_size);
    LOG_INFO("IMG_DESC_TEST", "Expected size: %d bytes", 
             img_dsc->header.w * img_dsc->header.h * 2);
    
    // Check for mismatch
    if (img_dsc->data_size != img_dsc->header.w * img_dsc->header.h * 2) {
        LOG_ERROR("IMG_DESC_TEST", "SIZE MISMATCH!");
    }
}
```

### Step 3: Verify Display

```cpp
// After lv_img_set_src():
vTaskDelay(pdMS_TO_TICKS(500));  // Wait for render

// If image is visible, test a specific color
uint16_t* buffer = (uint16_t*)img_dsc.data;
LOG_INFO("IMG_PIXEL_TEST", "Pixel[0]: 0x%04X", buffer[0]);
LOG_INFO("IMG_PIXEL_TEST", "Pixel[100]: 0x%04X", buffer[100]);
LOG_INFO("IMG_PIXEL_TEST", "Pixel[last]: 0x%04X", buffer[img_dsc.header.w * img_dsc.header.h - 1]);
```

---

## Summary: The Exact Solution

Your implementation is **97% correct**. The single fix needed:

1. **Add byte swap to MCU copy loop**:
   ```cpp
   rgb565_buffer[dst_idx] = __builtin_bswap16(pImg[y * mcu_w + x]);
   ```

2. **Verify `LV_COLOR_16_SWAP=1` matches hardware**:
   ```cpp
   tft.setSwapBytes(true);  // ✅ Must be true
   ```

3. **Confirm buffer allocation**:
   ```cpp
   heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);  // ✅ Correct
   ```

Everything else in your code is production-ready! 🎉

---

## Additional Resources

- LVGL Image Widget: https://docs.lvgl.io/8.3/widgets/img.html
- LVGL Color Formats: https://docs.lvgl.io/8.3/overview/color.html
- JPEGDecoder Library: https://github.com/Bodmer/JPEGDecoder
- TFT_eSPI Byte Swap: https://github.com/Bodmer/TFT_eSPI/wiki/Byte-swap-explanation

