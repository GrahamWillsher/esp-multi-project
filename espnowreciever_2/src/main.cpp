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
#include "espnow/rx_connection_handler.h"
#include "espnow/rx_heartbeat_manager.h"
#include <connection_manager.h>
#include <connection_event_processor.h>
#include <channel_manager.h>  // Centralized channel management
#include "test/test_data.h"
#include "config/wifi_setup.h"
#include "config/littlefs_init.h"
#include "../lib/webserver/webserver.h"
#include "../lib/webserver/utils/transmitter_manager.h"
#include "../lib/webserver/utils/receiver_config_manager.h"
#include <espnow_discovery.h>  // Common ESP-NOW discovery component
#include <firmware_version.h>
#include <firmware_metadata.h>  // Embed firmware metadata in binary

// Forward declaration for status indicator task (defined in test_data.cpp)
void taskStatusIndicator(void *parameter);

// ═══════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    smart_delay(1000);  // Give serial time to initialize
    LOG_INFO("MAIN", "\n========================================");
    LOG_INFO("MAIN", "ESP32 T-Display-S3 ESP-NOW Receiver");
    
    // Display firmware metadata using logging system
    char fwInfo[128];
    FirmwareMetadata::getInfoString(fwInfo, sizeof(fwInfo), false);
    LOG_INFO("MAIN", "%s", fwInfo);
    
    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        LOG_INFO("MAIN", "Built: %s", FirmwareMetadata::metadata.build_date);
    }
    
    LOG_INFO("MAIN", "Build: %s %s", __DATE__, __TIME__);
    LOG_INFO("MAIN", "========================================");
    Serial.flush();



    // Initialize TFT display and backlight
    init_display();
    
    // Initialize LittleFS filesystem
    initlittlefs();

    // Initialize WiFi with static IP and connect to network
    setupWiFi();

    // Initialize receiver-side configuration cache (local static data)
    ReceiverConfigManager::init();

    // Initialize transmitter cache from NVS (write-through cache)
    TransmitterManager::init();
    
    // Initialize ESP-NOW (but don't register callback yet - queue must be created first)
    esp_wifi_set_ps(WIFI_PS_NONE);

    LOG_INFO("MAIN", "Initializing ESP-NOW...");
    if (esp_now_init() != ESP_OK) {
        handle_error(ErrorSeverity::FATAL, "ESP-NOW", "Initialization failed");
    }
    LOG_INFO("MAIN", "ESP-NOW initialized on WiFi channel %d", WiFi.channel());
    LOG_DEBUG("MAIN", "ESP-NOW and WiFi STA coexist on same channel");
    
    // Display ready screen and enable backlight
    displayInitialScreen();
    
    LOG_INFO("MAIN", "===== Setup complete =====");
    smart_delay(1000);
    
    // Clear and prepare for data display
    tft.fillScreen(Display::tft_background);
    
    // Create mutex for TFT display access
    RTOS::tft_mutex = xSemaphoreCreateMutex();
    if (RTOS::tft_mutex == NULL) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create TFT mutex");
    }
    LOG_DEBUG("MAIN", "TFT mutex created");
    
    // Create ESP-NOW message queue
    ESPNow::queue = xQueueCreate(ESPNow::QUEUE_SIZE, sizeof(espnow_queue_msg_t));
    if (ESPNow::queue == NULL) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create ESP-NOW queue");
    }
    LOG_DEBUG("MAIN", "ESP-NOW queue created (size=%d)", ESPNow::QUEUE_SIZE);
    
    // CRITICAL: Setup message routes BEFORE starting worker task
    // This prevents race condition where PROBE messages arrive before handlers are registered
    LOG_DEBUG("MAIN", "Setting up ESP-NOW message routes...");
    setup_message_routes();  // From espnow_tasks.cpp
    LOG_DEBUG("MAIN", "ESP-NOW message routes initialized");
    
    // Create FreeRTOS tasks
    LOG_DEBUG("MAIN", "Creating FreeRTOS tasks...");
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
    LOG_DEBUG("MAIN", "Starting periodic announcement task...");
    EspnowDiscovery::instance().start(
        []() -> bool {
            return ESPNow::transmitter_connected;
        },
        5000,  // 5 second interval
        1,     // Low priority
        4096   // Stack size (increased for MqttLogger usage)
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
    
    LOG_DEBUG("MAIN", "All tasks created successfully");
    
    // PHASE C: Initialize channel manager (BEFORE connection manager)
    LOG_INFO("CHANNEL", "Initializing channel manager...");
    if (!ChannelManager::instance().init()) {
        LOG_ERROR("CHANNEL", "Failed to initialize channel manager!");
    }
    
    // PHASE C: Initialize common connection manager (AFTER first task starts)
    // Must be after FreeRTOS scheduler has started
    LOG_INFO("STATE", "Initializing common connection manager...");
    if (!EspNowConnectionManager::instance().init()) {
        LOG_ERROR("STATE", "Failed to initialize common connection manager!");
    }
    
    // Enable auto-reconnect and set timeout
    EspNowConnectionManager::instance().set_auto_reconnect(true);
    EspNowConnectionManager::instance().set_connecting_timeout_ms(30000);  // 30s timeout
    
    create_connection_event_processor(3, 0);
    ReceiverConnectionHandler::instance().init();
    
    // Initialize RX heartbeat manager (after connection manager is ready)
    RxHeartbeatManager::instance().init();
    LOG_INFO("HEARTBEAT", "RX Heartbeat manager initialized (90s timeout)");
    
    // *** PHASE 2: Initialize system state machine ***
    transition_to_state(SystemState::TEST_MODE);
    
    // NOW register ESP-NOW callbacks (queue is ready)
    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_espnow_sent);
    LOG_DEBUG("MAIN", "ESP-NOW callbacks registered");
    
    // Initialize web server (ESP-IDF http_server in lib/webserver.cpp)
    init_webserver();
    if (WiFi.status() == WL_CONNECTED) {
        LOG_INFO("MAIN", "Web server: http://%s", WiFi.localIP().toString().c_str());
    }
}

// ═══════════════════════════════════════════════════════════════════════
// LOOP (now minimal - tasks handle all functionality)
// ═══════════════════════════════════════════════════════════════════════

void loop() {
    // All functionality is now handled by FreeRTOS tasks
    // Heartbeat periodic check
    RxHeartbeatManager::instance().tick();

    // This loop just yields to the scheduler
    smart_delay(1000);
}
