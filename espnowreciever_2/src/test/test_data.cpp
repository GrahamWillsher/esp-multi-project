#include "test_data.h"
#include "../common.h"
#include "../display/display_core.h"
#include "../display/display_led.h"

extern void notify_sse_data_updated();

void generate_test_data() {
    static unsigned long lastTestUpdate = 0;
    static bool socIncreasing = true;
    static bool powerIncreasing = true;
    
    if (millis() - lastTestUpdate > 2000) {
        lastTestUpdate = millis();
        
        // Animate SOC (20% to 80%)
        if (socIncreasing) {
            TestMode::soc += 1;
            if (TestMode::soc >= 80) socIncreasing = false;
        } else {
            TestMode::soc -= 1;
            if (TestMode::soc <= 20) socIncreasing = true;
        }
        
        // Animate power (-4000W to +4000W)
        if (powerIncreasing) {
            TestMode::power += 500;
            if (TestMode::power >= 4000) powerIncreasing = false;
        } else {
            TestMode::power -= 500;
            if (TestMode::power <= -4000) powerIncreasing = true;
        }
        
        LOG_TRACE("Generated test: SOC=%d%%, Power=%ldW", TestMode::soc, TestMode::power);
        notify_sse_data_updated();
    }
}

void task_generate_test_data(void *parameter) {
    LOG_DEBUG("TestDataGen task started");
    
    for (;;) {
        if (TestMode::enabled) {
            generate_test_data();
            
            // Update display with mutex protection
            if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                display_soc((float)TestMode::soc);
                display_power(TestMode::power);
                xSemaphoreGive(RTOS::tft_mutex);
            }
        }
        
        smart_delay(1000);
    }
}

// Status indicator task
void taskStatusIndicator(void *parameter) {
    LOG_DEBUG("StatusIndicator task started");
    
    for (;;) {
        // Flash LED based on current SOC band color from ESP-NOW
        flash_led(ESPNow::current_led_color);  // Flash LED (default 1000ms cycle)
        
        static int heartbeat = 0;
        if (++heartbeat % 10 == 0) {
            LOG_DEBUG("Heartbeat %d - Test Mode: %s", 
                         heartbeat, TestMode::enabled ? "ON" : "OFF");
        }
        
        smart_delay(500);
    }
}
