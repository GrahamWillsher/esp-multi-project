/*
 * ESP32 T-Display-S3 - ESP-NOW Receiver with Display
 * Using official TFT_eSPI library as used in LilyGo examples
 * 
 * *** PHASE 2: File split into modular structure ***
 */

#include "common.h"
#include "helpers.h"
#include "state_machine.h"
#include "display/display_led.h"
#include "display/display_core.h"
#include "display/display_splash.h"
#include "espnow/espnow_callbacks.h"
#include "espnow/espnow_tasks.h"
#include "test/test_data.h"
#include "config/wifi_setup.h"
#include "config/littlefs_init.h"
#include "../lib/webserver/webserver.h"
#include <espnow_discovery.h>  // Common ESP-NOW discovery component

// Forward declaration for status indicator task (defined in test_data.cpp)
void taskStatusIndicator(void *parameter);

// ═══════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    smart_delay(1000);  // Give serial time to initialize
    LOG_INFO("\n========================================");
    LOG_INFO("ESP32 T-Display-S3 ESP-NOW Receiver");
    LOG_INFO("Build: %s %s", __DATE__, __TIME__);
    LOG_INFO("========================================");
    Serial.flush();



    // Initialize TFT display and backlight
    init_display();
    
    // Initialize LittleFS filesystem
    initlittlefs();

    // Initialize WiFi with static IP and connect to network
    setupWiFi();
    
    // Initialize ESP-NOW (but don't register callback yet - queue must be created first)
    esp_wifi_set_ps(WIFI_PS_NONE);

    LOG_INFO("Initializing ESP-NOW...");
    if (esp_now_init() != ESP_OK) {
        handle_error(ErrorSeverity::FATAL, "ESP-NOW", "Initialization failed");
    }
    LOG_INFO("ESP-NOW initialized on WiFi channel %d", WiFi.channel());
    LOG_DEBUG("ESP-NOW and WiFi STA coexist on same channel");
    
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
    LOG_DEBUG("Turning on backlight...");
    Serial.flush();
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(0, 255);
    #else
    ledcWrite(Display::PIN_LCD_BL, 255);
    #endif
    Display::current_backlight_brightness = 255;
    LOG_DEBUG("Backlight enabled at full brightness");
    Serial.flush();
    
    LOG_INFO("===== Setup complete =====");
    smart_delay(1000);
    
    // Clear and prepare for data display
    tft.fillScreen(Display::tft_background);
    
    // Create mutex for TFT display access
    RTOS::tft_mutex = xSemaphoreCreateMutex();
    if (RTOS::tft_mutex == NULL) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create TFT mutex");
    }
    LOG_DEBUG("TFT mutex created");
    
    // Create ESP-NOW message queue
    ESPNow::queue = xQueueCreate(ESPNow::QUEUE_SIZE, sizeof(espnow_queue_msg_t));
    if (ESPNow::queue == NULL) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create ESP-NOW queue");
    }
    LOG_DEBUG("ESP-NOW queue created (size=%d)", ESPNow::QUEUE_SIZE);
    
    // Create FreeRTOS tasks
    LOG_DEBUG("Creating FreeRTOS tasks...");
    tft.fillScreen(Display::tft_background);
    
    // Task: ESP-NOW Worker (priority 2, core 1) - highest priority for message processing
    xTaskCreatePinnedToCore(
        task_espnow_worker,
        "ESPNowWorker",
        4096,
        NULL,
        2,                   // Higher priority than display tasks
        &RTOS::task_espnow_worker,
        1
    );
    
    // Start periodic announcement using common discovery component
    // (creates its own internal task, no need to wrap it)
    LOG_DEBUG("Starting periodic announcement task...");
    EspnowDiscovery::instance().start(
        []() -> bool {
            return ESPNow::transmitter_connected;
        },
        5000,  // 5 second interval
        1,     // Low priority
        2048   // Stack size
    );
    
    // Task: Generate Test Data (priority 1, core 1) - updates display directly
    xTaskCreatePinnedToCore(
        task_generate_test_data,
        "TestDataGen",
        4096,
        NULL,
        1,
        &RTOS::task_test_data,
        1
    );
    
    // Task: Status Indicator (priority 0, core 1)
    xTaskCreatePinnedToCore(
        taskStatusIndicator,
        "StatusIndicator",
        2048,
        NULL,
        0,
        &RTOS::task_indicator,
        1
    );
    
    LOG_DEBUG("All tasks created successfully");
    
    // *** PHASE 2: Initialize state machine ***
    transition_to_state(SystemState::TEST_MODE);
    
    // NOW register ESP-NOW callbacks (queue is ready)
    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_espnow_sent);
    LOG_DEBUG("ESP-NOW callbacks registered");
    
    // Initialize web server (ESP-IDF http_server in lib/webserver.cpp)
    init_webserver();
    if (WiFi.status() == WL_CONNECTED) {
        LOG_INFO("Web server: http://%s", WiFi.localIP().toString().c_str());
    }
}

// ═══════════════════════════════════════════════════════════════════════
// LOOP (now minimal - tasks handle all functionality)
// ═══════════════════════════════════════════════════════════════════════

void loop() {
    // All functionality is now handled by FreeRTOS tasks
    // This loop just yields to the scheduler
    smart_delay(1000);
}
