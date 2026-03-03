# LVGL Image Display on ESP32: Working Implementation Research

**Date:** March 1, 2026  
**Platform:** ESP32-S3 with ST7789 display (320x170)  
**LVGL Version:** 8.3.11  
**Display Driver:** TFT_eSPI 2.5.43  
**Image Decoder:** JPEGDecoder  

---

## Executive Summary

Your issue: **Image widget created and positioned correctly, logs show success, but display remains BLACK during fade animation.**

The critical finding: **You already have a WORKING implementation of JPG display on this exact platform in `display_splash.cpp`!**

This document maps the exact pattern from your working code to understand why the LVGL version might not be displaying during fade.

---

## 1. Working Pattern: TFT_eSPI Direct (Current Implementation)

### Location
`espnowreciever_2/src/display/display_splash.cpp` - **This works perfectly**

### Exact Pattern Used

```cpp
// Load JPEG from LittleFS
void displaySplashJpeg2(const char* filename) {
    // Step 1: Verify file exists
    if (!LittleFS.exists(filename)) {
        LOG_ERROR("[JPEG2] File not found: %s", filename);
        return;
    }
    
    // Step 2: Open and load into RAM buffer
    File f = LittleFS.open(filename, "r");
    if (!f) return;
    
    size_t fileSize = f.size();
    uint8_t* buffer = (uint8_t*)malloc(fileSize);  // ← CRITICAL: Load entire file
    
    if (f.read(buffer, fileSize) != fileSize) {
        free(buffer);
        f.close();
        return;
    }
    f.close();
    
    // Step 3: Decode using JPEGDecoder
    if (!JpegDec.decodeArray(buffer, fileSize)) {  // ← Buffer in RAM, not file path
        LOG_ERROR("[JPEG2] Decode failed");
        free(buffer);
        return;
    }
    
    // Step 4: Render using MCU block streaming
    uint16_t* pImg;
    uint16_t mcu_w = JpegDec.MCUWidth;
    uint16_t mcu_h = JpegDec.MCUHeight;
    
    while (JpegDec.read()) {
        pImg = JpegDec.pImage;  // ← Get pixel data from current MCU
        
        int16_t mcu_x = JpegDec.MCUx * mcu_w + xOffset;
        int16_t mcu_y = JpegDec.MCUy * mcu_h + yOffset;
        
        tft.pushImage(mcu_x, mcu_y, win_w, win_h, pImg);  // ← Direct to TFT
    }
    
    free(buffer);  // ← Clean up only after decode completes
}
```

### Call Sequence (Works)
```
1. displaySplashWithFade()
2. → tft.fillScreen(TFT_BLACK)              // Clear display
3. → displaySplashScreenContent()
4. → displaySplashJpeg2("/BatteryEmulator4_320x170.jpg")
5. → fadeBacklight(255, 2000)              // PWM fade - image visible during fade!
6. → smart_delay(3000)
7. → fadeBacklight(0, 2000)                // Image stays visible during fade out
```

### Why This Works
- **Direct TFT driver control**: Uses `tft.pushImage()` directly to ST7789
- **MCU block streaming**: No full-image buffer required (just MCU at a time)
- **Immediate render**: Each MCU written to display as it's decoded
- **Buffer lifetime**: Only needed during decode phase, freed immediately after
- **No layer/cache issues**: Direct to framebuffer

---

## 2. The Black Screen Problem with LVGL 8.3.11

### What You're Likely Doing (Problematic Pattern)

```cpp
// Pattern that causes black screen:
lv_obj_t* img = lv_img_create(lv_scr_act());
lv_img_set_src(img, "S:/BatteryEmulator4_320x170.jpg");
lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

// Fade backlight...
fadeBacklight(255, 2000);  // ← Display stays BLACK
```

### Why This Fails During Fade

**Root Cause Chain:**

1. **Image Format Issue**
   - LVGL's `lv_img` expects pre-converted binary format (`.bin`) or embedded C arrays
   - Raw JPG files require custom image decoder registration
   - Without decoder: LVGL can't read the JPG file

2. **File System Access**
   - LVGL's image decoder needs registered file system driver
   - LittleFS file system drive must be configured with `lv_fs_drv_t`
   - If not registered: file paths fail silently

3. **Buffer Lifetime During Animation**
   - If using `LV_IMG_SRC_FILE`, LVGL tries to cache the image
   - During fade animation, display refresh requests image data repeatedly
   - If buffer is freed or becomes invalid → black screen

4. **Display Flush Timing**
   - LVGL's `lv_disp_flush_cb_t` must be called during fade
   - Animation task calls `lv_task_handler()` which triggers redraws
   - If image data unavailable during redraw → corruption or black

---

## 3. LVGL 8.3.11 Correct Pattern (If You Must Use LVGL)

### Option A: Pre-Convert Image to Binary Format

**Step 1: Convert JPG to LVGL Binary**
```
Use: https://lvgl.io/tools/imageconverter
- Upload: /BatteryEmulator4_320x170.jpg
- Output Format: Binary (Raw)
- Color Format: RGB565 Swap (for LV_COLOR_16_SWAP=1)
- Download: BatteryEmulator4_320x170.bin
- Store in: /BatteryEmulator4_320x170.bin (LittleFS)
```

**Step 2: Use in LVGL with File System Driver**
```cpp
// Register LittleFS as file system
static lv_fs_drv_t fs_drv;
lv_fs_drv_init(&fs_drv);
fs_drv.letter = 'S';
fs_drv.cache_size = 0;
fs_drv.open_cb = littlefs_open;
fs_drv.close_cb = littlefs_close;
fs_drv.read_cb = littlefs_read;
fs_drv.seek_cb = littlefs_seek;
lv_fs_drv_register(&fs_drv);

// Now this works:
lv_obj_t* img = lv_img_create(lv_scr_act());
lv_img_set_src(img, "S:/BatteryEmulator4_320x170.bin");
```

### Option B: Register Custom JPG Decoder (Complex)

**Step 1: Create Image Decoder**
```cpp
static lv_res_t decoder_info(lv_img_decoder_t* decoder, const void* src, 
                              lv_img_header_t* header) {
    if (/* src is JPG file */) {
        // Read JPG header to get dimensions
        header->cf = LV_IMG_CF_RAW;
        header->w = 320;
        header->h = 170;
        return LV_RES_OK;
    }
    return LV_RES_INV;
}

static lv_res_t decoder_open(lv_img_decoder_t* decoder, 
                              lv_img_decoder_dsc_t* dsc) {
    // Decode entire JPG into RAM buffer
    // Set dsc->img_data = decoded_buffer
    // Return LV_RES_OK
}

static void decoder_close(lv_img_decoder_t* decoder, 
                          lv_img_decoder_dsc_t* dsc) {
    // Free decoded buffer
}

// Register
lv_img_decoder_t* dec = lv_img_decoder_create();
lv_img_decoder_set_info_cb(dec, decoder_info);
lv_img_decoder_set_open_cb(dec, decoder_open);
lv_img_decoder_set_close_cb(dec, decoder_close);
```

**Problem:** Decoding entire JPG into RAM at once = memory pressure on ESP32

---

## 4. Key Configuration Requirements

### From Your Workspace (espnowreciever_2)

**`platformio.ini` typical ESP32-S3 setup:**
```ini
[env:receiver_lvgl]
platform = espressif32
board = esp32-s3-devkitc-1
build_flags =
    -DLVGL_CONF_INCLUDE_SIMPLE
    -DLV_CONF_INCLUDE_SIMPLE
    -DLV_COLOR_DEPTH=16
    -DLV_COLOR_16_SWAP=1
```

**Critical LVGL Configuration (`lv_conf.h`):**
```c
#define LV_COLOR_DEPTH          16      // 16-bit color
#define LV_COLOR_16_SWAP        1       // RGB565 byte swap for TFT_eSPI
#define LV_USE_IMAGE            1       // Enable image widget
#define LV_IMG_CACHE_DEF_SIZE   10      // Increase for multiple images
#define LV_FS_LITTLEFS_PATH     "/"     // LittleFS mount point
#define LV_USE_FS_LITTLEFS      1       // Enable LittleFS file system
```

**TFT_eSPI Configuration (`User_Setup.h` or pin defines):**
```cpp
#define TFT_MISO        13
#define TFT_MOSI        11
#define TFT_SCLK        12
#define TFT_CS          10
#define TFT_DC          9
#define TFT_RST         8

#define TFT_WIDTH       320
#define TFT_HEIGHT      170

#define LOAD_GLCD       // Load GLCD font
#define LOAD_FONT2      // Load Font 2
```

---

## 5. Display Flush Callback (Critical for Animation)

Your LVGL initialization likely has:

```cpp
void lv_display_flush(lv_disp_drv_t* disp, const lv_area_t* area, 
                       lv_color_t* color_p) {
    // TFT_eSPI has native LVGL support:
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, 
                       area->x2 - area->x1 + 1, 
                       area->y2 - area->y1 + 1);
    tft.pushColors((uint16_t*)color_p, 
                    (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), 
                    true);
    tft.endWrite();
    
    // CRITICAL: Must flush before returning
    lv_disp_flush_ready(disp);
}
```

**Problem During Fade:**
- If image data unavailable when flush called → draws garbage/black
- Timing mismatch between animation task and image availability

---

## 6. Why Direct TFT Approach Works (Your Current Code)

**displaySplashJpeg2 Success Factors:**

| Factor | Direct TFT | LVGL |
|--------|-----------|------|
| **Decode Method** | JPEGDecoder.decodeArray() | LVGL decoder (if registered) |
| **Render Method** | tft.pushImage() directly | lv_disp_flush() callback |
| **Buffer Lifetime** | Controlled - freed after decode | Managed by LVGL cache system |
| **Animation Support** | PWM directly on `ledcWrite()` | LVGL animation system |
| **Black Screen Risk** | Low - direct control | High - async buffer issues |
| **Memory Usage** | One MCU at a time | Full image or per-callback |

---

## 7. Recommended Solution

### Best Approach: Keep Direct TFT for Splash, Use LVGL for Main UI

```cpp
// In main setup:

// === SPLASH PHASE (TFT Direct) ===
displaySplashWithFade();  // Uses displaySplashJpeg2() - works perfectly

// === MAIN UI PHASE (LVGL) ===
lv_init();
lv_disp_draw_buf_init(&draw_buf, ...);
lv_disp_drv_init(&disp_drv);
disp_drv.hor_res = 320;
disp_drv.ver_res = 170;
disp_drv.flush_cb = lv_display_flush;
lv_disp_drv_register(&disp_drv);

// Use LVGL for UI elements (buttons, labels, etc.)
// Use TFT_eSPI direct for images if needed
```

### If You Must Use LVGL for Splash:

1. **Convert image:**
   - Use https://lvgl.io/tools/imageconverter
   - Output: Binary, RGB565 Swap
   - Save as `.bin` file to LittleFS

2. **Register file system:**
   ```cpp
   // In lv_conf.h or driver setup
   #define LV_USE_FS_LITTLEFS 1
   #define LV_FS_LITTLEFS_PATH "/"
   ```

3. **Set image source:**
   ```cpp
   lv_img_set_src(img, "S:/image.bin");  // Binary file, not JPG
   ```

4. **Increase image cache:**
   ```cpp
   #define LV_IMG_CACHE_DEF_SIZE 1  // Minimum for animations
   ```

---

## 8. Common Pitfalls to Avoid

| Pitfall | Symptom | Solution |
|---------|---------|----------|
| **No file system driver registered** | LVGL ignores "S:/" paths | Call `lv_fs_drv_register()` |
| **JPG without decoder** | Image won't load | Convert to .bin or implement decoder |
| **Buffer freed too early** | Black screen during animation | Keep buffer alive until `decoder_close()` |
| **LV_COLOR_16_SWAP mismatch** | Color corruption | Ensure both TFT_eSPI and LVGL use same setting |
| **Image cache too small** | Reloads every frame | Increase `LV_IMG_CACHE_DEF_SIZE` |
| **Display flush not called** | Screen freezes/black | Add `lv_disp_flush_ready(disp)` |
| **Block during fade animation** | Stutter/pause | Ensure `lv_task_handler()` called regularly |

---

## 9. ESP-IDF/Arduino LVGL Integration Notes

From [LVGL PR #2200](https://github.com/lvgl/lvgl/pull/2200) - Arduino ESP32 compatibility:

```cpp
// Arduino compatibility requires:
#if defined(__has_include)
  #if __has_include("lv_conf.h")
   #ifndef LV_CONF_INCLUDE_SIMPLE
    #define LV_CONF_INCLUDE_SIMPLE
   #endif
  #endif
#endif

// Then include normally:
#include "lvgl.h"
```

This is already handled in modern LVGL 8.3.11 distributions.

---

## 10. Image Decoder Reference Architecture

From LVGL 8.3 documentation structure:

**LVGL expects image decoders in this order:**
1. **Built-in formats** (LV_IMG_CF_TRUE_COLOR, etc.) - handled automatically
2. **Raw formats** (LV_IMG_CF_RAW*) - require decoder
3. **User formats** (LV_IMG_CF_USER_ENCODED*) - require custom decoder

**For RAW JPG format:**
- File extension: `.jpg` (will match in decoder_info)
- Color format returned: `LV_IMG_CF_RAW_ALPHA` or `LV_IMG_CF_RAW`
- Decoder open: Must fully decode into RAM buffer
- Decoder read: Optional (not used for RAW files)

---

## 11. Action Items for Your Fade Animation Issue

### Immediate Diagnosis
1. **Confirm current behavior:**
   - Is splash screen (TFT direct) displaying correctly? → YES
   - Is LVGL image widget showing anything? → NO (black)
   - Does image load without fade? → Test this

2. **Check file system registration:**
   ```cpp
   // Add debug in setup
   lv_fs_dir_t dir;
   if (lv_fs_dir_open(&dir, "S:/") == LV_RES_OK) {
       ESP_LOGI("LVGL", "File system S: mounted");
   } else {
       ESP_LOGI("LVGL", "File system S: NOT registered!");
   }
   ```

3. **Check image decoder:**
   ```cpp
   lv_img_decoder_t* dec = lv_img_decoder_get_next(NULL);
   int decoder_count = 0;
   while (dec) {
       decoder_count++;
       dec = lv_img_decoder_get_next(dec);
   }
   ESP_LOGI("LVGL", "Registered decoders: %d", decoder_count);
   ```

### Fix Implementation
- If file system not registered: Add `lv_fs_drv_register()`
- If JPG not supported: Convert to `.bin` or implement decoder
- If black during animation: Increase `LV_IMG_CACHE_DEF_SIZE`

---

## 12. References

**Working Code in Your Workspace:**
- [display_splash.cpp](../../espnowreciever_2/src/display/display_splash.cpp) - TFT_eSPI direct implementation

**LVGL Official Documentation:**
- Image widget: https://docs.lvgl.io/8.3/widgets/core/img.html
- Image decoder: https://docs.lvgl.io/8.3/overview/image.html#image-decoder
- File system: https://docs.lvgl.io/8.3/overview/file-system.html

**Arduino ESP32 LVGL Integration:**
- LVGL Arduino compatibility: https://github.com/lvgl/lvgl/pull/2200
- ESP32 port: https://github.com/lvgl/lv_port_esp32

**Image Conversion:**
- LVGL Image Converter: https://lvgl.io/tools/imageconverter
- JPEGDecoder: https://github.com/Bodmer/JPEGDecoder

---

## Conclusion

**Your working splash screen code proves your hardware is capable.** The issue is likely:

1. **LVGL-specific file system or decoder not configured**
2. **Image buffer lifetime during animation**
3. **LV_COLOR_16_SWAP mismatch between TFT_eSPI and LVGL**

**Quick Win:** Replace LVGL image display with direct TFT call (like your splash) during fade sequence. Use LVGL for static UI only.

**Proper Solution:** Configure LVGL's file system driver and use pre-converted `.bin` images, or implement JPG decoder and ensure buffer stays valid during animation.
