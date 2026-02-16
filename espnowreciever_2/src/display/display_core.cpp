#include "display_core.h"
#include "../helpers.h"
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

// Initialize TFT display hardware and backlight
void init_display() {
    Serial.println("[INIT] Initializing display...");
    Serial.flush();

    // Turn on display power (CRITICAL for T-Display-S3!)
    pinMode(Display::PIN_POWER_ON, OUTPUT);
    digitalWrite(Display::PIN_POWER_ON, HIGH);
    Serial.println("[INIT] PIN_POWER_ON (GPIO15) set to HIGH");
    Serial.flush();
    
    delay(100);  // Wait for power to stabilize
    
    // Initialize TFT
    tft.init();
    tft.setRotation(1);  // Landscape mode (320x170)
    tft.setSwapBytes(true);  // Ensure correct byte order for TFT
    
    // Setup backlight pin - keep OFF initially to prevent flash
    pinMode(Display::PIN_LCD_BL, OUTPUT);
    digitalWrite(Display::PIN_LCD_BL, LOW);  // Keep backlight OFF initially
    
    Serial.println("[INIT] Display initialized");
    Serial.flush();
    
    // Configure backlight PWM (but keep it OFF - splash fade will turn it on)
    Serial.println("[INIT] Configuring backlight...");
    Serial.flush();
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcSetup(0, 2000, 8);
    ledcAttachPin(Display::PIN_LCD_BL, 0);
    ledcWrite(0, 0);  // Start with backlight OFF
    #else
    ledcAttach(Display::PIN_LCD_BL, 200, 8);
    ledcWrite(Display::PIN_LCD_BL, 0);  // Start with backlight OFF
    #endif
    Display::current_backlight_brightness = 0;  // Initialize tracking variable to OFF
    Serial.println("[INIT] Backlight configured (OFF - waiting for splash)");
    Serial.flush();
}

// Display initial ready screen with backlight fade-in
void displayInitialScreen() {
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
    
    // Now turn backlight on to full brightness (status messages are drawn)
    LOG_DEBUG("DISPLAY", "Turning on backlight...");
    Serial.flush();
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(0, 255);
    #else
    ledcWrite(Display::PIN_LCD_BL, 255);
    #endif
    Display::current_backlight_brightness = 255;
    LOG_DEBUG("DISPLAY", "Backlight enabled at full brightness");
    Serial.flush();
}

// Display a number with proportional font, centering each digit in equal-width boxes
// OPTIMIZED: Only redraws digits that have changed for smoother, more efficient display
void display_centered_proportional_number(const GFXfont* font, float number, uint16_t color, int center_x, int center_y) {
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
        Serial.printf("Proportional number display: max digit width=%d (digit '8' + 3px margins each side), decimal point width=%d, height=%d\n", 
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
    static float lastSoC = -1.0f;
    static const int displayY = Display::SCREEN_HEIGHT / 3;
    
    // Early return if value hasn't changed
    if (lastSoC == newSoC) {
        return;
    }
    
    // Initialize gradient on first call
    if (!Display::soc_gradient_initialized) {
        pre_calculate_color_gradient(TFT_RED, Display::AMBER, 167, &Display::soc_color_gradient[0]);
        pre_calculate_color_gradient(Display::AMBER, Display::LIME, 167, &Display::soc_color_gradient[167]);
        pre_calculate_color_gradient(Display::LIME, TFT_GREEN, 166, &Display::soc_color_gradient[334]);
        Display::soc_gradient_initialized = true;
        Serial.println("SOC color gradient initialized (500 steps)");
    }
    
    // Map SOC to gradient index (0-500)
    float span = (Display::MAX_SOC_PERCENT - Display::MIN_SOC_PERCENT);
    if (span <= 0.0001f) span = 1.0f;
    
    float new_normalized = (newSoC - Display::MIN_SOC_PERCENT) / span;
    
    // Clamp to valid range
    if (new_normalized < 0.0f) new_normalized = 0.0f;
    if (new_normalized > 1.0f) new_normalized = 1.0f;
    
    int newIndex = (int)(new_normalized * (float)Display::TOTAL_GRADIENT_STEPS);
    if (newIndex > Display::TOTAL_GRADIENT_STEPS) newIndex = Display::TOTAL_GRADIENT_STEPS;
    
    // Get color from gradient
    uint16_t socColor = Display::soc_color_gradient[newIndex];
    
    // Display new SOC value using centered proportional number function
    display_centered_proportional_number(&FreeSansBold18pt7b, newSoC, socColor, Display::SCREEN_WIDTH / 2, displayY);
    
    lastSoC = newSoC;
}

void display_power(int32_t current_power_w) {
    static const int displayY = (Display::SCREEN_HEIGHT * 5) / 6;
    const int SCREEN_CENTER_X = Display::SCREEN_WIDTH / 2;
    const int textY = displayY + 15;
    
    static int32_t lastPower = INT32_MAX;

    // Early return if value hasn't changed
    if (lastPower == current_power_w) {
        return;
    }
    lastPower = current_power_w;
    
    // Calculate bar character width once
    static int MAX_BARS_PER_SIDE = -1;
    static int barCharWidth = 0;
    static uint16_t gradientGreen[30];
    static uint16_t gradientRed[30];
    
    if (MAX_BARS_PER_SIDE == -1) {
        tft.setFreeFont(&FreeSansBold12pt7b);
        barCharWidth = tft.textWidth("-");
        
        MAX_BARS_PER_SIDE = (Display::SCREEN_WIDTH / 2) / barCharWidth;
        if (MAX_BARS_PER_SIDE > 30) MAX_BARS_PER_SIDE = 30;
        
        pre_calculate_color_gradient(TFT_BLUE, TFT_GREEN, MAX_BARS_PER_SIDE - 1, gradientGreen);
        pre_calculate_color_gradient(TFT_BLUE, TFT_RED, MAX_BARS_PER_SIDE - 1, gradientRed);
        
        Serial.printf("Power bar setup: char width=%d, bars per side=%d\n", barCharWidth, MAX_BARS_PER_SIDE);
    }
    
    // Clamp input power to valid range
    int clamped_power = current_power_w;
    if (clamped_power < -Display::MAX_POWER) clamped_power = -Display::MAX_POWER;
    if (clamped_power > Display::MAX_POWER) clamped_power = Display::MAX_POWER;
    
    // Calculate target bars
    int target_bars = (clamped_power * MAX_BARS_PER_SIDE) / Display::MAX_POWER;
    
    if (clamped_power > 0 && target_bars == 0) target_bars = 1;
    else if (clamped_power < 0 && target_bars == 0) target_bars = -1;
    
    static int previous_bars = 0;
    
    // Lambda function to draw a single bar
    auto drawBar = [&](int barIndex, bool isNegative, uint16_t color) {
        int offset = isNegative ? -barIndex : barIndex;
        int barX = SCREEN_CENTER_X - (barCharWidth / 2) + (offset * barCharWidth);
        tft.setCursor(barX, displayY);
        tft.setTextColor(color);
        tft.setFreeFont(&FreeSansBold12pt7b);
        tft.print("-");
    };
    
    // Detect if direction stayed the same for pulsing effect
    bool sameDirection = (previous_bars < 0 && target_bars < 0) || (previous_bars > 0 && target_bars > 0);
    bool shouldPulse = sameDirection && (abs(target_bars) == abs(previous_bars));
    
    // Handle ripple/pulse effect
    if (shouldPulse && abs(target_bars) > 0) {
        bool isNegative = target_bars < 0;
        int numBars = abs(target_bars);
        const int DELAY_PER_BAR_MS = 30;
        
        for (int ripplePos = 0; ripplePos <= numBars; ripplePos++) {
            for (int i = 0; i < numBars; i++) {
                uint16_t barColor = isNegative ? gradientGreen[i] : gradientRed[i];
                uint16_t displayColor = (i == ripplePos && ripplePos < numBars) ? 
                    ((barColor >> 1) & 0x7BEF) : barColor;
                drawBar(i, isNegative, displayColor);
            }
            
            if (ripplePos < numBars) {
                smart_delay(DELAY_PER_BAR_MS);
            }
        }
        
        return;
    }
    
    // Special handling for zero power
    if (clamped_power == 0) {
        static bool lastWasZero = false;
        
        if (!lastWasZero && previous_bars != 0) {
            int prevAbs = abs(previous_bars);
            bool prevNegative = previous_bars < 0;
            for (int i = 0; i < prevAbs; i++) {
                drawBar(i, prevNegative, Display::tft_background);
            }
        }
        
        drawBar(0, false, TFT_BLUE);
        lastWasZero = true;
        previous_bars = 0;
    }
    else if (target_bars != previous_bars) {
        static bool lastWasZero = false;
        lastWasZero = false;
        
        int prevAbs = abs(previous_bars);
        int targetAbs = abs(target_bars);
        bool prevNegative = previous_bars < 0;
        bool targetNegative = target_bars < 0;
        
        // Direction changed
        if ((previous_bars < 0 && target_bars > 0) || (previous_bars > 0 && target_bars < 0)) {
            for (int i = 0; i < prevAbs; i++) {
                drawBar(i, prevNegative, Display::tft_background);
            }
            for (int i = 0; i < targetAbs; i++) {
                uint16_t barColor = targetNegative ? gradientGreen[i] : gradientRed[i];
                drawBar(i, targetNegative, barColor);
            }
        }
        // More bars
        else if (targetAbs > prevAbs) {
            for (int i = prevAbs; i < targetAbs; i++) {
                uint16_t barColor = targetNegative ? gradientGreen[i] : gradientRed[i];
                drawBar(i, targetNegative, barColor);
            }
        }
        // Fewer bars
        else if (targetAbs < prevAbs) {
            for (int i = targetAbs; i < prevAbs; i++) {
                drawBar(i, prevNegative, Display::tft_background);
            }
        }
        
        previous_bars = target_bars;
    }
    
    // Display power value text
    static int32_t last_displayed_power = INT32_MAX;
    if (current_power_w != last_displayed_power) {
        tft.fillRect(SCREEN_CENTER_X - 60, textY - 8, 120, 16, Display::tft_background);
        
        tft.setTextSize(1);
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextColor(TFT_WHITE);
        tft.setTextDatum(MC_DATUM);
        // Use char buffer to avoid String heap allocation
        char powerStr[12];
        snprintf(powerStr, sizeof(powerStr), "%dW", clamped_power);
        tft.drawString(powerStr, SCREEN_CENTER_X, textY);
        last_displayed_power = current_power_w;
    }
}
