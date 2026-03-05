# LVGL Display Initialization - White Block Issue Findings

## Summary
After analyzing the **official LilyGo T-Display-S3 LVGL reference implementation** from their GitHub repository, I've identified the root causes and solutions for the white block appearing before your splash screen.

---

## Critical Differences Found

### 1. **Backlight Initialization Order** ⚠️ CRITICAL
**Your Current Code Problem:**
- You're initializing backlight with `ledcWrite(0, 0)` or `ledcWrite(PIN_LCD_BL, 0)` separately
- This leaves the backlight OFF but the TFT hardware is already powered
- When TFT is powered but backlight is off, random display content/noise can show as white blocks

**LilyGo Reference Implementation:**
```cpp
// From lv_demos.ino - Backlight IMMEDIATELY after panel init, with gradient fade
ledcSetup(0, 10000, 8);
ledcAttachPin(PIN_LCD_BL, 0);

// Gradient fade from 0 to 255 - THIS IS KEY!
for (uint8_t i = 0; i < 0xFF; i++)
{
    ledcWrite(0, i);
    delay(2);
}
```

**Why This Works:**
- The gradient fade ensures controlled initialization of the display
- Starting at brightness 0 and gradually increasing prevents visual artifacts
- The 2ms delay between each step allows the panel to settle
- This is done **BEFORE** LVGL initialization

---

### 2. **LVGL Initialization Sequence**

**Critical Order (from LilyGo):**
```cpp
// 1. FIRST: Fade backlight in
for (uint8_t i = 0; i < 0xFF; i++)
{
    ledcWrite(0, i);
    delay(2);
}

// 2. Initialize LVGL
lv_init();

// 3. Allocate display buffer with DMA-capable memory
lv_disp_buf = (lv_color_t *)heap_caps_malloc(
    LVGL_LCD_BUF_SIZE * sizeof(lv_color_t), 
    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
);

// 4. Initialize draw buffer
lv_disp_draw_buf_init(&disp_buf, lv_disp_buf, NULL, LVGL_LCD_BUF_SIZE);

// 5. Initialize and register display driver
lv_disp_drv_init(&disp_drv);
disp_drv.hor_res = EXAMPLE_LCD_H_RES;
disp_drv.ver_res = EXAMPLE_LCD_V_RES;
disp_drv.flush_cb = example_lvgl_flush_cb;
disp_drv.draw_buf = &disp_buf;
disp_drv.user_data = panel_handle;
lv_disp_drv_register(&disp_drv);
```

---

### 3. **Memory Allocation - DMA Capability** ⚠️ IMPORTANT
**Your current approach vs. LilyGo:**

LilyGo uses:
```cpp
lv_disp_buf = (lv_color_t *)heap_caps_malloc(
    LVGL_LCD_BUF_SIZE * sizeof(lv_color_t), 
    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL  // <-- KEY: DMA allocation
);
```

This ensures the buffer is allocated in memory that can be used for DMA transfers, preventing corruption and white blocks.

---

### 4. **Display Driver Registration** ⚠️ CRITICAL
**Missing in your code:**
```cpp
// The flush callback MUST be set
disp_drv.flush_cb = example_lvgl_flush_cb;

// The panel handle MUST be passed to driver
disp_drv.user_data = panel_handle;

// Registration must happen BEFORE using any LVGL functions
lv_disp_drv_register(&disp_drv);

// Flag that LVGL is ready
is_initialized_lvgl = true;
```

If the driver isn't properly registered with the flush callback, LVGL will try to render but the display won't be updated correctly, causing visual artifacts.

---

### 5. **Backlight PWM Configuration**
**LilyGo uses:**
```cpp
ledcSetup(0, 10000, 8);      // channel 0, 10kHz frequency, 8-bit resolution
ledcAttachPin(PIN_LCD_BL, 0);  // Attach to PWM channel
```

**Parameters explained:**
- **Frequency: 10kHz** - Prevents visible flicker
- **Resolution: 8-bit** - 256 brightness levels (0-255)
- **Channel 0** - Uses first available PWM channel

---

### 6. **Hardware Reset Sequence**
**LilyGo (ESP-IDF API):**
```cpp
esp_lcd_panel_reset(panel_handle);
esp_lcd_panel_init(panel_handle);
esp_lcd_panel_invert_color(panel_handle, true);
esp_lcd_panel_swap_xy(panel_handle, true);
esp_lcd_panel_mirror(panel_handle, false, true);
esp_lcd_panel_set_gap(panel_handle, 0, 35);

// For newer ST7789 modules, additional configuration
for (uint8_t i = 0; i < (sizeof(lcd_st7789v) / sizeof(lcd_cmd_t)); i++)
{
    esp_lcd_panel_io_tx_param(io_handle, lcd_st7789v[i].cmd,
                              lcd_st7789v[i].data, lcd_st7789v[i].len & 0x7f);
    if (lcd_st7789v[i].len & 0x80)
        delay(120);
}
```

The **gap setting** `esp_lcd_panel_set_gap(panel_handle, 0, 35)` is CRITICAL for T-Display-S3!
- This offsets the display by 35 pixels vertically
- Without this, content shifts and white blocks can appear

---

## Root Cause of White Block

The white block appears because:

1. **TFT hardware is powered on (GPIO15 HIGH)** ✓
2. **Backlight is OFF or not properly faded in** ✗
3. **LVGL isn't initialized or driver isn't registered** ✗
4. **Display shows random/garbage content** → Appears as white (or noise pattern)
5. **Backlight suddenly turns on** → White block is visible before splash screen

---

## Recommended Fix Implementation

### Step 1: Update init_display() function
Replace your display initialization with:

```cpp
void init_display() {
    LOG_INFO("DISPLAY", "Initializing TFT display...");
    
    // 1. Enable display power
    pinMode(Display::PIN_POWER_ON, OUTPUT);
    digitalWrite(Display::PIN_POWER_ON, HIGH);
    smart_delay(100);
    
    // 2. Initialize TFT hardware
    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);
    
    // 3. Setup backlight with PWM
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcSetup(0, 10000, 8);        // channel, frequency, resolution
    ledcAttachPin(Display::PIN_LCD_BL, 0);
    #else
    ledcAttach(Display::PIN_LCD_BL, 10000, 8);
    #endif
    
    // 4. **CRITICAL**: Fade backlight in with gradient (prevents white block)
    for (uint8_t brightness = 0; brightness < 255; brightness++) {
        #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
        ledcWrite(0, brightness);
        #else
        ledcWrite(Display::PIN_LCD_BL, brightness);
        #endif
        smart_delay(2);  // Allow panel to settle
    }
    
    // 5. Clear screen with black after backlight is stable
    tft.fillScreen(TFT_BLACK);
    
    LOG_INFO("DISPLAY", "Display initialized and backlight faded in");
    Display::current_backlight_brightness = 255;
}
```

### Step 2: Ensure DisplayManager properly initializes LVGL
When using LVGL, ensure:
```cpp
// Allocate with DMA-capable memory
lv_disp_buf = (lv_color_t *)heap_caps_malloc(
    LVGL_LCD_BUF_SIZE * sizeof(lv_color_t),
    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
);

// Register display driver BEFORE showing anything
lv_disp_draw_buf_init(&disp_buf, lv_disp_buf, NULL, LVGL_LCD_BUF_SIZE);
lv_disp_drv_init(&disp_drv);
disp_drv.hor_res = SCREEN_WIDTH;
disp_drv.ver_res = SCREEN_HEIGHT;
disp_drv.flush_cb = your_flush_callback;  // MUST be set
disp_drv.draw_buf = &disp_buf;
lv_disp_drv_register(&disp_drv);  // MUST be called before rendering
```

### Step 3: Remove separate splash screen backlight control
Your `displaySplashWithFade()` should not:
- Turn backlight completely OFF (`ledcWrite(0, 0)`)
- Use `fadeBacklight(0, 2000)` at startup

Instead, backlight should stay ON and transition to display content.

---

## Key Takeaways from LilyGo Reference

✅ **Backlight gradient fade-in (0→255)** is essential
✅ **2ms delay between fade steps** allows hardware to settle
✅ **DMA-capable memory allocation** for LVGL buffers
✅ **Display driver registration BEFORE first render**
✅ **Flush callback properly configured**
✅ **10kHz PWM frequency** prevents visible flicker

❌ **DO NOT** leave backlight OFF during initialization
❌ **DO NOT** skip the gradient fade
❌ **DO NOT** initialize LVGL before backlight is ready
❌ **DO NOT** render content before driver is registered

---

## Files to Review in LilyGo Repository

- **lv_demos.ino** - Complete reference implementation
- **pin_config.h** - Correct pin definitions and buffer sizes
- Both show the exact sequence for error-free LVGL initialization

---

## Implementation Priority

1. **HIGH** - Add backlight gradient fade (0→255 with 2ms delays)
2. **HIGH** - Ensure driver registration before first render
3. **HIGH** - Use DMA-capable memory for LVGL buffers
4. **MEDIUM** - Verify PWM frequency (10kHz)
5. **MEDIUM** - Remove startup backlight OFF sequence
