#include "littlefs_init.h"
#include "../common.h"
#include <LittleFS.h>
#include <TFT_eSPI.h>

#ifdef USE_LVGL
    #include "../display/display_splash_lvgl.h"
#else
    #include "../display/display_splash.h"
#endif

extern TFT_eSPI tft;

void initlittlefs() {
    LOG_INFO("INIT", "Initializing LittleFS...");
    Serial.flush();
    
    bool littlefs_ok = false;
    if (!LittleFS.begin(false)) {
        LOG_WARN("INIT", "LittleFS mount failed, trying to format...");
        Serial.flush();
        if (!LittleFS.begin(true)) {
            LOG_ERROR("INIT", "LittleFS initialization failed");
            Serial.flush();
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_RED);
            tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("LittleFS INIT FAILED!", Display::SCREEN_WIDTH / 2, Display::SCREEN_HEIGHT / 2);
            smart_delay(4000);
            littlefs_ok = false;
        } else {
            LOG_INFO("INIT", "LittleFS formatted and mounted successfully");
            Serial.flush();
            littlefs_ok = true;
        }
    } else {
        LOG_INFO("INIT", "LittleFS mounted successfully");
        Serial.flush();
        littlefs_ok = true;
    }
    
    if (!littlefs_ok) {
        LOG_WARN("INIT", "Skipping splash screen (LittleFS not available)");
        Serial.flush();
    } else {
        LOG_INFO("INIT", "LittleFS ready - starting splash screen...");
        Serial.flush();
        
        #ifdef USE_LVGL
            // LVGL backend: Call LVGL splash directly (no generic dispatcher)
            LOG_INFO("INIT", "Using LVGL splash screen (direct call)");
            Display::display_splash_lvgl();
        #else
            // TFT backend: Call TFT splash  
            LOG_INFO("INIT", "Using TFT splash screen");
            displaySplashWithFade();
        #endif
        
        LOG_INFO("INIT", "Splash screen complete");
        Serial.flush();
    }
}
