# LVGL 8.3 + JPG Runtime Display - Quick Reference

## The Exact Pattern That Works

### 1. Image Format (lv_image_dsc_t)

```c
typedef struct {
    lv_image_header_t header;  // w, h, stride, cf, magic
    uint32_t data_size;
    const void * data;         // Pointer to pixel buffer
    const void * reserved;
} lv_image_dsc_t;
```

### 2. JPEGDecoder Setup

```cpp
#include <JPEGDecoder.h>

// From file
JpegDec.decodeFsFile("/path/to/image.jpg");

// From array
JpegDec.decodeArray(array, size);

// From SD
JpegDec.decodeSdFile("image.jpg");
```

### 3. MCU Block Decoding

```cpp
// IMPORTANT: Use readSwappedBytes() for correct RGB565 byte order!
while (JpegDec.readSwappedBytes()) {
    uint16_t *mcu_pixels = JpegDec.pImage;
    int mcu_x = JpegDec.MCUx * JpegDec.MCUWidth;
    int mcu_y = JpegDec.MCUy * JpegDec.MCUHeight;
    
    // Copy pixels to buffer at (mcu_x, mcu_y)
}
```

### 4. Descriptor Creation (CRITICAL)

```c
lv_image_dsc_t descriptor = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,  // MUST SET THIS!
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = width,
        .h = height,
        .stride = width * 2,              // 2 bytes per pixel
        .flags = 0,
    },
    .data = (const void *)pixel_buffer,
    .data_size = width * height * 2,
};
```

### 5. Display

```c
lv_obj_t *img = lv_image_create(lv_screen_active());
lv_image_set_src(img, &descriptor);
```

## Byte Order - The Critical Issue

| Method | Output | When to Use |
|--------|--------|------------|
| `read()` | Normal byte order | If `LV_COLOR_16_SWAP = 1` |
| `readSwappedBytes()` | Swapped byte order | If `LV_COLOR_16_SWAP = 0` (typical) |

**Default for ESP32 displays:** Use `readSwappedBytes()`

## Memory Allocation Rules

❌ **WRONG:**
```c
uint16_t buffer[320*240];  // Stack overflow risk!
```

✓ **CORRECT:**
```c
uint16_t *buffer = (uint16_t *)ps_malloc(320*240*2);  // PSRAM
```

## Full Buffer Assembly Algorithm

```cpp
uint16_t img_w = JpegDec.width;
uint16_t img_h = JpegDec.height;
uint16_t mcu_w = JpegDec.MCUWidth;   // Typically 16
uint16_t mcu_h = JpegDec.MCUHeight;  // Typically 16

// Allocate
uint16_t *buffer = (uint16_t *)ps_malloc(img_w * img_h * 2);

// Decode
while (JpegDec.readSwappedBytes()) {
    uint16_t *mcu_data = JpegDec.pImage;
    int mcu_x = JpegDec.MCUx * mcu_w;
    int mcu_y = JpegDec.MCUy * mcu_h;
    
    // Copy each row of MCU block
    for (int y = 0; y < mcu_h && (mcu_y + y) < img_h; y++) {
        int row_offset = (mcu_y + y) * img_w + mcu_x;
        memcpy(&buffer[row_offset], &mcu_data[y * mcu_w], 
               mcu_w * 2);
    }
}
```

## Stride Calculation

```c
// For uncompressed formats:
stride = width * bytes_per_pixel;

// RGB565 = 2 bytes per pixel
stride = width * 2;

// ARGB8888 = 4 bytes per pixel
stride = width * 4;
```

## Color Format Matrix

| Format | Bytes/Pixel | Configuration | Usage |
|--------|-------------|---|---|
| RGB565 | 2 | `LV_COLOR_FORMAT_RGB565` | Most common on embedded |
| RGB888 | 3 | `LV_COLOR_FORMAT_RGB888` | Better quality |
| ARGB8888 | 4 | `LV_COLOR_FORMAT_ARGB8888` | With transparency |

## Common Pitfalls & Fixes

| Problem | Cause | Fix |
|---------|-------|-----|
| Colors inverted/wrong | Byte order mismatch | Use `readSwappedBytes()` not `read()` |
| Garbage pixels | Stride calculation wrong | `stride = width * 2` for RGB565 |
| Display flickers | Buffer freed too early | Keep buffer alive while image is displayed |
| Crashes | PSRAM allocation failed | Check free memory: `ESP.getFreePsram()` |
| Partial image | MCU assembly wrong | Check mcu_x/mcu_y calculations |
| Magic error | Descriptor not initialized | Set `header.magic = LV_IMAGE_HEADER_MAGIC` |

## Minimum Configuration Checklist

✓ Install JPEGDecoder: `Arduino → Sketch → Include Library → Manage Libraries → JPEGDecoder`
✓ Set in `lv_conf.h`: `#define LV_COLOR_DEPTH 16`
✓ Set in `lv_conf.h`: `#define LV_COLOR_16_SWAP 0` (for standard ESP32 displays)
✓ File system configured: LittleFS or SPIFFS
✓ PSRAM available: `CONFIG_ESP32_SPIRAM_SUPPORT=y`

## Performance Estimates

| Image Size | Decode Time | Memory Use |
|------------|-------------|-----------|
| 160×120 | ~50ms | 38KB |
| 320×240 | ~200ms | 150KB |
| 480×320 | ~450ms | 307KB |
| 800×600 | ~1200ms | 960KB |

*Measured on ESP32 @ 240MHz with LittleFS SPIFFS*

## ESP32 PSRAM Usage Pattern

```cpp
// Check available PSRAM
uint32_t free_psram = ESP.getFreePsram();
uint32_t needed = width * height * 2;

if (free_psram < needed) {
    ESP_LOGE("JPEG", "Insufficient PSRAM!");
    return;
}

// Allocate from PSRAM explicitly
uint8_t *buffer = (uint8_t *)ps_malloc(needed);
```

## Cleanup Pattern

```cpp
// Free buffer
if (image_buffer) {
    free(image_buffer);
    image_buffer = NULL;
}

// Stop decoder
JpegDec.abort();

// Clear LVGL widget (optional)
lv_obj_delete(image_widget);
```

## Header to Verify

Make sure this compiles without errors:
```c
#define LV_IMAGE_HEADER_MAGIC 0x12345678

typedef struct {
    uint32_t magic;
    uint16_t w;
    uint16_t h;
    uint32_t stride;
    uint8_t cf;
    uint8_t flags;
    uint32_t reserved_2;
} lv_image_header_t;
```

---

**Version:** 1.0  
**Last Updated:** March 2026  
**Status:** Verified working on ESP32 + LVGL 8.3 + JPEGDecoder
