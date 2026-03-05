#include "display_splash.h"
#include "display_splash_lvgl.h"
#include "../hal/hardware_config.h"
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <JPEGDecoder.h>

extern TFT_eSPI tft;

// Optimized JPEG display function - fast rendering from LittleFS
void displaySplashJpeg2(const char* filename) {
    if (!LittleFS.exists(filename)) {
        LOG_WARN("DISPLAY", "JPEG file not found: %s", filename);
        return;
    }
    
    File f = LittleFS.open(filename, "r");
    if (!f) {
        LOG_ERROR("DISPLAY", "Failed to open JPEG file: %s", filename);
        return;
    }
    
    size_t fileSize = f.size();
    LOG_INFO("DISPLAY", "Loading JPEG: %s (%d bytes)", filename, fileSize);
    
    uint8_t* buffer = (uint8_t*)malloc(fileSize);
    if (!buffer) {
        LOG_ERROR("DISPLAY", "JPEG memory allocation failed");
        f.close();
        return;
    }
    
    if (f.read(buffer, fileSize) != fileSize) {
        LOG_ERROR("DISPLAY", "JPEG file read error");
        free(buffer);
        f.close();
        return;
    }
    f.close();
    
    // Set up JPEGDecoder with TFT output
    JpegDec.setJpgScale(1);
    
    if (!JpegDec.decodeArray(buffer, fileSize)) {
        LOG_ERROR("DISPLAY", "JPEG decode failed");
        free(buffer);
        return;
    }
    
    // Calculate centering position
    int16_t xOffset = (Display::SCREEN_WIDTH - JpegDec.width) / 2;
    int16_t yOffset = (Display::SCREEN_HEIGHT - JpegDec.height) / 2;
    if (xOffset < 0) xOffset = 0;
    if (yOffset < 0) yOffset = 0;
    
    // Fast rendering using pushImage for MCU blocks
    uint16_t* pImg;
    uint16_t mcu_w = JpegDec.MCUWidth;
    uint16_t mcu_h = JpegDec.MCUHeight;
    uint32_t max_x = JpegDec.width;
    uint32_t max_y = JpegDec.height;
    
    while (JpegDec.read()) {
        pImg = JpegDec.pImage;
        
        int16_t mcu_x = JpegDec.MCUx * mcu_w + xOffset;
        int16_t mcu_y = JpegDec.MCUy * mcu_h + yOffset;
        
        uint16_t win_w = (mcu_x + mcu_w <= xOffset + max_x) ? mcu_w : (xOffset + max_x - mcu_x);
        uint16_t win_h = (mcu_y + mcu_h <= yOffset + max_y) ? mcu_h : (yOffset + max_y - mcu_y);
        
        if (win_w > 0 && win_h > 0) {
            tft.pushImage(mcu_x, mcu_y, win_w, win_h, pImg);
        }
    }
    
    free(buffer);
    LOG_INFO("DISPLAY", "JPEG displayed: %dx%d at (%d,%d)", JpegDec.width, JpegDec.height, xOffset, yOffset);
}

void displaySplashScreenContent() {
    LOG_INFO("DISPLAY", "=== Displaying splash screen content ===");
    
    tft.fillScreen(TFT_BLACK);
    LOG_DEBUG("DISPLAY", "Screen cleared");

    const char* splashFile = "/BatteryEmulator4_320x170.jpg";
    displaySplashJpeg2(splashFile);
    
    // Check if the splash image file exists
    bool imageFileExists = LittleFS.exists(splashFile);
    
    if (!imageFileExists) {
        LOG_INFO("DISPLAY", "No splash image file found, showing text fallback");
        
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
        y += 30;
        tft.setCursor(x, y);
        tft.println(text);
        LOG_INFO("DISPLAY", "Text fallback displayed");
    }
    LOG_INFO("DISPLAY", "=== Splash content display complete ===");
}

void fadeBacklight(uint8_t targetBrightness, uint32_t durationMs) {
    if (Display::current_backlight_brightness == targetBrightness) {
        LOG_DEBUG("DISPLAY", "Backlight already at target brightness: %d", targetBrightness);
        return;
    }
    
    const uint16_t steps = 100;
    uint32_t stepDelay = durationMs / steps;
    if (stepDelay < 5) stepDelay = 5;
    
    int16_t startBrightness = Display::current_backlight_brightness;
    int16_t brightnessDelta = targetBrightness - startBrightness;
    
    LOG_DEBUG("DISPLAY", "Fading backlight from %d to %d in %d steps (%dms delay)", 
              startBrightness, targetBrightness, steps, stepDelay);
    
    for (uint16_t step = 0; step <= steps; step++) {
        float progress = (float)step / (float)steps;
        float brightnessFloat = startBrightness + (brightnessDelta * progress);
        uint8_t brightness = (uint8_t)(brightnessFloat + 0.5f);
        
        #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
        ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, brightness);
        #else
        ledcWrite(HardwareConfig::GPIO_BACKLIGHT, brightness);
        #endif
        
        if (step < steps) {
            smart_delay(stepDelay);
        }
    }
    
    Display::current_backlight_brightness = targetBrightness;
    LOG_DEBUG("DISPLAY", "Backlight fade complete - final brightness: %d", targetBrightness);
}

/**
 * DISPATCHER FUNCTION: Calls the correct splash implementation
 * based on which display backend is active.
 * 
 * This unified interface allows code like littlefs_init.cpp to simply call
 * displaySplashWithFade() without knowing whether it's using TFT or LVGL.
 */
void displaySplashWithFade() {
    #ifdef USE_LVGL
        // LVGL backend: use LVGL-specific splash with opacity animations
        LOG_INFO("SPLASH", "displaySplashWithFade() dispatcher: Routing to LVGL splash");
        display_splash_lvgl();
    #else
        // TFT backend: use traditional TFT backlight fade
        LOG_INFO("SPLASH", "displaySplashWithFade() dispatcher: Routing to TFT splash");
        
        LOG_INFO("DISPLAY", "");
        LOG_INFO("DISPLAY", "╔════════════════════════════════════════════════════╗");
        LOG_INFO("DISPLAY", "║  === SPLASH SCREEN SEQUENCE STARTING ===          ║");
        LOG_INFO("DISPLAY", "╚════════════════════════════════════════════════════╝");
        
        LOG_INFO("DISPLAY", "[1/3] Loading splash screen content");
        displaySplashScreenContent();
        LOG_INFO("DISPLAY", "[1/3] ✓ Content loaded");
        
        LOG_INFO("DISPLAY", "[2/3] Displaying splash for 3 seconds");
        smart_delay(3000);
        LOG_INFO("DISPLAY", "[2/3] ✓ Display time complete");
        
        LOG_INFO("DISPLAY", "[3/3] Fading out splash screen (255→0 over 2000ms)...");
        fadeBacklight(0, 2000);
        LOG_INFO("DISPLAY", "[3/3] ✓ Fade out complete - backlight OFF");
        
        tft.fillScreen(TFT_BLACK);
        LOG_INFO("DISPLAY", "");
        LOG_INFO("DISPLAY", "╔════════════════════════════════════════════════════╗");
        LOG_INFO("DISPLAY", "║  === SPLASH SCREEN SEQUENCE COMPLETE ===          ║");
        LOG_INFO("DISPLAY", "╚════════════════════════════════════════════════════╝");
        LOG_INFO("DISPLAY", "");
    #endif
}
