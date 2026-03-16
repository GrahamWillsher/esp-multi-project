/*
 * Global Variable Definitions
 * Defines all global state declared in common.h
 */

#include "common.h"

// Logging level
LogLevel current_log_level = LOG_INFO;

// WiFi Configuration (definitions)
namespace Config {
    const char* WIFI_SSID = "BTB-X9FMMG";
    const char* WIFI_PASSWORD = "amnPKhDrXU9GPt";
    const IPAddress LOCAL_IP = IPAddress(192, 168, 1, 230);
    const IPAddress GATEWAY = IPAddress(192, 168, 1, 1);
    const IPAddress SUBNET = IPAddress(255, 255, 255, 0);
    const IPAddress PRIMARY_DNS = IPAddress(8, 8, 8, 8);
    const IPAddress SECONDARY_DNS = IPAddress(8, 8, 4, 4);
}

// Display State (definitions)
namespace Display {
    int16_t tft_background = TFT_BLACK;
    uint8_t current_backlight_brightness = 255;
    uint16_t soc_color_gradient[TOTAL_GRADIENT_STEPS + 1];
    bool soc_gradient_initialized = false;
    unsigned long last_display_update = 0;
}

// ESP-NOW State (definitions)
namespace ESPNow {
    uint8_t received_soc = 50;
    int32_t received_power = 0;
    uint32_t received_voltage_mv = 0;
    
    LEDColor current_led_color = LED_ORANGE;  // Start with orange (medium)
    LEDEffect current_led_effect = LED_EFFECT_FLASH;

    int wifi_channel = 1;
    uint8_t transmitter_mac[6] = {0};  // Will be filled when transmitter connects
    QueueHandle_t queue = NULL;

    volatile uint32_t rx_callback_count = 0;
    volatile uint32_t rx_queue_drop_count = 0;
    volatile uint32_t rx_queue_high_watermark = 0;
}

// FreeRTOS Resources (definitions)
namespace RTOS {
    TaskHandle_t task_test_data = NULL;
    TaskHandle_t task_indicator = NULL;
    TaskHandle_t task_espnow_worker = NULL;
    TaskHandle_t task_display_renderer = NULL;
    TaskHandle_t task_announcement = NULL;
    SemaphoreHandle_t tft_mutex = NULL;
}

// TFT object
TFT_eSPI tft = TFT_eSPI();

// State Machine
SystemState current_state = SystemState::BOOTING;
