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
    if (!LittleFS.begin(false)) {
        Serial.println("[WARN] LittleFS mount failed, trying to format...");
        Serial.flush();
        if (!LittleFS.begin(true)) {
            Serial.println("[ERROR] LittleFS initialization failed!");
            Serial.flush();
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
