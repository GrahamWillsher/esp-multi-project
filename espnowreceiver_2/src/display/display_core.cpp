#include "display_core.h"
#include "display_splash.h"
#include "display_manager.h"
#include "../helpers.h"
#include <TFT_eSPI.h>
#include <esp32common/logging/logging_config.h>

// ════════════════════════════════════════════════════════════════════════════
// Global TFT Hardware Instance (for initialization only)
// Display rendering should use DisplayManager::get_driver() instead
// ════════════════════════════════════════════════════════════════════════════
extern TFT_eSPI tft;

// Initialize TFT display hardware and backlight
void init_display() {
    LOG_INFO("DISPLAY", "Initializing display...");
    
    // Enable display power (CRITICAL for T-Display-S3)
    pinMode(Display::PIN_POWER_ON, OUTPUT);
    digitalWrite(Display::PIN_POWER_ON, HIGH);
    smart_delay(100);  // Wait for power to stabilize
    
    // Initialize TFT hardware
    tft.init();
    tft.setRotation(1);  // Landscape mode (320x170)
    tft.setSwapBytes(true);
    LOG_DEBUG("DISPLAY", "TFT hardware initialized");
    
    // Setup backlight PWM control
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcSetup(0, 10000, 8);  // channel 0, 10kHz frequency, 8-bit resolution
    ledcAttachPin(Display::PIN_LCD_BL, 0);
    #else
    ledcAttach(Display::PIN_LCD_BL, 10000, 8);  // 10kHz, 8-bit
    #endif
    
    // CRITICAL: Gradient fade-in (0→255) prevents white block artifact
    // Based on LilyGo T-Display-S3 reference implementation
    LOG_DEBUG("DISPLAY", "Fading in backlight (0→255 with 2ms steps)...");
    for (uint8_t brightness = 0; brightness < 255; brightness++) {
        #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
        ledcWrite(0, brightness);
        #else
        ledcWrite(Display::PIN_LCD_BL, brightness);
        #endif
        smart_delay(2);  // Allow panel to settle
    }
    
    // Ensure full brightness is set
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(0, 255);
    #else
    ledcWrite(Display::PIN_LCD_BL, 255);
    #endif
    Display::current_backlight_brightness = 255;
    
    // Clear screen with black
    auto* driver = Display::DisplayManager::get_driver();
    driver->fill_screen(TFT_BLACK);
    
    LOG_INFO("DISPLAY", "Display initialized and backlight faded in");
}

// Display initial ready screen
void displayInitialScreen() {
    auto* driver = Display::DisplayManager::get_driver();
    
    // Clear screen and show ready message
    driver->fill_screen(Display::tft_background);

    driver->set_text_color(TFT_GREEN);
    driver->set_text_size(2);
    driver->set_text_datum(TC_DATUM);
    driver->draw_string("Ready", Display::SCREEN_WIDTH / 2, 10, 1);
    
    // Show test mode status
    driver->set_text_size(1);
    driver->set_text_color(TFT_YELLOW);
    driver->draw_string("Test Mode: ON", Display::SCREEN_WIDTH / 2, 35, 1);
    
    LOG_DEBUG("DISPLAY", "Initial screen displayed");
}

// Display a number with proportional font, centering each digit in equal-width boxes
// OPTIMIZED: Only redraws digits that have changed for smoother, more efficient display
void display_centered_proportional_number(const GFXfont* font, float number, uint16_t color, int center_x, int center_y) {
    auto* driver = Display::DisplayManager::get_driver();
    
    // One-time calculation of maximum digit width (using '8' as widest digit + 3px margin each side)
    static int maxDigitWidth = 0;
    static int maxDigitHeight = 0;
    static int decimalPointWidth = 0;  // Separate width for '.' character
    static const GFXfont* lastFont = nullptr;
    
    // Track previous number and position to enable selective redrawing
    static float lastNumber = -999999.0f;  // Initialize to impossible value to force first draw
    static char lastNumStr[12] = "";
    static int lastNumDigits = 0;
    static int lastStartX = 0;
    
    // ALWAYS ensure we're using the correct font at the start
    driver->set_free_font(font);
    driver->set_text_size(2);  // No scaling for proportional fonts
    
    // Recalculate if font changed or first call
    if (maxDigitWidth == 0 || lastFont != font) {
        maxDigitWidth = driver->text_width("8") + 6;  // Width of '8' + 3px left + 3px right (total 6px margin)
        maxDigitHeight = driver->font_height() + 6;   // Font height + 3px top + 3px bottom (total 6px margin)
        
        // Calculate decimal point width separately (it's much narrower than digits)
        decimalPointWidth = driver->text_width(".") + 6;  // Width of '.' + 3px left + 3px right (total 6px margin)
        
        lastFont = font;
        LOG_DEBUG("DISPLAY", "Proportional number: max digit width=%d (digit '8' + margins), decimal width=%d, height=%d",
                  maxDigitWidth, decimalPointWidth, maxDigitHeight);
    }
    
    // Calculate actual baseline Y position from center_y
    int baselineY = center_y + (maxDigitHeight / 4);  // Baseline positioned for vertical centering
    int clearY = center_y - (maxDigitHeight / 2);     // Clear area centered around center_y
    
    // Convert number to string to get individual digits
    char numStr[12];  // Support up to 10 digits plus sign and null
    snprintf(numStr, sizeof(numStr), "%.1f", number);
    int numDigits = strlen(numStr);
    
    // Calculate total width needed
    int totalWidth = 0;
    for (int i = 0; i < numDigits; i++) {
        if (numStr[i] == '.') {
            totalWidth += decimalPointWidth;
        } else {
            totalWidth += maxDigitWidth;
        }
    }
    
    // Calculate starting X position to center around the specified center_x point
    int startX = center_x - (totalWidth / 2);
    
    // Set font and color for drawing
    driver->set_free_font(font);
    driver->set_text_color(color);
    
    // OPTIMIZATION: Redraw all digits when number changes
    if (lastNumber != number) {
        // Check if digit count changed
        if (numDigits != lastNumDigits || startX != lastStartX) {
            // Digit count changed or position shifted - clear the old area completely
            int oldTotalWidth = 0;
            for (int i = 0; i < lastNumDigits; i++) {
                if (lastNumStr[i] == '.') {
                    oldTotalWidth += decimalPointWidth;
                } else {
                    oldTotalWidth += maxDigitWidth;
                }
            }
            driver.fill_rect(lastStartX, clearY, oldTotalWidth, maxDigitHeight, Display::tft_background);
                        driver->fill_rect(lastStartX, clearY, oldTotalWidth, maxDigitHeight, Display::tft_background);
            
            // Draw all digits since we cleared everything
            int currentX = startX;
            for (int i = 0; i < numDigits; i++) {
                char digitStr[2] = {numStr[i], '\0'};
                int dw = driver->text_width(digitStr);
                int charWidth = (numStr[i] == '.') ? decimalPointWidth : maxDigitWidth;
                int digitX = currentX + ((charWidth - dw) / 2);
                driver->set_cursor(digitX, baselineY);
                driver->print(digitStr);
                currentX += charWidth;
            }
        } else {
            // Same number of digits - clear and redraw ALL digits with new color
            int currentX = startX;
            for (int i = 0; i < numDigits; i++) {
                char digitStr[2] = {numStr[i], '\0'};
                int charWidth = (numStr[i] == '.') ? decimalPointWidth : maxDigitWidth;
                driver->fill_rect(currentX, clearY, charWidth, maxDigitHeight, Display::tft_background);
                int dw = driver->text_width(digitStr);
                int digitX = currentX + ((charWidth - dw) / 2);
                driver->set_cursor(digitX, baselineY);
                driver->print(digitStr);
                currentX += charWidth;
            }
        }
        
        // Update tracking variables
        lastNumber = number;
        strcpy(lastNumStr, numStr);
        lastNumDigits = numDigits;
        lastStartX = startX;
    }
}

void display_soc(float newSoC) {
    // Initialize gradient on first call
    if (!Display::soc_gradient_initialized) {
        pre_calculate_color_gradient(TFT_RED, Display::AMBER, 167, &Display::soc_color_gradient[0]);
        pre_calculate_color_gradient(Display::AMBER, Display::LIME, 167, &Display::soc_color_gradient[167]);
        pre_calculate_color_gradient(Display::LIME, TFT_GREEN, 166, &Display::soc_color_gradient[334]);
        Display::soc_gradient_initialized = true;
        LOG_DEBUG("DISPLAY", "SOC color gradient initialized (500 steps)");
    }
    
    // Update via status page widget
    Display::Pages::StatusPage* page = get_status_page();
    if (page) {
        page->update_soc(newSoC);
        page->render();
    }
}

void display_power(int32_t current_power_w) {
    // Update via status page widget
    Display::Pages::StatusPage* page = get_status_page();
    if (page) {
        page->update_power(current_power_w);
        page->render();
    }
}
