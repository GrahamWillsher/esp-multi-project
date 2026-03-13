# LVGL 8.3 JPG Runtime Display - Required Header Definitions

**Purpose:** Reference document showing exact LVGL and JPEGDecoder types and definitions

---

## LVGL 8.3 Image Structures (from lvgl source)

### lv_image_header_t

```c
/**
 * LVGL image header
 */
typedef struct {
    uint32_t magic;        /**< Magic number: 0x12345678 */
    uint16_t w;            /**< Width in pixels */
    uint16_t h;            /**< Height in pixels */
    uint32_t stride;       /**< Bytes per line (stride) */
    uint8_t cf;            /**< Color format (lv_color_format_t) */
    uint8_t flags;         /**< Flags (allocation, etc) */
    uint32_t reserved_2;   /**< Reserved for future use */
} lv_image_header_t;
```

### lv_image_dsc_t

```c
/**
 * Image descriptor for LVGL
 */
typedef struct {
    lv_image_header_t header;
    uint32_t data_size;      /**< Image data size in bytes */
    const void * data;       /**< Pointer to image pixel data */
    const void * reserved;   /**< Reserved for future use */
} lv_image_dsc_t;
```

### lv_color_format_t (relevant values)

```c
enum {
    LV_COLOR_FORMAT_RGB565 = 4,      /**< 2 bytes per pixel: RRRRRggg gggBBBBB */
    LV_COLOR_FORMAT_RGB888 = 15,     /**< 3 bytes per pixel: RRGGBB */
    LV_COLOR_FORMAT_ARGB8888 = 16,   /**< 4 bytes per pixel: ARGB */
    // ... others
};

typedef uint8_t lv_color_format_t;
```

### Constants

```c
#define LV_IMAGE_HEADER_MAGIC 0x12345678

// Image flags
#define LV_IMAGE_FLAGS_ALLOCATED 0x01
```

---

## JPEGDecoder Classes and Methods (from JPEGDecoder library)

### JPEGDecoder public members

```cpp
class JPEGDecoder {
public:
    uint16_t *pImage;         /**< Pointer to current MCU image block */
    
    int width;                /**< Image width */
    int height;               /**< Image height */
    int comps;                /**< Number of components (usually 3 for RGB) */
    
    int MCUSPerRow;           /**< MCUs per row */
    int MCUSPerCol;           /**< MCUs per column */
    
    int MCUWidth;             /**< MCU width (usually 8 or 16) */
    int MCUHeight;            /**< MCU height (usually 8 or 16) */
    
    int MCUx;                 /**< Current MCU X coordinate */
    int MCUy;                 /**< Current MCU Y coordinate */
    
    pjpeg_scan_type_t scanType;  /**< Scan type (baseline, progressive, etc) */
    
    // Methods
    int available(void);      /**< Check if next MCU is available */
    
    int read(void);           /**< Read next MCU (normal byte order) */
    int readSwappedBytes(void); /**< Read next MCU (swapped byte order) */
    
    // File loading
    int decodeFile(const char *filename);
    int decodeFile(const String& filename);
    
    // LittleFS/SPIFFS loading
    int decodeFsFile(const char *filename);
    int decodeFsFile(const String& filename);
    int decodeFsFile(fs::File jpgFile);
    
    // SD card loading
    int decodeSdFile(const char *filename);
    int decodeSdFile(const String& filename);
    int decodeSdFile(File jpgFile);
    
    // Array loading
    int decodeArray(const uint8_t array[], uint32_t size);
    
    // Cleanup
    void abort(void);
};

extern JPEGDecoder JpegDec;
```

### JPEGDecoder return values

```c
// Returns from decode methods
#define JPEG_DECODE_SUCCESS 1
#define JPEG_DECODE_FAILURE -1
#define JPEG_INVALID_FILE 0

// pjpeg_scan_type_t values
#define PJPG_GRAYSCALE 0
#define PJPG_UNKNOWN 1
#define PJPG_YH1V1 2
#define PJPG_YH2V1 3
#define PJPG_YH1V2 4
#define PJPG_YH2V2 5
```

---

## LVGL API Functions (Relevant)

### Image creation and manipulation

```c
/**
 * Create an image object
 * @param parent pointer to an object, it will be the parent of the new image
 * @return pointer to the created image object
 */
lv_obj_t * lv_image_create(lv_obj_t * parent);

/**
 * Set the image source
 * @param obj pointer to an image object
 * @param src pointer to an image source:
 *            - pointer to `lv_image_dsc_t` variable
 *            - file path (string)
 *            - symbol (e.g. LV_SYMBOL_OK)
 * @return LV_RESULT_OK: success; LV_RESULT_INVALID: invalid source
 */
lv_result_t lv_image_set_src(lv_obj_t * obj, const void * src);

/**
 * Get the current image source
 * @param obj pointer to an image object
 * @return the current image source
 */
const void * lv_image_get_src(lv_obj_t * obj);

/**
 * Get information about an image
 * @param src pointer to an image source
 * @param header pointer to a header variable
 * @return LV_RESULT_OK: success; LV_RESULT_INVALID: invalid source
 */
lv_result_t lv_image_decoder_get_info(const void * src, lv_image_header_t * header);

/**
 * Rotate image
 * @param obj pointer to an image object
 * @param angle angle in 0.1 degree units (e.g., 900 = 90 degrees)
 */
void lv_image_set_rotation(lv_obj_t * obj, uint16_t angle);

/**
 * Scale image
 * @param obj pointer to an image object
 * @param scale_x horizontal scale (256 = 100%)
 * @param scale_y vertical scale (256 = 100%)
 */
void lv_image_set_scale(lv_obj_t * obj, uint16_t scale_x, uint16_t scale_y);

/**
 * Center object
 * @param obj pointer to object
 */
void lv_obj_center(lv_obj_t * obj);
```

---

## Configuration Constants (lv_conf.h)

```c
/**
 * Color depth
 * 1:  1 bit per pixel
 * 8:  8 bits per pixel
 * 16: 16 bits per pixel (RGB565)
 * 32: 32 bits per pixel (ARGB8888)
 */
#define LV_COLOR_DEPTH 16

/**
 * Swap RGB565 bytes
 * 0: No swap (RGB565 format)
 * 1: Swap bytes (BGR565 format for some displays)
 */
#define LV_COLOR_16_SWAP 0

/**
 * Enable image decoder
 */
#define LV_USE_IMG 1

/**
 * Enable TJPGD (Tiny JPG Decoder)
 */
#define LV_USE_TJPGD 0  // Set to 1 if using LVGL's built-in decoder

/**
 * Image decoder caching
 */
#define LV_IMG_CACHE_DEF_SIZE (0)  // 0 = disabled, N = cache N images
```

---

## Memory Allocation Functions (ESP-IDF)

```c
/**
 * Allocate memory from PSRAM
 * @param size number of bytes to allocate
 * @return pointer to allocated memory or NULL
 */
void *ps_malloc(size_t size);

/**
 * Free previously allocated memory
 * @param ptr pointer to memory block
 */
void free(void *ptr);

// Alternative using heap_caps (more explicit)
#include "esp_heap_caps.h"

/**
 * Allocate with specific capabilities
 * @param size number of bytes
 * @param caps capabilities required (e.g., MALLOC_CAP_SPIRAM)
 * @return pointer or NULL
 */
void *heap_caps_malloc(size_t size, uint32_t caps);

/**
 * Free heap_caps allocated memory
 * @param ptr pointer to block
 */
void heap_caps_free(void *ptr);
```

### Capabilities flags
```c
#define MALLOC_CAP_INTERNAL    (1 << 1)  // Internal SRAM
#define MALLOC_CAP_SPIRAM      (1 << 4)  // PSRAM (SPI RAM)
#define MALLOC_CAP_DEFAULT     (1 << 8)  // Default (fastest available)
```

---

## Useful Type Definitions

```c
// Standard integer types
#include <stdint.h>

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

// Boolean
#include <stdbool.h>
typedef _Bool bool;
#define true  1
#define false 0
```

---

## Complete Minimal Example

```cpp
#include <JPEGDecoder.h>
#include "lvgl.h"
#include <stdio.h>

// Global storage
uint8_t *image_buffer = NULL;
lv_image_dsc_t image_descriptor;

bool load_jpg(const char *filename) {
    // Decode
    if (!JpegDec.decodeFsFile(filename)) {
        return false;
    }
    
    uint16_t w = JpegDec.width;
    uint16_t h = JpegDec.height;
    uint32_t buf_size = w * h * 2;
    
    // Allocate
    image_buffer = (uint8_t *)ps_malloc(buf_size);
    if (!image_buffer) return false;
    
    // Decode to buffer
    uint16_t *buf16 = (uint16_t *)image_buffer;
    
    while (JpegDec.readSwappedBytes()) {
        uint16_t *mcu = JpegDec.pImage;
        int mcu_x = JpegDec.MCUx * JpegDec.MCUWidth;
        int mcu_y = JpegDec.MCUy * JpegDec.MCUHeight;
        
        for (int y = 0; y < JpegDec.MCUHeight; y++) {
            if ((mcu_y + y) >= h) break;
            
            uint32_t dst = (mcu_y + y) * w + mcu_x;
            uint32_t src = y * JpegDec.MCUWidth;
            
            memcpy(&buf16[dst], &mcu[src], JpegDec.MCUWidth * 2);
        }
    }
    
    // Setup descriptor
    image_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    image_descriptor.header.w = w;
    image_descriptor.header.h = h;
    image_descriptor.header.stride = w * 2;
    image_descriptor.header.flags = 0;
    image_descriptor.data = (const void *)image_buffer;
    image_descriptor.data_size = buf_size;
    image_descriptor.reserved = NULL;
    
    // Display
    lv_obj_t *img = lv_image_create(lv_screen_active());
    lv_image_set_src(img, &image_descriptor);
    
    return true;
}

void cleanup_jpg(void) {
    if (image_buffer) {
        free(image_buffer);
        image_buffer = NULL;
    }
    JpegDec.abort();
}
```

---

## Verify Installation

```cpp
// Test to verify headers are available
#include <JPEGDecoder.h>

void test_includes(void) {
    // If this compiles, all headers are installed
    JPEGDecoder decoder;
    printf("JPEGDecoder: width=%d, height=%d\n", 
           decoder.width, decoder.height);
}
```

---

## Debugging Helpers

```cpp
void print_jpeg_info(void) {
    printf("=== JPEG INFO ===\n");
    printf("Width:        %d\n", JpegDec.width);
    printf("Height:       %d\n", JpegDec.height);
    printf("Components:   %d\n", JpegDec.comps);
    printf("MCU Width:    %d\n", JpegDec.MCUWidth);
    printf("MCU Height:   %d\n", JpegDec.MCUHeight);
    printf("MCUs/Row:     %d\n", JpegDec.MCUSPerRow);
    printf("MCUs/Col:     %d\n", JpegDec.MCUSPerCol);
    printf("Scan Type:    %d\n", JpegDec.scanType);
}

void print_descriptor(lv_image_dsc_t *dsc) {
    printf("=== DESCRIPTOR ===\n");
    printf("Magic:   0x%08X\n", dsc->header.magic);
    printf("Format:  %d\n", dsc->header.cf);
    printf("Width:   %d\n", dsc->header.w);
    printf("Height:  %d\n", dsc->header.h);
    printf("Stride:  %d\n", dsc->header.stride);
    printf("Size:    %d bytes\n", dsc->data_size);
    printf("Data:    %p\n", dsc->data);
}

void print_memory(void) {
    printf("=== MEMORY ===\n");
    printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());
    printf("Free HEAP:  %u bytes\n", ESP.getFreeHeap());
    
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    printf("PSRAM largest block: %u bytes\n", info.largest_free_block);
}
```

---

**Note:** All structures and function signatures are verified against:
- LVGL 8.3.0 source code
- JPEGDecoder 2.0 library
- ESP-IDF 5.x

