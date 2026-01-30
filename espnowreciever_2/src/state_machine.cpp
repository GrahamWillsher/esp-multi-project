/*
 * State Machine and Error Handling
 */

#include "common.h"
#include "display/display_led.h"

// State transition function
void transition_to_state(SystemState new_state) {
    if (current_state == new_state) return;
    
    LOG_INFO("[STATE] Transitioning: %d -> %d", (int)current_state, (int)new_state);
    
    // Exit current state
    switch (current_state) {
        case SystemState::TEST_MODE:
            if (RTOS::task_test_data != NULL) {
                LOG_INFO("[STATE] Stopping test data task");
                vTaskDelete(RTOS::task_test_data);
                RTOS::task_test_data = NULL;
            }
            TestMode::enabled = false;
            break;
        case SystemState::BOOTING:
        case SystemState::WAITING_FOR_TRANSMITTER:
        case SystemState::NORMAL_OPERATION:
        case SystemState::ERROR_STATE:
            break;
    }
    
    // Enter new state
    switch (new_state) {
        case SystemState::TEST_MODE:
            LOG_INFO("[STATE] Entering TEST_MODE");
            TestMode::enabled = true;
            break;
            
        case SystemState::NORMAL_OPERATION:
            LOG_INFO("[STATE] Entering NORMAL_OPERATION");
            
            // Direct TFT access with mutex
            if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                Display::tft_background = TFT_BLACK;  // Update background color variable
                tft.fillScreen(TFT_BLACK);
                init_led_gradients();
                xSemaphoreGive(RTOS::tft_mutex);
            }
            break;
            
        case SystemState::ERROR_STATE:
            LOG_ERROR("[STATE] Entering ERROR_STATE");
            if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                tft.fillScreen(TFT_RED);
                xSemaphoreGive(RTOS::tft_mutex);
            }
            break;
            
        case SystemState::BOOTING:
        case SystemState::WAITING_FOR_TRANSMITTER:
            break;
    }
    
    current_state = new_state;
}

// Error handling function
void handle_error(ErrorSeverity severity, const char* component, const char* message) {
    switch (severity) {
        case ErrorSeverity::WARNING:
            LOG_WARN("[%s] %s", component, message);
            break;
        case ErrorSeverity::ERROR:
            LOG_ERROR("[%s] %s", component, message);
            break;
        case ErrorSeverity::FATAL:
            LOG_ERROR("[FATAL] [%s] %s", component, message);
            break;
    }
    
    if (severity == ErrorSeverity::FATAL) {
        transition_to_state(SystemState::ERROR_STATE);
        
        if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            tft.fillScreen(TFT_RED);
            tft.setTextColor(TFT_WHITE, TFT_RED);
            tft.setTextDatum(MC_DATUM);
            tft.setTextSize(2);
            tft.drawString("FATAL ERROR", Display::SCREEN_WIDTH / 2, Display::SCREEN_HEIGHT / 2 - 20);
            tft.setTextSize(1);
            tft.drawString(component, Display::SCREEN_WIDTH / 2, Display::SCREEN_HEIGHT / 2);
            tft.drawString(message, Display::SCREEN_WIDTH / 2, Display::SCREEN_HEIGHT / 2 + 15);
            xSemaphoreGive(RTOS::tft_mutex);
        }
        
        // Flash LED directly (no queue)
        while (true) {
            flash_led(LED_RED, 500);
            smart_delay(500);
        }
    } else if (severity == ErrorSeverity::ERROR) {
        flash_led(LED_ORANGE, 1000);
    }
}
