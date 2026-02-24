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
#include <espnow_common.h>
#include "config/logging_config.h"

// LED color enumeration (used by display_led.h and ESPNow state)
// Wire format: 0=red, 1=green, 2=orange (matches enum values)
enum LEDColor {
    LED_RED    = 0,  // Explicitly match wire format
    LED_GREEN  = 1,
    LED_ORANGE = 2
};

// LED effect modes for simulated/status indicator
enum LEDEffect {
    LED_EFFECT_SOLID = 0,
    LED_EFFECT_FLASH = 1,
    LED_EFFECT_HEARTBEAT = 2
};

// Connection state tracking for timeout detection
struct ConnectionState {
    bool is_connected;
    uint32_t last_rx_time_ms;
};

// Actual TFT RGB565 color values for LED display
namespace LEDColors {
    constexpr uint16_t RED    = TFT_RED;     // 0xF800
    constexpr uint16_t GREEN  = TFT_GREEN;   // 0x07E0  
    constexpr uint16_t ORANGE = TFT_ORANGE;  // 0xFD20
}

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
namespace Display {
    // Pin definitions
    constexpr uint8_t PIN_POWER_ON = 15;
    constexpr uint8_t PIN_LCD_BL = 38;
    
    // Display dimensions
    constexpr int SCREEN_WIDTH = 320;
    constexpr int SCREEN_HEIGHT = 170;
    
    // Color definitions
    constexpr uint16_t AMBER = 0xFD20;
    constexpr uint16_t LIME = 0x87E0;
    constexpr uint16_t STEELBLUE = 0x49F1;
    
    // SOC/Power range
    constexpr float MIN_SOC_PERCENT = 20.0f;
    constexpr float MAX_SOC_PERCENT = 80.0f;
    constexpr int MAX_POWER = 4000;
    
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
    // Received data
    extern volatile uint8_t received_soc;
    extern volatile int32_t received_power;
    extern volatile uint32_t received_voltage_mv;
    extern volatile bool data_received;
    
    // Dirty flags for display optimization
    struct DirtyFlags {
        volatile bool soc_changed = false;
        volatile bool power_changed = false;
        volatile bool led_changed = false;
        volatile bool background_changed = false;
    };
    extern DirtyFlags dirty_flags;
    
    // LED indicator state
    extern LEDColor current_led_color;
    extern LEDEffect current_led_effect;
    
    // Connection state
    extern volatile int wifi_channel;
    extern volatile bool transmitter_connected;
    extern uint8_t transmitter_mac[6];  // Transmitter MAC address for sending commands
    
    // Message queue
    constexpr int QUEUE_SIZE = 10;
    extern QueueHandle_t queue;
}

// FreeRTOS Resources
namespace RTOS {
    extern TaskHandle_t task_test_data;
    extern TaskHandle_t task_indicator;
    extern TaskHandle_t task_espnow_worker;
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
// Helper function declaration
void smart_delay(uint32_t ms);

// ═══════════════════════════════════════════════════════════════════════
// Backward Compatibility: Global aliases for namespaced variables
// (Required by webserver and other legacy code)
// ═══════════════════════════════════════════════════════════════════════

// Received data aliases (references to ESPNow namespace)
extern volatile uint8_t& g_received_soc;
extern volatile int32_t& g_received_power;
extern volatile uint32_t& g_received_voltage_mv;
