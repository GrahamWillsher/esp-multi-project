/*
 * State Machine and Error Handling
 */

#include "common.h"
#include "state_machine.h"
#include "state/connection_state_manager.h"
#include "display/display_led.h"
#include "display/display_core.h"
#include "espnow/rx_heartbeat_manager.h"
#include "espnow/rx_state_machine.h"
#include <connection_manager.h>

static bool rx_has_live_data_flow() {
    return RxStateMachine::instance().connection_state() == EspNowDeviceState::ACTIVE;
}

static bool rx_link_traffic_is_recent() {
    return EspNowConnectionManager::instance().ms_since_last_heartbeat() < 20000;
}

SystemStateManager& SystemStateManager::instance() {
    static SystemStateManager inst;
    return inst;
}

void SystemStateManager::init() {
    state_entry_time_ms_ = millis();
}

uint32_t SystemStateManager::get_state_duration_ms() const {
    return millis() - state_entry_time_ms_;
}

void SystemStateManager::update() {
    static SystemState last_seen_state = current_state;
    if (last_seen_state != current_state) {
        state_entry_time_ms_ = millis();
        last_seen_state = current_state;
    }

    const uint32_t elapsed = get_state_duration_ms();

    switch (current_state) {
        case SystemState::BOOTING:
            if (elapsed > BOOT_TIMEOUT_MS) {
                LOG_WARN("STATE", "Boot timeout -> ERROR_STATE");
                transition_to_state(SystemState::ERROR_STATE);
            }
            break;

        case SystemState::WAITING_FOR_TRANSMITTER:
            // Only enter normal operation once real data flow is active.
            // CONNECTED means link established but no payload stream yet, and STALE must not
            // be treated as healthy just because its enum value is above CONNECTED.
            if (rx_has_live_data_flow()) {
                transition_to_state(SystemState::NORMAL_OPERATION);
            } else if (elapsed > TX_WAIT_TIMEOUT_MS && !rx_link_traffic_is_recent()) {
                transition_to_state(SystemState::NETWORK_ERROR);
            }
            break;

        case SystemState::NORMAL_OPERATION: {
            // RxStateMachine is the authoritative source for ESP-NOW link/data freshness.
            const auto rx_state = RxStateMachine::instance().connection_state();
            if (rx_state < EspNowDeviceState::CONNECTED) {
                transition_to_state(SystemState::WAITING_FOR_TRANSMITTER);
            } else if (rx_state == EspNowDeviceState::STALE) {
                transition_to_state(SystemState::DATA_STALE_ERROR);
            }
            break;
        }

        case SystemState::NETWORK_ERROR:
        case SystemState::DATA_STALE_ERROR:
        case SystemState::DEGRADED_MODE:
            // Recovery: only return to normal when the live data stream is active again.
            if (rx_has_live_data_flow()) {
                transition_to_state(SystemState::NORMAL_OPERATION);
            } else if (elapsed > ERROR_RECOVERY_RETRY_MS) {
                transition_to_state(SystemState::WAITING_FOR_TRANSMITTER);
            }
            break;

        case SystemState::ERROR_STATE:
            if (elapsed > ERROR_RECOVERY_RETRY_MS) {
                transition_to_state(SystemState::BOOTING);
            }
            break;
    }
}

// State transition function
void transition_to_state(SystemState new_state) {
    if (current_state == new_state) return;
    
    
    LOG_INFO("STATE", "Transitioning: %d -> %d", (int)current_state, (int)new_state);
    
    // Enter new state
    switch (new_state) {
        case SystemState::NORMAL_OPERATION:
            LOG_INFO("STATE", "Entering NORMAL_OPERATION");
            // Direct TFT access with mutex
            if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                Display::tft_background = TFT_BLACK;  // Update background color variable
                // Don't clear screen here - data display will fill it as it arrives
                // tft.fillScreen(TFT_BLACK);
                init_led_gradients();
                xSemaphoreGive(RTOS::tft_mutex);
            }
            break;
            
        case SystemState::ERROR_STATE:
            LOG_ERROR("STATE", "Entering ERROR_STATE");
            if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                tft.fillScreen(TFT_RED);
                xSemaphoreGive(RTOS::tft_mutex);
            }
            break;

        case SystemState::NETWORK_ERROR:
            LOG_WARN("STATE", "Entering NETWORK_ERROR");
            break;

        case SystemState::DATA_STALE_ERROR:
            LOG_WARN("STATE", "Entering DATA_STALE_ERROR");
            break;

        case SystemState::DEGRADED_MODE:
            LOG_WARN("STATE", "Entering DEGRADED_MODE");
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
            LOG_WARN(component, "%s", message);
            break;
        case ErrorSeverity::ERROR:
            LOG_ERROR(component, "%s", message);
            break;
        case ErrorSeverity::FATAL:
            LOG_ERROR(component, "FATAL: %s", message);
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
