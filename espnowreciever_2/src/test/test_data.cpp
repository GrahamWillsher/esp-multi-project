#include "test_data.h"
#include "../common.h"
#include "../display/display_core.h"
#include "../display/display_led.h"

extern void notify_sse_data_updated();

void generate_test_data() {
    static unsigned long lastTestUpdate = 0;
    static bool socIncreasing = true;
    
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
        
        // Generate random power value between -4000W and +4000W
        TestMode::power = random(-4000, 4001);

        // Simulate pack voltage based on SOC (30.0V to 42.0V default range)
        const uint32_t min_mv = 30000;
        const uint32_t max_mv = 42000;
        uint32_t clamped_soc = (TestMode::soc < 0) ? 0 : (TestMode::soc > 100 ? 100 : (uint32_t)TestMode::soc);
        TestMode::voltage_mv = min_mv + ((max_mv - min_mv) * clamped_soc) / 100;
        
        LOG_TRACE("TEST", "Generated test: SOC=%d%%, Power=%ldW", TestMode::soc, TestMode::power);
        notify_sse_data_updated();
    }
}

void task_generate_test_data(void *parameter) {
    LOG_DEBUG("TEST", "TestDataGen task started");
    
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
    LOG_DEBUG("TEST", "StatusIndicator task started");
    
    for (;;) {
        // Choose LED behavior (simulated mode can override effect)
        LEDColor color = ESPNow::current_led_color;
        LEDEffect effect = ESPNow::current_led_effect;

        if (TestMode::enabled) {
            // Simulated: color based on SOC bands
            if (TestMode::soc <= 25) {
                color = LED_RED;
            } else if (TestMode::soc <= 50) {
                color = LED_ORANGE;
            } else {
                color = LED_GREEN;
            }

            // Simulated: effect based on power magnitude
            if (abs(TestMode::power) >= 2000) {
                effect = LED_EFFECT_FLASH;
            } else {
                effect = LED_EFFECT_HEARTBEAT;
            }
        }

        switch (effect) {
            case LED_EFFECT_SOLID:
                set_led(color);
                smart_delay(500);
                break;
            case LED_EFFECT_HEARTBEAT:
                heartbeat_led(color);
                break;
            case LED_EFFECT_FLASH:
            default:
                flash_led(color);  // Flash LED (default 1000ms cycle)
                break;
        }
        
        static int heartbeat = 0;
        if (++heartbeat % 10 == 0) {
            LOG_DEBUG("TEST", "Heartbeat %d - Test Mode: %s", 
                      heartbeat, TestMode::enabled ? "ON" : "OFF");
        }
        
        smart_delay(500);
    }
}
