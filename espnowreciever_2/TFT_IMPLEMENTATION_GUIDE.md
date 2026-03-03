## TFT Implementation - Code Extraction Guide

This document provides step-by-step instructions for extracting existing code and filling in the TFT implementation stubs.

---

## Source Files to Reference

All existing code that should be integrated into TFT implementation:

### Display Hardware Initialization
- **File**: [src/hal/display/tft_espi_display_driver.cpp](src/hal/display/tft_espi_display_driver.cpp)
- **Lines to Extract**: 16-31
- **What to copy**: 
  ```cpp
  tft_.init();
  tft_.setSwapBytes(true);  // CRITICAL for color rendering
  ```
- **Target Method**: `TftDisplay::init_hardware()`

### Backlight PWM Control
- **File**: [src/hal/display/lvgl_driver.cpp](src/hal/display/lvgl_driver.cpp)
- **Lines to Extract**: 83-115
- **What to copy**:
  - `pinMode()` and `digitalWrite()` for power enable
  - `ledcSetup()` / `ledcAttach()` for PWM configuration
  - PWM channel, frequency, resolution settings
  - Hardware config constants
- **Target Methods**: 
  - `TftDisplay::init_backlight()`
  - `TftDisplay::set_backlight()`

### Backlight Animation (Fade In/Out)
- **File**: [src/hal/display/lvgl_driver.cpp](src/hal/display/lvgl_driver.cpp)
- **Lines to Extract**: 184-208 (animate_backlight_to / set_backlight pattern)
- **What to copy**:
  - Smooth brightness progression algorithm
  - Step calculation and timing
  - PWM write calls
- **Target Method**: `TftDisplay::animate_backlight()`
- **Note**: TFT version will use `delay()` instead of LVGL animations

### JPEG Splash Loading
- **File**: [src/display/display_splash_lvgl.cpp](src/display/display_splash_lvgl.cpp)
- **Lines to Extract**: 29-110
- **What to copy**:
  - `decode_jpg_to_rgb565()` function
  - LittleFS file opening
  - JPEGDecoder library usage
  - MCU block copying to buffer
  - Pixel format handling
- **Target Method**: `TftDisplay::load_and_draw_splash()`
- **Note**: After decode, draw with `tft_.pushImage()` instead of LVGL

### Display Dimensions & Constants
- **File**: [src/hal/display/lvgl_driver.cpp](src/hal/display/lvgl_driver.cpp)
- **Lines to Extract**: Look for `SCREEN_WIDTH`, `SCREEN_HEIGHT`
- **Also check**: [src/common.h](src/common.h) for display constants
- **Target**: Use same constants in TFT implementation

### Hardware Configuration
- **Files**: [src/hal/hardware_config.h](src/hal/hardware_config.h), [src/common.h](src/common.h)
- **Look for constants**:
  - `GPIO_BACKLIGHT`
  - `GPIO_DISPLAY_POWER`
  - `BACKLIGHT_PWM_CHANNEL`
  - `BACKLIGHT_FREQUENCY_HZ`
  - `BACKLIGHT_RESOLUTION_BITS`
- **Usage**: Copy these into TFT implementation or reference from headers

---

## Step-by-Step Implementation

### Step 1: Initialize Hardware

**Target File**: [src/display/tft_impl/tft_display.cpp](src/display/tft_impl/tft_display.cpp#L38-L55)
**Method**: `void TftDisplay::init_hardware()`

```cpp
void TftDisplay::init_hardware() {
    LOG_DEBUG("TFT", "Initializing TFT hardware...");
    
    // EXTRACT FROM: tft_espi_display_driver.cpp lines 16-31
    tft_.init();
    tft_.setRotation(1);  // Landscape mode (0=portrait, 1=landscape, 2=reverse portrait, 3=reverse landscape)
    tft_.fillScreen(TFT_BLACK);
    
    // CRITICAL: Must be called AFTER init()
    // Sets byte order for color data to match hardware expectations
    tft_.setSwapBytes(true);
    
    LOG_DEBUG("TFT", "TFT hardware initialized (rotation=1, swapBytes=true)");
}
```

**Verification**: 
- [ ] Screen should be black
- [ ] No color glitches
- [ ] Display is in landscape orientation (320x170)

---

### Step 2: Initialize Backlight PWM

**Target File**: [src/display/tft_impl/tft_display.cpp](src/display/tft_impl/tft_display.cpp#L57-L75)
**Method**: `void TftDisplay::init_backlight()`

```cpp
void TftDisplay::init_backlight() {
    LOG_DEBUG("TFT", "Initializing backlight PWM...");
    
    // EXTRACT FROM: lvgl_driver.cpp lines 83-115
    
    // Step 1: Enable display power
    pinMode(HardwareConfig::GPIO_DISPLAY_POWER, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_DISPLAY_POWER, HIGH);  // Power ON
    LOG_DEBUG("TFT", "Display power enabled (GPIO %d)", 
              HardwareConfig::GPIO_DISPLAY_POWER);
    
    // Step 2: Configure backlight PWM
    pinMode(HardwareConfig::GPIO_BACKLIGHT, OUTPUT);
    
    // Handle ESP-IDF version differences (v4 vs v5)
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    // ESP-IDF v4.x API
    ledcSetup(HardwareConfig::BACKLIGHT_PWM_CHANNEL, 
              HardwareConfig::BACKLIGHT_FREQUENCY_HZ, 
              HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
    ledcAttachPin(HardwareConfig::GPIO_BACKLIGHT, 
                  HardwareConfig::BACKLIGHT_PWM_CHANNEL);
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, 0);  // Start at 0
    
    #else
    // ESP-IDF v5.x API
    ledcAttach(HardwareConfig::GPIO_BACKLIGHT, 
               HardwareConfig::BACKLIGHT_FREQUENCY_HZ, 
               HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
    ledcWrite(HardwareConfig::GPIO_BACKLIGHT, 0);  // Start at 0
    #endif
    
    LOG_DEBUG("TFT", "Backlight PWM initialized (GPIO %d, %d Hz, %d-bit)",
              HardwareConfig::GPIO_BACKLIGHT,
              HardwareConfig::BACKLIGHT_FREQUENCY_HZ,
              HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
}
```

**Verification**:
- [ ] Backlight is off initially
- [ ] No boot errors in log
- [ ] Can later fade in without artifacts

---

### Step 3: Implement Backlight Control

**Target File**: [src/display/tft_impl/tft_display.cpp](src/display/tft_impl/tft_display.cpp#L139-L160)
**Methods**: `void TftDisplay::set_backlight()` and `void TftDisplay::animate_backlight()`

```cpp
void TftDisplay::set_backlight(uint8_t brightness) {
    // EXTRACT FROM: lvgl_driver.cpp lines 210-224
    
    static int16_t last_logged = -1;
    
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, brightness);
    #else
    ledcWrite(HardwareConfig::GPIO_BACKLIGHT, brightness);
    #endif
    
    // Quiet logging: only log at boundaries and large deltas
    if (last_logged < 0 || brightness == 0 || brightness == 255 ||
        (uint8_t)last_logged == 0 || (uint8_t)last_logged == 255 ||
        ((int)brightness - last_logged >= 50) || ((int)brightness - last_logged <= -50)) {
        LOG_DEBUG("TFT", "Backlight: %u", brightness);
        last_logged = brightness;
    }
}

void TftDisplay::animate_backlight(uint8_t target, uint32_t duration_ms) {
    if (duration_ms == 0) {
        set_backlight(target);
        return;
    }
    
    // EXTRACT FROM: Custom TFT animation logic
    // TFT doesn't have async animations, so we use blocking delay()
    
    const uint32_t steps = (duration_ms + 8) / 16;  // ~60 FPS
    const int8_t direction = (target > 0) ? 1 : -1;  // Fade direction
    
    LOG_DEBUG("TFT", "Animating backlight to %u over %u ms (%u steps)", 
              target, duration_ms, steps);
    
    // Get current brightness by reading state (can track in member variable)
    // For now, approximate based on our last write
    uint8_t current = 0;  // TODO: Could store in member variable
    
    for (uint32_t i = 0; i <= steps; i++) {
        // Linear interpolation
        uint8_t brightness = (target * i) / steps;
        set_backlight(brightness);
        
        if (i < steps) {
            smart_delay(duration_ms / steps);
        }
    }
    
    // Ensure we hit exact target
    set_backlight(target);
}
```

**Alternative - Better Implementation** (track state):

Update `TftDisplay` class to add:
```cpp
private:
    uint8_t current_backlight_ = 0;  // Track current brightness
```

Then update `set_backlight()` to save state, and `animate_backlight()` can use it.

---

### Step 4: Implement Splash Screen Loading

**Target File**: [src/display/tft_impl/tft_display.cpp](src/display/tft_impl/tft_display.cpp#L118-L137)
**Method**: `void TftDisplay::load_and_draw_splash()`

```cpp
void TftDisplay::load_and_draw_splash() {
    // EXTRACT FROM: display_splash_lvgl.cpp lines 29-110
    
    LOG_INFO("TFT", "Loading splash image...");
    
    const char* splash_path = "/BatteryEmulator4_320x170.jpg";
    uint16_t img_w = 0, img_h = 0;
    
    // Step 1: Decode JPEG
    uint16_t* img_data = decode_jpg_to_rgb565(splash_path, img_w, img_h);
    if (!img_data) {
        LOG_WARN("TFT", "Failed to load splash image, showing fallback");
        // Fallback: Just black screen or text
        tft_.fillScreen(TFT_BLACK);
        return;
    }
    
    LOG_INFO("TFT", "Splash image decoded: %ux%u", img_w, img_h);
    
    // Step 2: Center image on screen (320x170)
    const int16_t x_offset = (320 - img_w) / 2;  // Center horizontally
    const int16_t y_offset = (170 - img_h) / 2;  // Center vertically
    
    // Step 3: Draw image to display
    tft_.pushImage(x_offset, y_offset, img_w, img_h, img_data);
    
    // Step 4: Free the decoded image buffer
    heap_caps_free(img_data);
    
    LOG_INFO("TFT", "Splash image displayed at (%d, %d)", x_offset, y_offset);
}

/**
 * Helper: Decode JPEG from LittleFS to RGB565 buffer
 * EXTRACT FROM: display_splash_lvgl.cpp lines 29-110
 * 
 * Returns heap_caps_malloc'd buffer. Caller must heap_caps_free() it.
 * Returns nullptr on failure; out_w / out_h are set to 0.
 */
static uint16_t* decode_jpg_to_rgb565(const char* path,
                                      uint16_t& out_w, uint16_t& out_h) {
    out_w = 0;
    out_h = 0;

    if (!LittleFS.exists(path)) {
        LOG_ERROR("TFT", "File not found: %s", path);
        return nullptr;
    }

    File f = LittleFS.open(path, "r");
    if (!f) {
        LOG_ERROR("TFT", "Cannot open: %s", path);
        return nullptr;
    }

    const size_t file_sz = f.size();
    LOG_INFO("TFT", "JPEG size: %u bytes", (unsigned)file_sz);

    // Read JPEG file into buffer
    uint8_t* raw = (uint8_t*)malloc(file_sz);
    if (!raw) {
        LOG_ERROR("TFT", "malloc failed for JPEG (%u bytes)", (unsigned)file_sz);
        f.close();
        return nullptr;
    }
    if (f.read(raw, file_sz) != file_sz) {
        LOG_ERROR("TFT", "Short read: %s", path);
        free(raw);
        f.close();
        return nullptr;
    }
    f.close();

    // Decode JPEG
    const uint32_t t0 = millis();
    if (!JpegDec.decodeArray(raw, file_sz)) {
        LOG_ERROR("TFT", "JPEG decode failed: %s", path);
        free(raw);
        return nullptr;
    }
    free(raw);

    out_w = JpegDec.width;
    out_h = JpegDec.height;
    LOG_INFO("TFT", "JPEG decoded in %u ms: %ux%u", 
             (unsigned)(millis() - t0), (unsigned)out_w, (unsigned)out_h);

    // Allocate RGB565 buffer
    const size_t num_px = (size_t)out_w * out_h;
    uint16_t* buf = (uint16_t*)heap_caps_malloc(
        num_px * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        LOG_ERROR("TFT", "PSRAM malloc failed for pixel buffer (%u bytes)",
                  (unsigned)(num_px * sizeof(uint16_t)));
        out_w = 0; out_h = 0;
        return nullptr;
    }

    // Copy MCU blocks from JPEGDecoder to contiguous buffer
    while (JpegDec.read()) {
        const uint16_t  mcu_w = JpegDec.MCUWidth;
        const uint16_t  mcu_h = JpegDec.MCUHeight;
        const uint32_t  x0    = (uint32_t)JpegDec.MCUx * mcu_w;
        const uint32_t  y0    = (uint32_t)JpegDec.MCUy * mcu_h;
        const uint16_t* src   = JpegDec.pImage;
        
        for (uint16_t dy = 0; dy < mcu_h; dy++) {
            if (y0 + dy >= out_h) break;
            for (uint16_t dx = 0; dx < mcu_w; dx++) {
                if (x0 + dx >= out_w) break;
                buf[(y0 + dy) * out_w + (x0 + dx)] = src[dy * mcu_w + dx];
            }
        }
    }

    LOG_INFO("TFT", "JPEG pixel buffer ready: 0x%08X", (uint32_t)buf);
    return buf;
}
```

**Files to #include** at top of tft_display.cpp:
```cpp
#include <LittleFS.h>
#include <JPEGDecoder.h>
```

**Verification**:
- [ ] LittleFS can find splash file
- [ ] JPEGDecoder decodes without errors
- [ ] Image displays centered on screen
- [ ] No memory leaks (buffer freed)
- [ ] Colors are correct (no byte-swap issues)

---

### Step 5: Implement Splash Sequence Orchestration

**Target File**: [src/display/tft_impl/tft_display.cpp](src/display/tft_impl/tft_display.cpp#L78-L98)
**Method**: `void TftDisplay::display_splash_with_fade()`

```cpp
void TftDisplay::display_splash_with_fade() {
    LOG_INFO("TFT", "=== Splash START ===");
    
    try {
        // 1. Start with backlight OFF
        set_backlight(0);
        LOG_DEBUG("TFT", "Backlight set to 0");
        
        // 2. Load and display splash image
        load_and_draw_splash();
        
        // 3. Fade in backlight (0 -> 255 over 300ms)
        animate_backlight(255, 300);
        LOG_DEBUG("TFT", "Fade-in complete");
        
        // 4. Hold splash for 2 seconds
        LOG_DEBUG("TFT", "Holding splash...");
        smart_delay(2000);
        
        // 5. Fade out backlight (255 -> 0 over 300ms)
        animate_backlight(0, 300);
        LOG_DEBUG("TFT", "Fade-out complete");
        
        LOG_INFO("TFT", "=== Splash END ===");
    } catch (const std::exception& e) {
        LOG_ERROR("TFT", "Splash error: %s", e.what());
    }
}
```

**Verification**:
- [ ] Backlight starts off
- [ ] Splash image fades in smoothly
- [ ] Backlight reaches full brightness
- [ ] Splash displays for ~2 seconds
- [ ] Backlight fades out smoothly
- [ ] Sequence completes without crashes

---

### Step 6: Implement Display Updates (SOC & Power)

**Target File**: [src/display/tft_impl/tft_display.cpp](src/display/tft_impl/tft_display.cpp)
**Methods**: 
- `void TftDisplay::update_soc(float soc_percent)`
- `void TftDisplay::update_power(int32_t power_w)`
- Helper methods: `draw_soc()`, `draw_power()`

For now, provide simple text-based implementation:

```cpp
void TftDisplay::update_soc(float soc_percent) {
    LOG_DEBUG("TFT", "Updating SOC: %.1f%%", soc_percent);
    draw_soc(soc_percent);
}

void TftDisplay::draw_soc(float soc_percent) {
    // TODO: Implement proper SOC display
    // For now, simple text in upper left
    tft_.setTextColor(TFT_GREEN);
    tft_.setTextSize(2);
    char buf[16];
    snprintf(buf, sizeof(buf), "SOC: %.0f%%", soc_percent);
    tft_.drawString(buf, 10, 10);
}

void TftDisplay::update_power(int32_t power_w) {
    LOG_DEBUG("TFT", "Updating Power: %ld W", power_w);
    draw_power(power_w);
}

void TftDisplay::draw_power(int32_t power_w) {
    // TODO: Implement proper power display
    // For now, simple text below SOC
    tft_.setTextColor(TFT_YELLOW);
    tft_.setTextSize(2);
    char buf[16];
    snprintf(buf, sizeof(buf), "PWR: %ld W", power_w);
    tft_.drawString(buf, 10, 35);
}
```

---

### Step 7: Implement Status Page Display

**Target File**: [src/display/tft_impl/tft_display.cpp](src/display/tft_impl/tft_display.cpp)
**Method**: `void TftDisplay::show_status_page()`

Simple placeholder for now:

```cpp
void TftDisplay::show_status_page() {
    LOG_INFO("TFT", "Showing status page...");
    
    tft_.fillScreen(TFT_BLACK);
    tft_.setTextColor(TFT_GREEN);
    tft_.setTextSize(2);
    tft_.drawString("Battery Status", 50, 10);
    
    // SOC and Power will be updated via update_soc() and update_power()
    // as data becomes available
}
```

---

### Step 8: Implement Error Display

**Target File**: [src/display/tft_impl/tft_display.cpp](src/display/tft_impl/tft_display.cpp)
**Methods**:
- `void TftDisplay::show_error_state()`
- `void TftDisplay::show_fatal_error()`

```cpp
void TftDisplay::show_error_state() {
    LOG_WARN("TFT", "Showing error state");
    
    tft_.fillScreen(TFT_RED);
    tft_.setTextColor(TFT_WHITE);
    tft_.setTextSize(2);
    tft_.drawString("ERROR", 130, 75);
}

void TftDisplay::show_fatal_error(const char* component, const char* message) {
    LOG_ERROR("TFT", "Fatal error: [%s] %s", component, message);
    
    tft_.fillScreen(TFT_RED);
    tft_.setTextColor(TFT_WHITE);
    tft_.setTextSize(2);
    tft_.drawString("FATAL ERROR", 90, 20);
    
    if (component) {
        tft_.setTextSize(1);
        tft_.drawString(component, 20, 60);
    }
    
    if (message) {
        tft_.setTextSize(1);
        tft_.drawString(message, 20, 80);
    }
}
```

---

### Step 9: Implement Wrapper Methods

**Target File**: [src/display/tft_impl/tft_display.cpp](src/display/tft_impl/tft_display.cpp)
**Method**: `void TftDisplay::display_initial_screen()`

```cpp
void TftDisplay::display_initial_screen() {
    LOG_INFO("TFT", "Displaying Ready screen...");
    
    tft_.fillScreen(TFT_BLACK);
    tft_.setTextColor(TFT_GREEN);
    tft_.setTextSize(2);
    tft_.drawString("Ready", 140, 80);
    
    // Fade in backlight to full brightness
    animate_backlight(255, 300);
}
```

---

## Compilation Checklist

Before testing on hardware:

- [ ] All `#include` directives are present
- [ ] All member variables are declared in header
- [ ] All helper functions are implemented
- [ ] No syntax errors in code
- [ ] `#ifdef USE_TFT` / `#endif` guards are correct

### To Compile:
```bash
# Set USE_TFT in platformio.ini
pio run -e esp32s3-tft
```

### If compilation fails:
1. Check error messages for missing includes
2. Check that HardwareConfig constants are available
3. Check that `smart_delay()` is available (should be in helpers.h)
4. Check that LOG_* macros are available (should be in logging.h)

---

## Testing Checklist

### Visual Tests:
- [ ] Display turns on
- [ ] Backlight starts off
- [ ] Splash image displays correctly
- [ ] Splash fades in smoothly
- [ ] Splash fades out smoothly
- [ ] Ready screen shows
- [ ] Text is readable

### Functional Tests:
- [ ] `update_soc()` updates display
- [ ] `update_power()` updates display
- [ ] `show_status_page()` displays correctly
- [ ] `show_error_state()` displays red
- [ ] `show_fatal_error()` displays error details

### Performance Tests:
- [ ] No memory leaks (PSRAM usage stable)
- [ ] No crashes during splash sequence
- [ ] Backlight fade is smooth
- [ ] Text rendering is fast

---

## Future Enhancements

After basic implementation works:

1. **Better SOC/Power Display**
   - Draw progress bars instead of text
   - Use different colors based on values
   - Add min/max indicators

2. **Status Page Layout**
   - Create detailed layout with multiple metrics
   - Add icons or graphics
   - Implement screen transitions

3. **Animation Effects**
   - Pulse effects for alerts
   - Smooth value transitions
   - Page transition animations

4. **Performance Optimization**
   - Use `setAddrWindow()` for partial updates
   - Minimize full-screen redraws
   - Cache font metrics

---

This guide provides the blueprint for implementing a fully functional TFT display driver. Start with Steps 1-5 to get the basic splash sequence working, then incrementally add Steps 6-9 for full functionality.
