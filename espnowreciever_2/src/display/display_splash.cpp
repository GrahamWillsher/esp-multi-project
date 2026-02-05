#include "display_splash.h"
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <JPEGDecoder.h>

extern TFT_eSPI tft;

// Optimized JPEG display function - fast rendering from LittleFS
void displaySplashJpeg2(const char* filename) {
    if (!LittleFS.exists(filename)) {
        LOG_ERROR("[JPEG2] File not found: %s", filename);
        return;
    }
    
    File f = LittleFS.open(filename, "r");
    if (!f) {
        LOG_ERROR("[JPEG2] Failed to open: %s", filename);
        return;
    }
    
    size_t fileSize = f.size();
    LOG_DEBUG("[JPEG2] Loading %s (%d bytes)", filename, fileSize);
    
    uint8_t* buffer = (uint8_t*)malloc(fileSize);
    if (!buffer) {
        LOG_ERROR("[JPEG2] Memory allocation failed");
        f.close();
        return;
    }
    
    if (f.read(buffer, fileSize) != fileSize) {
        LOG_ERROR("[JPEG2] File read error");
        free(buffer);
        f.close();
        return;
    }
    f.close();
    
    if (!JpegDec.decodeArray(buffer, fileSize)) {
        LOG_ERROR("[JPEG2] Decode failed");
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
    LOG_DEBUG("[JPEG2] Displayed %dx%d at (%d,%d)", JpegDec.width, JpegDec.height, xOffset, yOffset);
}

void displaySplashScreenContent() {
    LOG_DEBUG("[SPLASH] Displaying splash screen content...");
    
    tft.fillScreen(TFT_BLACK);
    LOG_DEBUG("[SPLASH] Screen cleared");

    const char* splashFile = "/BatteryEmulator4_320x170.jpg";
    displaySplashJpeg2(splashFile);
    
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
        y += 30;
        tft.setCursor(x, y);
        tft.println(text);
    }
    LOG_DEBUG("[SPLASH] Splash screen content displayed");
}

void fadeBacklight(uint8_t targetBrightness, uint32_t durationMs) {
    if (Display::current_backlight_brightness == targetBrightness) {
        LOG_DEBUG("Backlight already at target brightness: %d", targetBrightness);
        return;
    }
    
    const uint16_t steps = 100;
    uint32_t stepDelay = durationMs / steps;
    if (stepDelay < 5) stepDelay = 5;
    
    int16_t startBrightness = Display::current_backlight_brightness;
    int16_t brightnessDelta = targetBrightness - startBrightness;
    
    LOG_DEBUG("Fading backlight from %d to %d in %d steps (%dms delay)", 
                  startBrightness, targetBrightness, steps, stepDelay);
    
    for (uint16_t step = 0; step <= steps; step++) {
        float progress = (float)step / (float)steps;
        float brightnessFloat = startBrightness + (brightnessDelta * progress);
        uint8_t brightness = (uint8_t)(brightnessFloat + 0.5f);
        
        #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
        ledcWrite(0, brightness);
        #else
        ledcWrite(Display::PIN_LCD_BL, brightness);
        #endif
        
        if (step < steps) {
            smart_delay(stepDelay);
        }
    }
    
    Display::current_backlight_brightness = targetBrightness;
    LOG_DEBUG("Backlight fade complete - final brightness: %d", targetBrightness);
}

void displaySplashWithFade() {
    LOG_INFO("[SPLASH] === Starting Splash Screen Sequence ===");
    
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(0, 0);
    #else
    ledcWrite(Display::PIN_LCD_BL, 0);
    #endif
    Display::current_backlight_brightness = 0;
    smart_delay(200);
    
    LOG_DEBUG("[SPLASH] Displaying content...");
    displaySplashScreenContent();
    LOG_DEBUG("[SPLASH] Content displayed");
    
    LOG_DEBUG("[SPLASH] Fading in splash screen...");
    fadeBacklight(255, 2000);
    LOG_DEBUG("[SPLASH] Fade in complete");
    
    smart_delay(3000);
    
    LOG_DEBUG("[SPLASH] Fading out splash screen...");
    fadeBacklight(0, 2000);
    LOG_DEBUG("[SPLASH] Fade out complete");
    
    tft.fillScreen(TFT_BLACK);
    LOG_DEBUG("[SPLASH] Screen cleared, backlight remains OFF");
    LOG_INFO("[SPLASH] === Splash Screen Sequence Complete ===");
}
