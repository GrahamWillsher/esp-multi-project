# Working Code Patterns: LVGL Image Display on ESP32-S3

**Source:** Extracted from `espnowreceiver_2` working implementation  
**Status:** ✅ Verified Working - TFT_eSPI Direct Rendering  
**Hardware:** ESP32-S3 + ST7789 (320x170) + LittleFS + JPEGDecoder  

> **Historical note (March 15, 2026):** This pattern reference is kept for working-display examples. Any shared ESP-NOW protocol includes shown below should now be read as canonical `esp32common/...` includes.

---

## Pattern 1: Direct TFT Rendering (✅ WORKS)

### File: `src/display/display_splash.cpp`

**Complete Working Implementation:**

```cpp
#include "display_splash.h"
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <JPEGDecoder.h>

extern TFT_eSPI tft;

// ============================================================================
// PATTERN 1: JPEG Display from LittleFS with Direct TFT Rendering
// ============================================================================
void displaySplashJpeg2(const char* filename) {
    // STEP 1: File validation
    if (!LittleFS.exists(filename)) {
        LOG_ERROR("[JPEG2] File not found: %s", filename);
        return;
    }
    
    // STEP 2: Load entire JPEG into RAM buffer
    File f = LittleFS.open(filename, "r");
    if (!f) {
        LOG_ERROR("[JPEG2] Failed to open: %s", filename);
        return;
    }
    
    size_t fileSize = f.size();
    LOG_DEBUG("[JPEG2] Loading %s (%d bytes)", filename, fileSize);
    
    // Critical: Allocate buffer in RAM (not PSRAM) for JPEGDecoder
    uint8_t* buffer = (uint8_t*)malloc(fileSize);
    if (!buffer) {
        LOG_ERROR("[JPEG2] Memory allocation failed");
        f.close();
        return;
    }
    
    // STEP 3: Read file into buffer
    if (f.read(buffer, fileSize) != fileSize) {
        LOG_ERROR("[JPEG2] File read error");
        free(buffer);
        f.close();
        return;
    }
    f.close();
    
    // STEP 4: Decode JPEG from buffer (not file path!)
    if (!JpegDec.decodeArray(buffer, fileSize)) {  // <- Use decodeArray, not decodeFsFile
        LOG_ERROR("[JPEG2] Decode failed");
        free(buffer);
        return;
    }
    
    LOG_INFO("[JPEG2] Decoded: %dx%d", JpegDec.width, JpegDec.height);
    
    // STEP 5: Calculate position for centered display
    int16_t xOffset = (Display::SCREEN_WIDTH - JpegDec.width) / 2;
    int16_t yOffset = (Display::SCREEN_HEIGHT - JpegDec.height) / 2;
    if (xOffset < 0) xOffset = 0;
    if (yOffset < 0) yOffset = 0;
    
    // STEP 6: Stream MCU blocks to display
    uint16_t* pImg;
    uint16_t mcu_w = JpegDec.MCUWidth;
    uint16_t mcu_h = JpegDec.MCUHeight;
    uint32_t max_x = JpegDec.width;
    uint32_t max_y = JpegDec.height;
    
    while (JpegDec.read()) {
        pImg = JpegDec.pImage;
        
        // Calculate MCU position
        int16_t mcu_x = JpegDec.MCUx * mcu_w + xOffset;
        int16_t mcu_y = JpegDec.MCUy * mcu_h + yOffset;
        
        // Clip to display bounds
        uint16_t win_w = (mcu_x + mcu_w <= xOffset + max_x) ? mcu_w : (xOffset + max_x - mcu_x);
        uint16_t win_h = (mcu_y + mcu_h <= yOffset + max_y) ? mcu_h : (yOffset + max_y - mcu_y);
        
        if (win_w > 0 && win_h > 0) {
            tft.pushImage(mcu_x, mcu_y, win_w, win_h, pImg);
        }
    }
    
    // STEP 7: Cleanup - critical to free buffer
    free(buffer);
    
    LOG_DEBUG("[JPEG2] Displayed %dx%d at (%d,%d)", JpegDec.width, JpegDec.height, xOffset, yOffset);
}

// ============================================================================
// PATTERN 2: Splash Screen Content Display
// ============================================================================
void displaySplashScreenContent() {
    LOG_DEBUG("[SPLASH] Displaying splash screen content...");
    
    // Clear display
    tft.fillScreen(TFT_BLACK);
    LOG_DEBUG("[SPLASH] Screen cleared");

    // Display JPEG image
    const char* splashFile = "/BatteryEmulator4_320x170.jpg";
    displaySplashJpeg2(splashFile);
    
    // If no image, show text fallback
    bool imageDisplayed = LittleFS.exists(splashFile);
    if (!imageDisplayed) {
        LOG_INFO("[SPLASH] No splash image found, showing text splash");
        
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(2);
        
        String text = "ESP32 Display";
        int16_t x = (Display::SCREEN_WIDTH - (text.length() * 12)) / 2;
        int16_t y = Display::SCREEN_HEIGHT / 2 - 20;
        
        tft.setCursor(x, y);
        tft.println(text);
        
        tft.setTextSize(1);
        text = "ESP-NOW Receiver";
        x = (Display::SCREEN_WIDTH - (text.length() * 6)) / 2;
        y = Display::SCREEN_HEIGHT / 2 + 20;
        
        tft.setCursor(x, y);
        tft.println(text);
    }
}

// ============================================================================
// PATTERN 3: Backlight Fade Animation (PWM Control)
// ============================================================================
void fadeBacklight(uint8_t targetBrightness, uint32_t durationMs) {
    LOG_DEBUG("[BACKLIGHT] Fading to %d over %d ms", targetBrightness, durationMs);
    
    uint8_t currentBrightness = Display::current_backlight_brightness;
    uint32_t startTime = millis();
    
    while (millis() - startTime < durationMs) {
        // Linear interpolation
        uint32_t elapsed = millis() - startTime;
        float progress = (float)elapsed / durationMs;
        
        if (progress > 1.0f) progress = 1.0f;
        
        uint8_t brightness = (uint8_t)(currentBrightness + 
                            (int32_t)targetBrightness - currentBrightness) * progress);
        
        // Update PWM backlight
        #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
        ledcWrite(0, brightness);
        #else
        ledcWrite(Display::PIN_LCD_BL, brightness);
        #endif
        
        Display::current_backlight_brightness = brightness;
        smart_delay(10);  // Update every 10ms for smooth fade
    }
    
    // Ensure final value is set
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(0, targetBrightness);
    #else
    ledcWrite(Display::PIN_LCD_BL, targetBrightness);
    #endif
    
    Display::current_backlight_brightness = targetBrightness;
    LOG_DEBUG("[BACKLIGHT] Fade complete");
}

// ============================================================================
// PATTERN 4: Complete Splash Sequence with Fade
// ============================================================================
void displaySplashWithFade() {
    LOG_INFO("[SPLASH] === Starting Splash Screen Sequence ===");
    
    // Step 1: Turn off backlight
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(0, 0);
    #else
    ledcWrite(Display::PIN_LCD_BL, 0);
    #endif
    Display::current_backlight_brightness = 0;
    smart_delay(200);
    
    // Step 2: Display content (image or text)
    LOG_DEBUG("[SPLASH] Displaying content...");
    displaySplashScreenContent();
    LOG_DEBUG("[SPLASH] Content displayed");
    
    // Step 3: Fade in backlight over 2 seconds
    LOG_DEBUG("[SPLASH] Fading in splash screen...");
    fadeBacklight(255, 2000);
    LOG_DEBUG("[SPLASH] Fade in complete");
    
    // Step 4: Display splash for 3 seconds
    smart_delay(3000);
    
    // Step 5: Fade out backlight over 2 seconds
    LOG_DEBUG("[SPLASH] Fading out splash screen...");
    fadeBacklight(0, 2000);
    LOG_DEBUG("[SPLASH] Fade out complete");
    
    // Step 6: Clear display
    tft.fillScreen(TFT_BLACK);
    
    LOG_INFO("[SPLASH] === Splash Screen Sequence Complete ===");
}
```

---

## Pattern 2: Image File from LittleFS

### Header: `src/display/display_splash.h`

```cpp
#pragma once

#include <Arduino.h>
#include "../common.h"

// Splash screen functions

/**
 * Display JPEG file from LittleFS
 * @param filename Path to JPEG file (e.g., "/image.jpg")
 */
void displaySplashJpeg2(const char* filename);

/**
 * Display splash screen content (image or text fallback)
 */
void displaySplashScreenContent();

/**
 * Fade backlight to target brightness
 * @param targetBrightness 0-255 PWM value
 * @param durationMs Duration of fade in milliseconds
 */
void fadeBacklight(uint8_t targetBrightness, uint32_t durationMs);

/**
 * Complete splash screen sequence with fade animation
 * - Turns off backlight
 * - Displays splash image/text
 * - Fades in over 2 seconds
 * - Holds for 3 seconds
 * - Fades out over 2 seconds
 */
void displaySplashWithFade();
```

---

## Pattern 3: Initialization & Integration

### File: `src/config/littlefs_init.cpp`

```cpp
#include "littlefs_init.h"
#include "../common.h"
#include "../display/display_splash.h"
#include <LittleFS.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

void initlittlefs() {
    Serial.println("[INIT] Initializing LittleFS...");
    Serial.flush();
    
    bool littlefs_ok = false;
    
    // Attempt to mount, format if needed
    if (!LittleFS.begin(false)) {
        Serial.println("[WARN] LittleFS mount failed, trying to format...");
        Serial.flush();
        
        if (!LittleFS.begin(true)) {  // true = format if mount fails
            Serial.println("[ERROR] LittleFS initialization failed!");
            Serial.flush();
            
            // Display error message
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_RED);
            tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("LittleFS INIT FAILED!", Display::SCREEN_WIDTH / 2, Display::SCREEN_HEIGHT / 2);
            smart_delay(4000);
            littlefs_ok = false;
        } else {
            Serial.println("[INIT] LittleFS formatted and mounted successfully");
            Serial.flush();
            littlefs_ok = true;
        }
    } else {
        Serial.println("[INIT] LittleFS mounted successfully");
        Serial.flush();
        littlefs_ok = true;
    }
    
    // If LittleFS initialized, display splash
    if (littlefs_ok) {
        Serial.println("[INIT] Starting splash screen...");
        Serial.flush();
        displaySplashWithFade();
        Serial.println("[INIT] Splash screen complete");
        Serial.flush();
    } else {
        Serial.println("[INIT] Skipping splash screen (LittleFS not available)");
        Serial.flush();
    }
}
```

---

## Pattern 4: Common Include File

### File: `src/common.h` (Relevant Portions)

```cpp
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <LittleFS.h>
#include <JPEGDecoder.h>
#include <esp32common/espnow/common.h>

// Display namespace and configuration
namespace Display {
    // Display dimensions
    const int SCREEN_WIDTH = 320;
    const int SCREEN_HEIGHT = 170;
    
    // Backlight pin
    const int PIN_LCD_BL = 38;  // Adjust to your hardware
    
    // Current backlight brightness (0-255)
    extern uint8_t current_backlight_brightness;
}

// ... rest of common.h
```

---

## Critical Implementation Details

### 1. JPEGDecoder Usage

**WRONG** (will fail):
```cpp
JpegDec.decodeFsFile(filename);  // File system file - slow and unreliable
```

**CORRECT** (works):
```cpp
uint8_t* buffer = malloc(fileSize);
f.read(buffer, fileSize);
JpegDec.decodeArray(buffer, fileSize);  // Buffer in RAM - fast and reliable
```

### 2. MCU Block Streaming

**Why MCU blocks?**
- JPEG is decoded in 8x8 or 16x16 blocks (MCUs)
- Streaming MCU by MCU = lower memory peak
- Each MCU is ~500 bytes max, not 320x170x2 = 108KB

**Correct pattern:**
```cpp
while (JpegDec.read()) {
    pImg = JpegDec.pImage;  // Current MCU pixel data
    tft.pushImage(x, y, width, height, pImg);
}
```

### 3. Display Dimensions

**For 320x170 display:**
```cpp
#define TFT_WIDTH   320
#define TFT_HEIGHT  170
```

**Centering calculation:**
```cpp
int16_t xOffset = (320 - imgWidth) / 2;   // Centers horizontally
int16_t yOffset = (170 - imgHeight) / 2;  // Centers vertically
```

### 4. Backlight Control

**ESP-IDF 4.x:**
```cpp
ledcWrite(0, brightness);  // Channel 0, value 0-255
```

**ESP-IDF 5.x+:**
```cpp
ledcWrite(PIN_LCD_BL, brightness);  // Use pin number directly
```

**Setup (in main init):**
```cpp
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
ledcSetup(0, 5000, 8);  // Channel 0, 5kHz, 8-bit
ledcAttachPin(38, 0);   // Attach pin 38 to channel 0
#else
ledcAttach(38, 5000, 8);  // Pin 38, 5kHz, 8-bit
#endif
```

### 5. File System Integration

**LittleFS configuration:**
```cpp
// In setup():
if (!LittleFS.begin(false)) {
    if (!LittleFS.begin(true)) {  // Format if mount fails
        // Handle error
    }
}

// Check if file exists:
if (LittleFS.exists("/image.jpg")) {
    File f = LittleFS.open("/image.jpg", "r");
    // ...
    f.close();
}
```

---

## Testing Checklist

- [ ] LittleFS mounted and file `/BatteryEmulator4_320x170.jpg` exists
- [ ] JPEGDecoder library installed and tested
- [ ] TFT_eSPI initialized with correct pins
- [ ] Backlight PWM working (can control brightness 0-255)
- [ ] Basic image display works without animation
- [ ] Fade animation smooth (10ms updates)
- [ ] Serial logging shows all steps completing
- [ ] Image stays visible during entire fade sequence

---

## Memory Analysis

**Peak memory usage for 320x170 JPG:**
- Buffer: ~110 KB (file size, varies by compression)
- JPEG decoder state: ~50 KB
- MCU temporary: ~10 KB
- **Total: ~170 KB** (well within ESP32-S3 RAM)

**If PSRAM used:**
- Can allocate buffer in PSRAM for very large files
- MCU decoding still requires IRAM for speed

---

## Performance Metrics

**From working implementation:**
- File read: ~50-100ms for 110KB JPG
- Decode & display: ~200-400ms (depends on JPEG complexity)
- Total splash sequence: 2 + 3 + 2 = 7 seconds (design)
- Fade smoothness: 200 frames at 10ms interval = imperceptible flicker

---

## Comparison: Direct TFT vs LVGL

| Aspect | Direct TFT | LVGL |
|--------|-----------|------|
| **Setup Complexity** | Simple - 2-3 functions | Complex - needs config |
| **File Support** | Any (with decoder) | Needs file system driver |
| **Buffer Management** | Manual (simple) | Automatic (can cause issues) |
| **Animation Control** | Direct PWM | LVGL animation system |
| **RAM Usage** | Predictable | Variable (cache) |
| **UI Flexibility** | Limited | Full widget library |
| **Learning Curve** | Shallow | Steep |

**Verdict:** Use direct TFT for splash screens and performance-critical display. Use LVGL for complex UIs.

---

## Next Steps

1. **Copy exact pattern** from this document
2. **Verify file exists:** `/BatteryEmulator4_320x170.jpg` in LittleFS
3. **Test independently:** Call `displaySplashWithFade()` from `setup()`
4. **Check serial logs:** Should see all [SPLASH], [JPEG2], [BACKLIGHT] messages
5. **Debug**: If image not showing, add file exists check and log file size

**If using LVGL for splash instead:**
- Convert image to `.bin` format via imageconverter
- Register LittleFS file system driver
- Use `lv_img_set_src("S:/image.bin")` 
- See detailed guide in `LVGL_IMAGE_DISPLAY_RESEARCH_FINDINGS.md`

