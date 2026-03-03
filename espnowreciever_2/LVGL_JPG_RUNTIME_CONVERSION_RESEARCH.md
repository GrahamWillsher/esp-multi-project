# LVGL 8.3 JPG Runtime Display Research

## March 2, 2026 - CRITICAL CORRECTION (Proper LVGL Implementation)

### FATAL FLAW IN PREVIOUS PLAN

The plan from earlier today **violates the core requirement**: all rendering must be via LVGL, with no TFT references.

**What was wrong:**
1. JPG converted to RGB565 binary, then **blitted directly via TFT** (`tft.pushImage()`)
   - Should be: JPG decoded → wrapped in `lv_image_dsc_t` → displayed via LVGL image object
2. Screen clearing used `tft.fillScreen(TFT_BLACK)`
   - Should be: LVGL screen object with black background
3. SOC/Power display still uses TFT
   - Should be: LVGL labels/widgets for text display

**This means:** Previous implementation was a TFT-based hack pretending to be LVGL. The white block issue will persist.

### Corrected execution plan (REAL LVGL approach)

#### Phase 1: JPG → LVGL Image Descriptor
1. Decode JPG from LittleFS to RGB565 buffer in PSRAM
2. Create `lv_image_dsc_t` structure with:
   - `header`: magic=0x12345678, w/h from JPG, cf=LV_COLOR_FORMAT_RGB565
   - `data`: pointer to RGB565 buffer
   - `data_size`: width × height × 2
3. Persist descriptor for LVGL use (not freed after blit)

#### Phase 2: Splash Screen via LVGL Objects
1. Create LVGL screen: `lv_obj_create(NULL)` with black background
2. Create LVGL image object: `lv_image_create(screen)` with descriptor from Phase 1
3. Position image centered
4. Animate via `lv_obj_set_style_opa()` with LVGL timer (fade 0→255, hold 2s, fade 255→0)
5. Clear screen after fade-out

#### Phase 3: Status Display via LVGL
- SOC and Power labels created via `lv_label_create()` instead of TFT drawing
- Positioned and styled via LVGL properties
- Updated by state machine using LVGL, not TFT

#### Phase 4: Remove all TFT references from startup
- No `tft.fillScreen()` in LVGL path
- No `tft.pushImage()` or direct TFT writing
- All rendering delegated to LVGL driver → TFT (via `lv_disp_drv_t`)

### Why this fixes the real problem

- **Proper abstraction**: All rendering goes through LVGL's display driver
- **No conflicts**: TFT is accessed only from LVGL driver, not from application code
- **Correct format**: JPG properly wrapped in LVGL descriptor, not raw RGB565 blob
- **LVGL animations**: Opacity fade uses LVGL built-in mechanisms, not raw PWM
- **Consistent architecture**: Splash, status display, and all other content use LVGL uniformly

### Next steps (correct order)

1. **Create LVGL image descriptor system** in new file: `src/display/lvgl_image_converter.cpp`
   - `JPG_to_lv_image_dsc()`: Decodes JPG, wraps in descriptor, returns persistent handle
   - Manages PSRAM buffer lifecycle

2. **Rewrite splash sequence** in `display_splash_lvgl.cpp`
   - Create LVGL screen with image and animation
   - Remove all `tft.*` calls
   - Use `lv_obj_set_style_opa()` for fade instead of PWM

3. **Create LVGL status display** for SOC/Power
   - Labels instead of TFT drawing
   - Position in corner/margins of existing LVGL content
   - Update via safe LVGL calls from state machine

4. **Validation checkpoints**
   - Confirm zero TFT references in splash code (except driver layer)
   - Confirm JPG descriptor persists and displays
   - Confirm opacity animation is smooth (LVGL controlled)
   - Confirm no white blocks (LVGL handles all rendering atomically)

---

## Research Date: March 1, 2026

This document contains comprehensive research on how to display JPG images in LVGL 8.3 by converting them at runtime to LVGL-compatible format.

---

## 1. LVGL IMAGE FORMAT REQUIREMENTS

### Image Descriptor Structure: `lv_image_dsc_t`

LVGL expects images in the form of an `lv_image_dsc_t` structure, which contains:

```c
typedef struct {
    lv_image_header_t header;
    uint32_t data_size;
    const void * data;
    const void * reserved;
} lv_image_dsc_t;
```

Where `lv_image_header_t` contains:
```c
typedef struct {
    uint32_t magic;          // LV_IMAGE_HEADER_MAGIC (0x12345678)
    uint16_t w;              // Width in pixels
    uint16_t h;              // Height in pixels
    uint32_t stride;         // Bytes per line (can include padding)
    lv_color_format_t cf;    // Color format
    uint8_t flags;           // Allocation flags
    uint32_t reserved_2;     // Reserved
} lv_image_header_t;
```

### Supported Color Formats for Runtime Conversion

**For LVGL 8.3, the most practical formats are:**

1. **LV_COLOR_FORMAT_RGB565** (2 bytes per pixel)
   - 5-bit red, 6-bit green, 5-bit blue
   - Compact and efficient
   - Requires byte order handling

2. **LV_COLOR_FORMAT_RGB888** (3 bytes per pixel)
   - 8-bit red, green, blue per channel
   - Better quality, uses more memory

3. **LV_COLOR_FORMAT_ARGB8888** (4 bytes per pixel)
   - Full color with alpha channel
   - Most memory intensive

### Critical Requirements

✓ **Image data must be allocated dynamically** (heap memory, not stack)
✓ **Stride calculation**: `stride = (width * bytes_per_pixel + padding) rounded to alignment`
✓ **Data pointer must remain valid** while the image is displayed
✓ **Magic field must be set** to `LV_IMAGE_HEADER_MAGIC` for LVGL to recognize it
✓ **Color format must match actual pixel data layout**

---

## 2. RUNTIME JPG CONVERSION PROCESS

### Two Decoder Libraries Available

#### A. **TinyJpgDec (TJPGD)** - Built into LVGL

**Pros:**
- Already integrated into LVGL
- Very small memory footprint (~4KB working buffer)
- Decodes in 8x8 pixel tiles

**Cons:**
- Decodes only 8x8 MCU blocks at a time
- Doesn't decode entire image to memory at once
- Not ideal for full image buffering

**Configuration:** Set `LV_USE_TJPGD 1` in `lv_conf.h`

#### B. **JPEGDecoder Library** - Arduino JPEGDecoder by Bodmer (Recommended for Runtime)

**Pros:**
- Designed for MCU/embedded systems
- Full image decoding to memory
- Supports multiple byte order modes
- Supports both `read()` and `readSwappedBytes()`
- Works with ESP32/ESP8266 SPIFFS, SD cards, and arrays

**Cons:**
- Requires additional library installation
- Higher memory usage than TJPGD
- Only supports baseline JPEG (no progressive)

**GitHub:** https://github.com/Bodmer/JPEGDecoder

### Step-by-Step Conversion Process

```c
// 1. INITIALIZE JPEGDECODER
#include <JPEGDecoder.h>

// Decode from LittleFS file
bool decoded = JpegDec.decodeFsFile("/path/to/image.jpg");

// OR from array
bool decoded = JpegDec.decodeArray(image_array, array_size);

// Check if successful
if (!decoded) {
    ESP_LOGE("JPEG", "Failed to decode JPG");
    return;
}

// 2. GET IMAGE DIMENSIONS
uint16_t jpg_width = JpegDec.width;
uint16_t jpg_height = JpegDec.height;

// 3. ALLOCATE BUFFER FOR CONVERTED DATA
// For RGB565 (2 bytes per pixel):
uint16_t bytes_per_pixel = 2;
uint32_t buffer_size = jpg_width * jpg_height * bytes_per_pixel;
uint8_t *image_buffer = (uint8_t *)ps_malloc(buffer_size);

if (!image_buffer) {
    ESP_LOGE("JPEG", "Failed to allocate %u bytes", buffer_size);
    JpegDec.abort();
    return;
}

// 4. READ MCU BLOCKS AND CONVERT
uint16_t mcu_w = JpegDec.MCUWidth;
uint16_t mcu_h = JpegDec.MCUHeight;

while (JpegDec.readSwappedBytes()) {  // Use readSwappedBytes() for correct byte order
    uint16_t *pImg = JpegDec.pImage;
    int mcu_x = JpegDec.MCUx * mcu_w;
    int mcu_y = JpegDec.MCUy * mcu_h;
    
    // Calculate actual block dimensions
    int win_w = (mcu_x + mcu_w <= jpg_width) ? mcu_w : jpg_width - mcu_x;
    int win_h = (mcu_y + mcu_h <= jpg_height) ? mcu_h : jpg_height - mcu_y;
    
    // Copy MCU block to appropriate location in buffer
    uint32_t dst_offset = (mcu_y * jpg_width + mcu_x) * bytes_per_pixel;
    uint16_t *dst = (uint16_t *)(image_buffer + dst_offset);
    
    // Copy each row of the MCU block
    for (int y = 0; y < win_h; y++) {
        uint16_t *src_row = pImg + (y * mcu_w);
        uint16_t *dst_row = dst + (y * jpg_width);
        memcpy(dst_row, src_row, win_w * bytes_per_pixel);
    }
}

// 5. CREATE LVGL IMAGE DESCRIPTOR
lv_image_dsc_t my_image = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.w = jpg_width,
    .header.h = jpg_height,
    .header.stride = jpg_width * bytes_per_pixel,
    .header.flags = 0,
    .data_size = buffer_size,
    .data = (const void *)image_buffer
};

// 6. DISPLAY THE IMAGE
lv_obj_t *img = lv_image_create(lv_screen_active());
lv_image_set_src(img, &my_image);

// 7. CLEANUP (when image is no longer needed)
free(image_buffer);
JpegDec.abort();
```

---

## 3. IMAGE DESCRIPTOR LIFETIME & MEMORY MANAGEMENT

### Critical Memory Considerations

**Descriptor Lifetime:**
- The `lv_image_dsc_t` can be either **static or on stack** as long as it's in scope
- The **data pointer** (pointing to pixel buffer) must remain valid during entire display lifetime
- LVGL keeps a reference to the descriptor but doesn't copy it

**Data Buffer Lifetime:**
- Must be **dynamically allocated** (heap)
- Should remain allocated **while image is displayed**
- Cannot be freed until image widget is destroyed or source is changed

**Safe Pattern:**

```c
// Global or persistent pointer
static uint8_t *g_image_buffer = NULL;
static lv_image_dsc_t g_image_descriptor = {0};

void load_jpg_image(const char *filename) {
    // Free previous image if exists
    if (g_image_buffer) {
        free(g_image_buffer);
        g_image_buffer = NULL;
    }
    
    // Decode and allocate
    bool decoded = JpegDec.decodeFsFile(filename);
    if (!decoded) return;
    
    uint32_t buffer_size = JpegDec.width * JpegDec.height * 2;
    g_image_buffer = (uint8_t *)ps_malloc(buffer_size);
    
    // ... populate buffer with MCU blocks ...
    
    // Set descriptor
    g_image_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_image_descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    g_image_descriptor.header.w = JpegDec.width;
    g_image_descriptor.header.h = JpegDec.height;
    g_image_descriptor.header.stride = JpegDec.width * 2;
    g_image_descriptor.data = (const void *)g_image_buffer;
    g_image_descriptor.data_size = buffer_size;
    
    // Display
    lv_obj_t *img = lv_image_create(lv_screen_active());
    lv_image_set_src(img, &g_image_descriptor);
}

void cleanup_image(void) {
    if (g_image_buffer) {
        free(g_image_buffer);
        g_image_buffer = NULL;
    }
    JpegDec.abort();
}
```

---

## 4. WORKING EXAMPLES FROM PROJECTS

### Real-World ESP32 + LVGL + JPG Pattern

Based on analysis of JPEGDecoder examples and LVGL integration:

#### Example 1: Basic File-Based Decoding

```cpp
#include <JPEGDecoder.h>
#include "lvgl.h"

void display_jpg_from_file(const char *filename) {
    // Decode JPG
    JpegDec.decodeFsFile(filename);
    
    uint16_t img_w = JpegDec.width;
    uint16_t img_h = JpegDec.height;
    uint32_t buf_size = img_w * img_h * 2;
    
    // Allocate PSRAM for large images
    uint16_t *image_data = (uint16_t *)ps_malloc(buf_size);
    if (!image_data) {
        JpegDec.abort();
        return;
    }
    
    // Read MCU blocks into buffer
    while (JpegDec.readSwappedBytes()) {
        uint16_t *mcu_data = JpegDec.pImage;
        int mcu_x = JpegDec.MCUx * JpegDec.MCUWidth;
        int mcu_y = JpegDec.MCUy * JpegDec.MCUHeight;
        
        // Blit to position
        for (int y = 0; y < JpegDec.MCUHeight; y++) {
            if (mcu_y + y >= img_h) break;
            for (int x = 0; x < JpegDec.MCUWidth; x++) {
                if (mcu_x + x >= img_w) break;
                uint32_t idx = (mcu_y + y) * img_w + (mcu_x + x);
                image_data[idx] = mcu_data[y * JpegDec.MCUWidth + x];
            }
        }
    }
    
    // Create LVGL image descriptor
    lv_image_dsc_t *img_dsc = (lv_image_dsc_t *)malloc(sizeof(lv_image_dsc_t));
    img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    img_dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    img_dsc->header.w = img_w;
    img_dsc->header.h = img_h;
    img_dsc->header.stride = img_w * 2;
    img_dsc->data = (const void *)image_data;
    img_dsc->data_size = buf_size;
    
    // Display
    lv_obj_t *img_obj = lv_image_create(lv_screen_active());
    lv_image_set_src(img_obj, img_dsc);
    
    // Store for cleanup
    g_current_image_buffer = (void *)image_data;
    g_current_image_dsc = img_dsc;
}
```

#### Example 2: Array-Based Decoding

```cpp
// Assuming image_array contains JPG data
extern const uint8_t my_image_jpg[];
extern const uint32_t my_image_jpg_len;

void display_jpg_from_array(void) {
    JpegDec.decodeArray(my_image_jpg, my_image_jpg_len);
    
    uint16_t img_w = JpegDec.width;
    uint16_t img_h = JpegDec.height;
    
    // Allocate RGB565 buffer
    uint16_t *buf = (uint16_t *)ps_malloc(img_w * img_h * 2);
    
    // Decode to buffer
    while (JpegDec.readSwappedBytes()) {
        uint16_t *src = JpegDec.pImage;
        uint32_t dst_idx = (JpegDec.MCUy * JpegDec.MCUHeight * img_w) +
                          (JpegDec.MCUx * JpegDec.MCUWidth);
        
        for (int y = 0; y < JpegDec.MCUHeight; y++) {
            uint32_t row_offset = dst_idx + (y * img_w);
            memcpy(&buf[row_offset], 
                   &src[y * JpegDec.MCUWidth], 
                   JpegDec.MCUWidth * 2);
        }
    }
    
    // Create descriptor and display
    static lv_image_dsc_t dsc;
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc.header.w = img_w;
    dsc.header.h = img_h;
    dsc.header.stride = img_w * 2;
    dsc.data = buf;
    dsc.data_size = img_w * img_h * 2;
    
    lv_image_set_src(lv_image_create(lv_screen_active()), &dsc);
}
```

---

## 5. BYTE ORDER HANDLING - CRITICAL!

### The RGB565 Byte Order Issue

This is the **most common failure point** when integrating JPEGDecoder with LVGL.

#### JPEGDecoder Output Formats

**`read()` method produces:**
```
MSB: R5 R4 R3 R2 R1 G5 G4 G3  |  G2 G1 G0 B4 B3 B2 B1 B0 :LSB
Big-endian 16-bit value
```

**`readSwappedBytes()` produces:**
```
LSB: G2 G1 G0 B4 B3 B2 B1 B0  |  MSB: R5 R4 R3 R2 R1 G5 G4 G3
Byte-swapped (little-endian on ESP32)
```

#### LVGL 8.3 Configuration

In your `lv_conf.h`:

```c
// Color depth
#define LV_COLOR_DEPTH 16  // 16-bit RGB565

// Byte swap setting (check your display requirements)
#define LV_COLOR_16_SWAP 0  // Set to 1 if display needs swapped bytes
```

#### Correct Usage Pattern

```c
// For most ESP32 displays with standard RGB565
if (LV_COLOR_16_SWAP == 0) {
    // Use readSwappedBytes() - produces correct byte order
    while (JpegDec.readSwappedBytes()) { ... }
} else {
    // Use read() - produces correct byte order for swapped systems
    while (JpegDec.read()) { ... }
}
```

#### Manual Conversion If Needed

```c
// If you need to swap bytes manually:
static inline uint16_t swap_bytes_rgb565(uint16_t pixel) {
    return ((pixel & 0xFF) << 8) | ((pixel >> 8) & 0xFF);
}

// When copying pixel data:
uint16_t rgb565_pixel = JpegDec.pImage[i];
buffer[i] = swap_bytes_rgb565(rgb565_pixel);
```

---

## 6. INTEGRATION CHECKLIST

### Before You Start

- [ ] LVGL 8.3 configured with `LV_COLOR_DEPTH 16`
- [ ] JPEGDecoder library installed from https://github.com/Bodmer/JPEGDecoder
- [ ] Sufficient PSRAM available on ESP32 (for full image buffering)
- [ ] File system (LittleFS/SPIFFS) configured if loading from files
- [ ] Display color format (RGB565, BGR565, etc.) identified

### Development Steps

1. **Verify JPEGDecoder Works**
   ```cpp
   // Test decode and read
   JpegDec.decodeFsFile("/test.jpg");
   while (JpegDec.readSwappedBytes()) {
       Serial.printf("MCU: %d,%d\n", JpegDec.MCUx, JpegDec.MCUy);
   }
   ```

2. **Test Memory Allocation**
   ```cpp
   uint32_t buffer_size = JpegDec.width * JpegDec.height * 2;
   uint16_t *buf = (uint16_t *)ps_malloc(buffer_size);
   if (!buf) {
       Serial.println("Memory allocation failed!");
   }
   ```

3. **Verify Pixel Data Integrity**
   - Decode to buffer
   - Display first few pixels in Serial output
   - Compare with expected colors

4. **Test LVGL Display**
   - Create image descriptor with correct magic/stride/format
   - Set image source
   - Check for visual distortions

### Debugging Tips

**If colors are wrong:**
- Check `LV_COLOR_16_SWAP` setting
- Try opposite byte order
- Verify JPEGDecoder `readSwappedBytes()` vs `read()`

**If image appears corrupted:**
- Check stride calculation: `stride = width * 2` for RGB565
- Verify MCU block positioning calculation
- Ensure buffer size is correct

**If crashes occur:**
- Buffer allocation failed (check free PSRAM)
- Descriptor/buffer lifetime issue (freed too early)
- Stack overflow from local arrays

---

## 7. COMPLETE WORKING IMPLEMENTATION TEMPLATE

```cpp
// ============================================================
// ESP32 + LVGL 8.3 + JPEGDecoder Runtime Image Display
// ============================================================

#include <JPEGDecoder.h>
#include "lvgl.h"

// Global image management
struct {
    uint8_t *buffer;
    lv_image_dsc_t descriptor;
    lv_obj_t *image_widget;
    bool is_allocated;
} g_image_manager = {0};

// Function prototypes
bool jpeg_load_and_display(const char *filename);
void jpeg_cleanup(void);

// ============================================================
bool jpeg_load_and_display(const char *filename) {
    // Clean up previous image
    jpeg_cleanup();
    
    // Decode JPG
    bool decoded = JpegDec.decodeFsFile(filename);
    if (!decoded) {
        ESP_LOGE("JPEG", "Failed to decode: %s", filename);
        return false;
    }
    
    uint16_t img_w = JpegDec.width;
    uint16_t img_h = JpegDec.height;
    uint32_t buffer_size = img_w * img_h * 2;  // RGB565 = 2 bytes/pixel
    
    ESP_LOGI("JPEG", "Decoded: %ux%u (%u bytes)", img_w, img_h, buffer_size);
    
    // Allocate buffer in PSRAM for large images
    g_image_manager.buffer = (uint8_t *)ps_malloc(buffer_size);
    if (!g_image_manager.buffer) {
        ESP_LOGE("JPEG", "Buffer allocation failed!");
        JpegDec.abort();
        return false;
    }
    
    // Read MCU blocks and assemble full image
    uint16_t *buf16 = (uint16_t *)g_image_manager.buffer;
    uint16_t mcu_w = JpegDec.MCUWidth;
    uint16_t mcu_h = JpegDec.MCUHeight;
    
    while (JpegDec.readSwappedBytes()) {
        uint16_t *mcu_data = JpegDec.pImage;
        int mcu_x = JpegDec.MCUx * mcu_w;
        int mcu_y = JpegDec.MCUy * mcu_h;
        
        // Calculate actual dimensions for edge MCUs
        uint16_t block_w = (mcu_x + mcu_w <= img_w) ? mcu_w : img_w - mcu_x;
        uint16_t block_h = (mcu_y + mcu_h <= img_h) ? mcu_h : img_h - mcu_y;
        
        // Copy MCU block to buffer
        for (uint16_t y = 0; y < block_h; y++) {
            uint32_t src_offset = y * mcu_w;
            uint32_t dst_offset = (mcu_y + y) * img_w + mcu_x;
            memcpy(&buf16[dst_offset], &mcu_data[src_offset], block_w * 2);
        }
    }
    
    // Create LVGL image descriptor
    g_image_manager.descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_image_manager.descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    g_image_manager.descriptor.header.w = img_w;
    g_image_manager.descriptor.header.h = img_h;
    g_image_manager.descriptor.header.stride = img_w * 2;
    g_image_manager.descriptor.header.flags = 0;
    g_image_manager.descriptor.data = (const void *)g_image_manager.buffer;
    g_image_manager.descriptor.data_size = buffer_size;
    
    // Create and set image widget
    lv_obj_t *parent = lv_screen_active();
    if (!g_image_manager.image_widget) {
        g_image_manager.image_widget = lv_image_create(parent);
    }
    
    lv_image_set_src(g_image_manager.image_widget, &g_image_manager.descriptor);
    lv_obj_center(g_image_manager.image_widget);
    
    g_image_manager.is_allocated = true;
    ESP_LOGI("JPEG", "Image displayed successfully");
    
    return true;
}

// ============================================================
void jpeg_cleanup(void) {
    if (g_image_manager.is_allocated && g_image_manager.buffer) {
        free(g_image_manager.buffer);
        g_image_manager.buffer = NULL;
        g_image_manager.is_allocated = false;
    }
    JpegDec.abort();
}

// ============================================================
// Example usage in your main code:
//
// void app_main(void) {
//     // Initialize LVGL display, etc...
//     
//     // Load and display JPG
//     jpeg_load_and_display("/images/photo.jpg");
//     
//     // ... LVGL runs normally ...
//     
//     // Cleanup when done
//     jpeg_cleanup();
// }
```

---

## 8. KEY TAKEAWAYS

✓ **Use `readSwappedBytes()`** for correct RGB565 byte order on ESP32
✓ **Allocate buffers dynamically** (heap/PSRAM) not on stack
✓ **Keep descriptor and buffer in scope** while image is displayed
✓ **Calculate stride correctly**: `width * bytes_per_pixel`
✓ **Handle MCU blocks individually** - don't expect full image at once
✓ **Set magic field** to `LV_IMAGE_HEADER_MAGIC`
✓ **Free resources** when image is no longer needed
✓ **Use `ps_malloc()`** on ESP32 for PSRAM allocation

---

## IMPLEMENTATION BLUEPRINT: Correct LVGL-Only Splash

### File Structure
```
src/display/
  lvgl_image_converter.h      ← NEW: Declares JPG→descriptor conversion
  lvgl_image_converter.cpp    ← NEW: Implements conversion with lifecycle
  display_splash_lvgl.h       ← (update)
  display_splash_lvgl.cpp     ← (complete rewrite - LVGL only)
  
src/state_machine.cpp         ← (update SOC/Power display to LVGL)
```

### 1. lvgl_image_converter.h
```cpp
#pragma once
#include <lv_types.h>

namespace Display {

// Persistent handle for converted JPG image
struct LvglImageHandle {
    lv_image_dsc_t descriptor;
    uint16_t* data_buffer;      // RGB565 pixel buffer in PSRAM
    uint32_t buffer_size;
    bool valid;
};

// Convert JPG file to LVGL image descriptor
// Returns persistent handle; caller must free with free_lvgl_image()
LvglImageHandle convert_jpg_to_lvgl_image(const char* filename);

// Free descriptor and buffer
void free_lvgl_image(LvglImageHandle& handle);

}
```

### 2. lvgl_image_converter.cpp (Pseudocode)
```cpp
LvglImageHandle Display::convert_jpg_to_lvgl_image(const char* filename) {
    LvglImageHandle handle = {0};
    
    // 1. Decode JPG to RGB565
    uint16_t* buffer = decode_jpg_to_rgb565(filename);  // Returns PSRAM buffer
    uint16_t w = JpegDec.width;
    uint16_t h = JpegDec.height;
    
    // 2. Create LVGL image descriptor
    handle.descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    handle.descriptor.header.w = w;
    handle.descriptor.header.h = h;
    handle.descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    handle.descriptor.header.stride = w * 2;  // RGB565 = 2 bytes/pixel
    handle.descriptor.data = buffer;
    handle.descriptor.data_size = w * h * 2;
    
    handle.data_buffer = buffer;
    handle.buffer_size = w * h * 2;
    handle.valid = true;
    
    return handle;
}
```

### 3. display_splash_lvgl.cpp (LVGL-Only Rewrite)
```cpp
void display_splash_lvgl() {
    LOG_INFO("LVGL_SPLASH", "=== Starting Splash via LVGL ===");
    
    // 1. Create root screen
    lv_obj_t* splash_screen = lv_obj_create(NULL);
    lv_obj_set_size(splash_screen, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(splash_screen, lv_color_black(), 0);
    
    // 2. Load JPG and wrap in LVGL descriptor
    auto img_handle = Display::convert_jpg_to_lvgl_image("/BatteryEmulator4_320x170.jpg");
    if (!img_handle.valid) {
        LOG_ERROR("LVGL_SPLASH", "Failed to convert JPG");
        lv_obj_delete(splash_screen);
        return;
    }
    
    // 3. Create LVGL image object
    lv_obj_t* img_obj = lv_image_create(splash_screen);
    lv_image_set_src(img_obj, &img_handle.descriptor);
    lv_obj_center(img_obj);
    
    // 4. Load screen and render
    lv_scr_load(splash_screen);
    for (int i = 0; i < 10; i++) {
        lv_timer_handler();
        smart_delay(5);
    }
    
    // 5. Animate opacity: 0 → 255 (fade in over 2s)
    lv_anim_t anim_in;
    lv_anim_init(&anim_in);
    lv_anim_set_var(&anim_in, splash_screen);
    lv_anim_set_values(&anim_in, 0, 255);
    lv_anim_set_time(&anim_in, 2000);
    lv_anim_set_exec_cb(&anim_in, [](void* var, int32_t v) {
        lv_obj_set_style_opa((lv_obj_t*)var, v, 0);
    });
    lv_anim_start(&anim_in);
    
    while (lv_anim_count_running() > 0) {
        lv_timer_handler();
        smart_delay(10);
    }
    
    // 6. Hold 2s
    smart_delay(2000);
    
    // 7. Animate opacity: 255 → 0 (fade out over 2s)
    lv_anim_t anim_out;
    lv_anim_init(&anim_out);
    lv_anim_set_var(&anim_out, splash_screen);
    lv_anim_set_values(&anim_out, 255, 0);
    lv_anim_set_time(&anim_out, 2000);
    lv_anim_set_exec_cb(&anim_out, [](void* var, int32_t v) {
        lv_obj_set_style_opa((lv_obj_t*)var, v, 0);
    });
    lv_anim_start(&anim_out);
    
    while (lv_anim_count_running() > 0) {
        lv_timer_handler();
        smart_delay(10);
    }
    
    // 8. Clean up
    lv_obj_delete(splash_screen);
    Display::free_lvgl_image(img_handle);
    
    LOG_INFO("LVGL_SPLASH", "=== Splash Complete ===");
}
```

### 4. SOC/Power Display (in state_machine.cpp, LVGL path)
```cpp
#ifdef USE_LVGL
// Create LVGL labels for status instead of TFT drawing
if (!status_label_initialized) {
    soc_label = lv_label_create(lv_scr_act());
    power_label = lv_label_create(lv_scr_act());
    lv_obj_align(soc_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_align(power_label, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    status_label_initialized = true;
}

// Update text via LVGL
char buf[32];
snprintf(buf, sizeof(buf), "SOC: %d%%", soc_percent);
lv_label_set_text(soc_label, buf);

snprintf(buf, sizeof(buf), "P: %dW", power_w);
lv_label_set_text(power_label, buf);
#endif
```

### 5. Validation Checklist
- [ ] `grep -r "tft\." src/display/display_splash_lvgl.cpp` returns nothing
- [ ] `grep -r "tft\." src/display/lvgl_image_converter.cpp` returns nothing
- [ ] JPG appears on screen (centered splash image)
- [ ] Fade-in smooth (0→255 over 2s)
- [ ] 2s pause with full brightness
- [ ] Fade-out smooth (255→0 over 2s)
- [ ] No white block, no flicker, no partial renders
- [ ] SOC/Power displayed via LVGL, not TFT

---

## 9. REFERENCES

- **LVGL 8.3 Documentation**: https://docs.lvgl.io/8.3/overview/image.html
- **LVGL Image Decoders**: https://docs.lvgl.io/8.3/libs/images.html
- **JPEGDecoder Library**: https://github.com/Bodmer/JPEGDecoder
- **LVGL Binary Image Format**: See `/docs/src/main-modules/images/adding_images.rst`
- **LVGL MCU Integration**: LVGL source `src/draw/lv_image_decoder.h`

---

## AUTHOR'S NOTES

This research reveals that **runtime JPG conversion to LVGL format is absolutely viable** on ESP32 with LVGL 8.3. The key is using JPEGDecoder's `readSwappedBytes()` method to get the correct byte order and assembling MCU blocks into a contiguous buffer.

**Estimated memory overhead:**
- JPG data: Original file size
- Working buffer (JPEGDecoder): ~4KB
- Full image buffer: `width × height × 2` bytes for RGB565

For a 320×240 QVGA image: ~150KB additional RAM
For a 480×320 HVGA image: ~307KB additional RAM

Modern ESP32 with PSRAM can easily handle multiple such images.

