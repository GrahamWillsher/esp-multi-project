/**
 * LVGL Configuration for ESP32-S3 T-Display (320x170)
 * 
 * This configuration is optimized for:
 * - ST7789 display controller (8-bit parallel)
 * - RGB565 color format
 * - PSRAM-backed memory pool
 * - Partial refresh mode for efficiency
 * 
 * Based on LVGL v8.3.x
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>
#include <stddef.h>  /* For size_t */

/* =============================================================================
 * Display Configuration
 * ============================================================================= */

/* Maximum horizontal and vertical resolution */
#define LV_HOR_RES_MAX          320
#define LV_VER_RES_MAX          170

/* Color depth: 16 bit RGB565 (matches ST7789) */
#define LV_COLOR_DEPTH          16

/* Swap the 2 bytes of RGB565 color (depends on display) */
/* CRITICAL: Must match TFT_eSPI's setSwapBytes(true) in lvgl_driver.cpp */
#define LV_COLOR_16_SWAP        1

/* 1: Enable screen transparency (alpha channel) */
#define LV_COLOR_SCREEN_TRANSP  0

/* =============================================================================
 * Memory Configuration (Use PSRAM)
 * ============================================================================= */

/* Size of the memory available for LVGL's internal management */
#define LV_MEM_CUSTOM           1
#define LV_MEM_SIZE             (128U * 1024U)  /* 128KB pool (using PSRAM) */

#if LV_MEM_CUSTOM == 0
/* Automatic memory management (heap_caps_malloc for PSRAM) */
#define LV_MEM_ADR              0
#define LV_MEM_AUTO_DEFRAG      1
#define LV_MEM_BUF_MAX_NUM      16
#else
/* Custom memory allocators for PSRAM */
extern void * lv_custom_mem_alloc(size_t size);
extern void lv_custom_mem_free(void * ptr);
extern void * lv_custom_mem_realloc(void * ptr, size_t new_size);

#define LV_MEM_CUSTOM_ALLOC     lv_custom_mem_alloc
#define LV_MEM_CUSTOM_FREE      lv_custom_mem_free
#define LV_MEM_CUSTOM_REALLOC   lv_custom_mem_realloc
#endif

/* Memory pool monitoring (useful for debugging) */
#define LV_MEM_CUSTOM_INCLUDE   <stdlib.h>

/* =============================================================================
 * Display Driver Configuration
 * ============================================================================= */

/* Default display refresh period (ms) */
#define LV_DISP_DEF_REFR_PERIOD 20      /* 50 FPS */

/* Input device read period (ms) - not used (no touch) */
#define LV_INDEV_DEF_READ_PERIOD 30

/* DPI (dots per inch) - affects default sizes */
#define LV_DPI_DEF              130

/* Rendering mode */
#define LV_DISP_RENDER_MODE_PARTIAL 1   /* Partial refresh (most efficient) */

/* =============================================================================
 * GPU/Hardware Acceleration
 * ============================================================================= */

/* Use ESP32's DMA for faster transfers (if applicable) */
#define LV_USE_GPU_ESP32_DMA    0       /* TFT_eSPI handles this */

/* =============================================================================
 * Font Configuration
 * ============================================================================= */

/* Montserrat fonts with bpp = 4 (16 gray levels) */
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   0
#define LV_FONT_MONTSERRAT_14   1       /* Power text */
#define LV_FONT_MONTSERRAT_16   0
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   1       /* Medium text */
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   1       /* SOC display - better anti-aliasing than 28pt */
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   1       /* Large text */
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_MONTSERRAT_34   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_38   0
#define LV_FONT_MONTSERRAT_40   0
#define LV_FONT_MONTSERRAT_42   0
#define LV_FONT_MONTSERRAT_44   0
#define LV_FONT_MONTSERRAT_46   0
#define LV_FONT_MONTSERRAT_48   0

/* Demonstrate special features (not needed for basic project) */
#define LV_FONT_MONTSERRAT_12_SUBPX      0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK            0

/* Unscii fonts (pixel perfect fonts) */
#define LV_FONT_UNSCII_8        0
#define LV_FONT_UNSCII_16       0

/* Default font */
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

/* Enable subpixel rendering (smoother text) */
#define LV_FONT_SUBPX_BGR       0

/* Custom fonts (add as needed) */
#define LV_FONT_CUSTOM_DECLARE

/* =============================================================================
 * Text Settings
 * ============================================================================= */

/* Character encoding: 1 = ASCII (0-127), 2 = ISO8859-1, 3 = Unicode */
#define LV_TXT_ENC              LV_TXT_ENC_UTF8

/* Can break words (hyphenation) */
#define LV_TXT_BREAK_CHARS      " ,.;:-_"

/* Line spacing adjustment */
#define LV_TXT_LINE_BREAK_LONG_LEN      0

/* Text coloring */
#define LV_TXT_COLOR_CMD        "#"

/* =============================================================================
 * Widget Configuration
 * ============================================================================= */

/* Documentation of widgets: https://docs.lvgl.io/latest/en/html/widgets/index.html */

#define LV_USE_ARC              0
#define LV_USE_BAR              1       /* Power bar widget */
#define LV_USE_BTN              0
#define LV_USE_BTNMATRIX        0
#define LV_USE_CANVAS           0
#define LV_USE_CHECKBOX         0
#define LV_USE_DROPDOWN         0
#define LV_USE_IMG              1       /* Splash screen image */
#define LV_USE_LABEL            1       /* Text labels (SOC, power) */
#define LV_USE_LINE             0
#define LV_USE_ROLLER           0
#define LV_USE_SLIDER           0
#define LV_USE_SWITCH           0
#define LV_USE_TEXTAREA         0
#define LV_USE_TABLE            0

/* Extra widgets (not needed) */
#define LV_USE_ANIMIMG          0
#define LV_USE_CALENDAR         0
#define LV_USE_CHART            0
#define LV_USE_COLORWHEEL       0
#define LV_USE_IMGBTN           0
#define LV_USE_KEYBOARD         0
#define LV_USE_LED              0
#define LV_USE_LIST             0
#define LV_USE_MENU             0
#define LV_USE_METER            0
#define LV_USE_MSGBOX           0
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          0
#define LV_USE_TABVIEW          0
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0

/* =============================================================================
 * Animation Configuration
 * ============================================================================= */

/* Enable animation engine (for backlight fades, transitions) */
#define LV_USE_ANIMATION        1

#if LV_USE_ANIMATION
/* Animation refresh rate (ms) */
#define LV_ANIM_RESOLUTION      20      /* 50 FPS */
#endif

/* =============================================================================
 * Themes Configuration
 * ============================================================================= */

/* Use built-in themes */
#define LV_USE_THEME_DEFAULT    1
#define LV_USE_THEME_BASIC      0
#define LV_USE_THEME_MONO       0

#if LV_USE_THEME_DEFAULT
#define LV_THEME_DEFAULT_DARK   1       /* Dark theme (black background) */
#define LV_THEME_DEFAULT_GROW   1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif

/* =============================================================================
 * Image Configuration
 * ============================================================================= */

/* Image caching */
#define LV_IMG_CACHE_DEF_SIZE   1       /* Cache 1 image (splash screen) */

/* Enable indexed (palette) images */
#define LV_IMG_INDEXED          1

/* Enable alpha channel */
#define LV_IMG_ALPHA            1

/* Default transformation (rotation, zoom) */
#define LV_IMG_TRANSFORM_NONE   1

/* =============================================================================
 * Logging Configuration
 * ============================================================================= */

/* Enable logging */
#define LV_USE_LOG              1

#if LV_USE_LOG
/* Logging levels: TRACE, INFO, WARN, ERROR, USER, NONE */
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN

/* Print log messages via printf */
#define LV_LOG_PRINTF           1

/* Custom logging function (optional) */
extern void lv_custom_log(const char * buf);
#define LV_LOG_USER             lv_custom_log
#endif

/* =============================================================================
 * Performance Monitoring
 * ============================================================================= */

/* Show performance monitor (CPU, FPS, memory) */
#define LV_USE_PERF_MONITOR     0

/* Show memory usage monitor */
#define LV_USE_MEM_MONITOR      0

/* =============================================================================
 * Optimization Settings
 * ============================================================================= */

/* 1: Refresh only dirty areas (recommended) */
#define LV_REFR_MODE            LV_REFR_MODE_PARTIAL

/* Maximum number of dirty areas per refresh */
#define LV_REFR_MAX_AREAS       4

/* Cache last drawn area (reduces overhead) */
#define LV_REFR_CACHE_LAST_AREA 1

/* =============================================================================
 * Asserts & Safety
 * ============================================================================= */

/* Enable asserts (useful for debugging) */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* Custom assert handler (optional) */
#define LV_ASSERT_HANDLER_INCLUDE   <stdint.h>
#define LV_ASSERT_HANDLER           while(1);

/* =============================================================================
 * Other Settings
 * ============================================================================= */

/* 1: Show CPU usage and FPS count in the right bottom corner */
#define LV_USE_PERF_MONITOR     0

/* Enable shadow drawing */
#define LV_SHADOW_CACHE_SIZE    0

/* Enable outline drawing */
#define LV_OUTLINE_WIDTH        2

/* Enable pattern drawing */
#define LV_PATTERN_CACHE_SIZE   0

/* Enable gradient drawing */
#define LV_GRADIENT_MAX_STOPS   8

/* Enable dithering (smooth gradients) */
#define LV_DITHER_GRADIENT      1

/* Enable blend modes */
#define LV_USE_BLEND_MODES  0   /* Not used for basic display */

/* =============================================================================
 * File System Configuration (for images from LittleFS)
 * ============================================================================= */

/* Enable file system support */
#define LV_USE_FS_IF            1

#if LV_USE_FS_IF
#define LV_FS_IF_FATFS          0
#define LV_FS_IF_PC             0
#define LV_FS_IF_LITTLEFS       1       /* Use LittleFS for image loading */
#endif

/* =============================================================================
 * Image Format Support
 * ============================================================================= */

/* Enable SJPG (LVGL's simplified JPEG decoder) for file loading */
#define LV_USE_SJPG             0       /* Disabled: project uses JPEGDecoder directly */

/* PNG support (optional) */
#define LV_USE_PNG              0

/* BMP support (optional) */
#define LV_USE_BMP              0

/* Other optional image/libs extras (keep disabled for embedded footprint) */
#define LV_USE_GIF              0
#define LV_USE_QRCODE           0
#define LV_USE_RLOTTIE          0
#define LV_USE_FREETYPE         0
#define LV_USE_TINY_TTF         0
#define LV_USE_FS_STDIO         0

/* =============================================================================
 * GPU/Acceleration (ESP32-S3 specific)
 * ============================================================================= */

/* Use ESP32-S3 DMA for faster pixel transfers */
#define LV_USE_GPU_ESP32_DMA    0       /* TFT_eSPI already handles DMA */

/* Disable non-ESP32 GPU backends */
#define LV_USE_GPU_STM32_DMA2D      0
#define LV_USE_GPU_NXP_PXP          0
#define LV_USE_GPU_NXP_VG_LITE      0
#define LV_USE_GPU_SWM341_DMA2D     0

/* =============================================================================
 * API Settings
 * ============================================================================= */

/* Enable all object types by default */
#define LV_USE_OBJ_ALL          0

/* Enable snapshot API (screenshot functionality) */
#define LV_USE_SNAPSHOT         0

/* Enable group (focus management for input devices) */
#define LV_USE_GROUP            0

/* =============================================================================
 * Compiler Settings
 * ============================================================================= */

/* Prefix all global LVGL symbols (avoid conflicts) */
#define LV_CONF_MINIMAL         0

/* Use custom tick source (for RTOS integration) */
#define LV_TICK_CUSTOM          1

#if LV_TICK_CUSTOM
#define LV_TICK_CUSTOM_INCLUDE  "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#endif /* LV_CONF_H */
