/*
 * Common definitions and global state
 * Shared across all modules
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <LittleFS.h>
#include <JPEGDecoder.h>
#include <esp32common/espnow/common.h>
#include "config/led_config.h"
#include "config/logging_config.h"
#include "state/connection_state.h"

// ═══════════════════════════════════════════════════════════════════════
// Global Namespaces
// ═══════════════════════════════════════════════════════════════════════

// WiFi Configuration
namespace Config {
    extern const char* WIFI_SSID;
    extern const char* WIFI_PASSWORD;
    extern const IPAddress LOCAL_IP;
    extern const IPAddress GATEWAY;
    extern const IPAddress SUBNET;
    extern const IPAddress PRIMARY_DNS;
    extern const IPAddress SECONDARY_DNS;
}

// Display Configuration and State
// NOTE: Hardware GPIO pins moved to src/hal/hardware_config.h
namespace Display {
    // Display dimensions (logical, after rotation)
    // NOTE: For hardware dimensions, see HardwareConfig::DISPLAY_WIDTH/HEIGHT
    constexpr int SCREEN_WIDTH = 320;
    constexpr int SCREEN_HEIGHT = 170;
    
    // Color definitions
    constexpr uint16_t AMBER = 0xFD20;
    constexpr uint16_t LIME = 0x87E0;
    constexpr uint16_t STEELBLUE = 0x49F1;
    
    // SOC/Power range
    constexpr float MIN_SOC_PERCENT = 20.0f;
    constexpr float MAX_SOC_PERCENT = 80.0f;
    constexpr int MAX_POWER = 5000;
    
    // State variables
    extern int16_t tft_background;
    extern uint8_t current_backlight_brightness;
    
    // Color gradient arrays
    constexpr int TOTAL_GRADIENT_STEPS = 500;
    extern uint16_t soc_color_gradient[TOTAL_GRADIENT_STEPS + 1];
    extern bool soc_gradient_initialized;
    
    // Display update tracking
    extern unsigned long last_display_update;
    constexpr unsigned long DISPLAY_UPDATE_INTERVAL = 500;
}

// ESP-NOW State
namespace ESPNow {
    // LED indicator state
    extern LEDColor current_led_color;
    extern LEDEffect current_led_effect;
    
    // Connection state (managed by RxStateMachine, not volatile flags)
    extern int wifi_channel;                // Managed by ChannelManager
    extern uint8_t transmitter_mac[6];      // Transmitter MAC address for sending commands
    
    // Message queue
    constexpr int QUEUE_SIZE = 24;
    extern QueueHandle_t queue;

    // Queue telemetry
    extern volatile uint32_t rx_callback_count;
    extern volatile uint32_t rx_queue_drop_count;
    extern volatile uint32_t rx_queue_high_watermark;
}

// FreeRTOS Resources
namespace RTOS {
    extern TaskHandle_t task_indicator;
    extern TaskHandle_t task_espnow_worker;
    extern TaskHandle_t task_display_renderer;
    extern TaskHandle_t task_announcement;
    extern SemaphoreHandle_t tft_mutex;
}

// TFT object (global for library compatibility)
extern TFT_eSPI tft;

// ═══════════════════════════════════════════════════════════════════════
// State Machine
// ═══════════════════════════════════════════════════════════════════════

enum class SystemState {
    BOOTING,
    WAITING_FOR_TRANSMITTER,
    NORMAL_OPERATION,
    DEGRADED_MODE,
    NETWORK_ERROR,
    DATA_STALE_ERROR,
    ERROR_STATE
};

extern SystemState current_state;
void transition_to_state(SystemState new_state);

// ═══════════════════════════════════════════════════════════════════════
// Error Handling
// ═══════════════════════════════════════════════════════════════════════

enum class ErrorSeverity { WARNING, ERROR, FATAL };
void handle_error(ErrorSeverity severity, const char* component, const char* message);

// ═══════════════════════════════════════════════════════════════════════
// FreeRTOS-Aware Task Helper
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief FreeRTOS-friendly delay with automatic task yielding
 * 
 * This function provides a task-safe alternative to Arduino's delay().
 * Unlike Arduino's delay() which blocks the entire system, smart_delay()
 * intelligently yields to other FreeRTOS tasks during the wait period.
 * 
 * **Key Differences from delay()**:
 * - Uses vTaskDelay() when running in FreeRTOS context (allows other tasks to run)
 * - Falls back to delay() if FreeRTOS scheduler not running (startup/early init)
 * - Prevents watchdog timeouts by keeping scheduler active
 * - Improves overall system responsiveness and concurrency
 * 
 * **When to use**:
 * - ✅ Anywhere in FreeRTOS task code (default choice)
 * - ✅ Hardware initialization delays (power stabilization, settling time)
 * - ✅ Splash screen pauses during display initialization
 * - ✅ Debounce delays in button handlers
 * - ❌ NOT in ISRs (use ISR-safe alternatives instead)
 * - ❌ NOT in critical sections (use explicit timing instead)
 * 
 * **Implementation Details**:
 * - Checks if FreeRTOS scheduler is running via xTaskGetSchedulerState()
 * - Converts milliseconds to FreeRTOS ticks using pdMS_TO_TICKS()
 * - Ensures minimum 1 tick delay even for very small ms values
 * - Falls back to Arduino delay() if scheduler not running
 * 
 * **Timing Notes**:
 * - Minimum effective delay: 1 FreeRTOS tick (typically 10ms on ESP32)
 * - Requested delays < 1 tick will be rounded up to 1 tick
 * - Example: smart_delay(5) will delay ~10ms, not exactly 5ms
 * - For sub-tick precision, use vTaskDelay(pdMS_TO_TICKS(x)) directly
 * 
 * **Usage Examples**:
 * @code
 *   // Wait 100ms, allowing other tasks to run
 *   smart_delay(100);
 *   
 *   // Use named constant for clarity
 *   constexpr uint32_t HARDWARE_STABILIZATION_MS = 100;
 *   smart_delay(HARDWARE_STABILIZATION_MS);
 *   
 *   // Typical display initialization
 *   digitalWrite(DISPLAY_POWER, HIGH);
 *   smart_delay(50);  // Let power stabilize
 *   digitalWrite(DISPLAY_RESET, LOW);
 *   smart_delay(10);  // Hold reset
 *   digitalWrite(DISPLAY_RESET, HIGH);
 *   smart_delay(100); // Let reset complete
 * @endcode
 * 
 * **Performance Impact**:
 * - No overhead in FreeRTOS context (uses efficient task scheduler)
 * - Improves system responsiveness during long waits
 * - Allows other priority-1 and higher tasks to execute
 * - Prevents "busy waiting" and associated power waste
 * 
 * @param ms Duration to delay in milliseconds
 * 
 * @see vTaskDelay() - FreeRTOS equivalent (uses ticks, not milliseconds)
 * @see delay() - Arduino blocking delay (blocks all tasks)
 * @see pdMS_TO_TICKS() - Convert milliseconds to FreeRTOS ticks
 */
void smart_delay(uint32_t ms);

