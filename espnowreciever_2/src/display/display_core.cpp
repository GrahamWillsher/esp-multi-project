#include "display_core.h"
#include "display_splash.h"
#include "display_manager.h"
#include "pages/status_page.h"
#include "../helpers.h"
#include "../hal/hardware_config.h"
#include "../hal/display/tft_espi_display_driver.h"
#include <TFT_eSPI.h>
#include <logging_config.h>

// ════════════════════════════════════════════════════════════════════════════
// Global TFT Hardware Instance
// ════════════════════════════════════════════════════════════════════════════
// IMPORTANT: This is the single TFT_eSPI instance used throughout the codebase.
// It is wrapped by the DisplayManager HAL for thread-safe access, but can be
// accessed directly when HAL methods aren't sufficient (e.g., for advanced operations).
extern TFT_eSPI tft;

// ════════════════════════════════════════════════════════════════════════════
// Helper Functions
// ════════════════════════════════════════════════════════════════════════════

// Get reference to global TFT_eSPI instance (for TFT library-specific operations)
// Generally prefer DisplayManager::get_driver() for portable code
TFT_eSPI& get_tft_hardware() {
    return tft;
}

// Helper function to get display driver safely
static HAL::IDisplayDriver* get_display_driver() {
    return Display::DisplayManager::get_driver();
}

// Global status page instance
static Display::Pages::StatusPage* g_status_page = nullptr;

// Get or create status page
static Display::Pages::StatusPage* get_status_page() {
    if (!g_status_page) {
        HAL::IDisplayDriver* driver = get_display_driver();
        if (driver) {
            g_status_page = new Display::Pages::StatusPage(driver);
        }
    }
    return g_status_page;
}

// Initialize TFT display hardware and backlight via DisplayManager
void init_display() {
    LOG_INFO("DISPLAY", "Initializing display via DisplayManager+HAL...");
    
    // Create and initialize DisplayManager with driver
    // NOTE: The driver's init() will handle TFT initialization and setSwapBytes()
    static HAL::TftEspiDisplayDriver tft_driver(tft);
    
    if (!Display::DisplayManager::init(&tft_driver)) {
        LOG_ERROR("DISPLAY", "Failed to initialize DisplayManager");
        return;
    }
    
    LOG_INFO("DISPLAY", "Display initialized successfully via HAL");
}

// Display initial ready screen with backlight fade-in
void displayInitialScreen() {
    // TFT path: original text-based ready screen
    // Use global tft object (can also be accessed via get_tft_hardware())
    
    // Clear screen and show ready message
    tft.fillScreen(Display::tft_background);

    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(2);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Ready", Display::SCREEN_WIDTH / 2, 10);
    
    // Show test mode status
    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("Test Mode: ON", Display::SCREEN_WIDTH / 2, 35);
    
    // Fade in backlight gradually (prevent flash)
    LOG_DEBUG("DISPLAY", "Fading in backlight...");
    fadeBacklight(255, 1000);  // Fade to full brightness over 1 second
    LOG_DEBUG("DISPLAY", "Backlight fade-in complete");
}

// Display a number with proportional font, centering each digit in equal-width boxes
// OPTIMIZED: Only redraws digits that have changed for smoother, more efficient display
void display_centered_proportional_number(const GFXfont* font, float number, uint16_t color, int center_x, int center_y) {
    // Use global tft object
    
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
    tft.setFreeFont(font);
    tft.setTextSize(2);  // No scaling for proportional fonts
    
    // Recalculate if font changed or first call
    if (maxDigitWidth == 0 || lastFont != font) {
        maxDigitWidth = tft.textWidth("8") + 6;  // Width of '8' + 3px left + 3px right (total 6px margin)
        maxDigitHeight = tft.fontHeight() + 6;   // Font height + 3px top + 3px bottom (total 6px margin)
        
        // Calculate decimal point width separately (it's much narrower than digits)
        decimalPointWidth = tft.textWidth(".") + 6;  // Width of '.' + 3px left + 3px right (total 6px margin)
        
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
    tft.setFreeFont(font);
    tft.setTextColor(color);
    
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
            tft.fillRect(lastStartX, clearY, oldTotalWidth, maxDigitHeight, Display::tft_background);
            
            // Draw all digits since we cleared everything
            int currentX = startX;
            for (int i = 0; i < numDigits; i++) {
                char digitStr[2] = {numStr[i], '\0'};
                int dw = tft.textWidth(digitStr);
                int charWidth = (numStr[i] == '.') ? decimalPointWidth : maxDigitWidth;
                int digitX = currentX + ((charWidth - dw) / 2);
                tft.setCursor(digitX, baselineY);
                tft.print(digitStr);
                currentX += charWidth;
            }
        } else {
            // Same number of digits - clear and redraw ALL digits with new color
            int currentX = startX;
            for (int i = 0; i < numDigits; i++) {
                char digitStr[2] = {numStr[i], '\0'};
                int charWidth = (numStr[i] == '.') ? decimalPointWidth : maxDigitWidth;
                tft.fillRect(currentX, clearY, charWidth, maxDigitHeight, Display::tft_background);
                int dw = tft.textWidth(digitStr);
                int digitX = currentX + ((charWidth - dw) / 2);
                tft.setCursor(digitX, baselineY);
                tft.print(digitStr);
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
