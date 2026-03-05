/*
 * ESP32 T-Display-S3 - ESP-NOW Receiver with Display
 * Using official TFT_eSPI library as used in LilyGo examples
 * 
 * *** PHASE 2: File split into modular structure ***
 * *** PHASE 3: Display HAL abstraction for testability ***
 */

#include "common.h"
#include "helpers.h"
#include "state_machine.h"
#include "display/display_led.h"
#include "display/display_core.h"
#include "display/display_splash.h"
#include "display/display_manager.h"
#include "hal/display/tft_espi_display_driver.h"

#include "espnow/espnow_callbacks.h"
#include "espnow/espnow_tasks.h"
#include "espnow/rx_connection_handler.h"
#include "espnow/rx_heartbeat_manager.h"
#include "state/connection_state_manager.h"
#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_task.h"
#include "hal/hardware_config.h"
#ifdef USE_LVGL
#include "hal/display/lvgl_driver.h"
#endif
#include <connection_manager.h>
#include <connection_event_processor.h>
#include <channel_manager.h>
#include "config/wifi_setup.h"
#include "config/littlefs_init.h"
#include "../lib/webserver/webserver.h"
#include "../lib/webserver/utils/transmitter_manager.h"
#include "../lib/webserver/utils/receiver_config_manager.h"
#include "../lib/receiver_config/receiver_config_manager.h"  // ReceiverNetworkConfig
#include <espnow_discovery.h>  // Common ESP-NOW discovery component
#include <firmware_version.h>
#include <firmware_metadata.h>  // Embed firmware metadata in binary

// ═══════════════════════════════════════════════════════════════════════
// Globals
// ═══════════════════════════════════════════════════════════════════════

// Hardware display instance (lives for entire application lifetime)
static TFT_eSPI tft_hardware = TFT_eSPI();

// Display driver (wraps TFT_eSPI with HAL interface)
static HAL::TftEspiDisplayDriver tft_driver(tft_hardware);

// DEBUG SWITCH: keep disabled for normal boot; this probe uses direct TFT test frames.
static constexpr bool PRE_LITTLEFS_DEBUG_HALT = false;

static void run_pre_littlefs_debug_and_halt() {
    LOG_WARN("PREBOOT", "============================================");
    LOG_WARN("PREBOOT", "PRE-LITTLEFS DEBUG MODE ENABLED (HALTING)");
    LOG_WARN("PREBOOT", "This runs BEFORE initlittlefs() by request.");
    LOG_WARN("PREBOOT", "============================================");

    // CRITICAL: Initialize TFT hardware first (tft is just declared, not initialized yet)
    LOG_WARN("PREBOOT", "Initializing TFT hardware for debug probe...");
    
    // Enable panel power first
    pinMode(HardwareConfig::GPIO_DISPLAY_POWER, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_DISPLAY_POWER, HIGH);
    smart_delay(100);
    
    // Force backlight OFF before TFT init
    pinMode(HardwareConfig::GPIO_BACKLIGHT, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_BACKLIGHT, LOW);
    
    // Configure backlight PWM at 0
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcSetup(HardwareConfig::BACKLIGHT_PWM_CHANNEL, 
              HardwareConfig::BACKLIGHT_FREQUENCY_HZ, 
              HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
    ledcAttachPin(HardwareConfig::GPIO_BACKLIGHT, HardwareConfig::BACKLIGHT_PWM_CHANNEL);
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, 0);
    #else
    ledcAttach(HardwareConfig::GPIO_BACKLIGHT, 
               HardwareConfig::BACKLIGHT_FREQUENCY_HZ, 
               HardwareConfig::BACKLIGHT_RESOLUTION_BITS);
    ledcWrite(HardwareConfig::GPIO_BACKLIGHT, 0);
    #endif

    // Initialize TFT
    tft_hardware.init();
    tft_hardware.setRotation(1);  // Landscape
    tft_hardware.setSwapBytes(true);
    LOG_WARN("PREBOOT", "TFT hardware initialized");

    LOG_WARN("PREBOOT", "Step 1: Backlight forced OFF for 2s");
    tft_hardware.fillScreen(TFT_BLACK);
    smart_delay(2000);

    // Turn backlight ON while keeping black frame, to catch unexpected white frame
    LOG_WARN("PREBOOT", "Step 2: Backlight ON, screen should remain BLACK for 3s");
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, 255);
    #else
    ledcWrite(HardwareConfig::GPIO_BACKLIGHT, 255);
    #endif
    tft_hardware.fillScreen(TFT_BLACK);
    smart_delay(3000);

    // Visual checkpoints so we know direct panel writes are stable pre-LittleFS
    LOG_WARN("PREBOOT", "Step 3: Showing RED/GREEN/BLUE test frames");
    tft_hardware.fillScreen(TFT_RED);
    smart_delay(1000);
    tft_hardware.fillScreen(TFT_GREEN);
    smart_delay(1000);
    tft_hardware.fillScreen(TFT_BLUE);
    smart_delay(1000);
    tft_hardware.fillScreen(TFT_BLACK);

    LOG_WARN("PREBOOT", "HALT: Program stopped BEFORE initlittlefs().");
    LOG_WARN("PREBOOT", "Observe display + serial logs now.");
    while (true) {
        smart_delay(50);
    }
}

// ═══════════════════════════════════════════════════════════════════════

void setup() {
    // Force backlight OFF immediately at boot to prevent pre-splash white flash
    pinMode(HardwareConfig::GPIO_BACKLIGHT, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_BACKLIGHT, LOW);

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

    // Initialize Display Manager with TFT driver (provides HAL abstraction)
    Display::DisplayManager::init(&tft_driver);
    LOG_INFO("MAIN", "Display HAL initialized");

    // Initialize TFT display and backlight (legacy function - uses global tft object)
    // TODO: Refactor to use Display::DisplayManager for HAL abstraction
    init_display();
    
    // Pre-LittleFS debug probe (requested): stop here to inspect startup behavior
    if (PRE_LITTLEFS_DEBUG_HALT) {
        run_pre_littlefs_debug_and_halt();
    }

    // Initialize LittleFS filesystem
    initlittlefs();

    // Load receiver network configuration from NVS
    ReceiverNetworkConfig::loadConfig();

    // Initialize WiFi with static IP and connect to network
    setupWiFi();

    // Initialize receiver-side configuration cache (local static data)
    ReceiverConfigManager::init();

    // Initialize transmitter cache from NVS (write-through cache)
    TransmitterManager::init();
    
    // Initialize web server (after WiFi is connected)
    LOG_INFO("MAIN", "Initializing web server...");
    init_webserver();
    LOG_INFO("MAIN", "Web server initialized");
    
    // Initialize ESP-NOW (but don't register callback yet - queue must be created first)
    esp_wifi_set_ps(WIFI_PS_NONE);

    LOG_INFO("MAIN", "Initializing ESP-NOW...");
    if (esp_now_init() != ESP_OK) {
        handle_error(ErrorSeverity::FATAL, "ESP-NOW", "Initialization failed");
    }
    LOG_INFO("MAIN", "ESP-NOW initialized on WiFi channel %d", WiFi.channel());
    LOG_DEBUG("MAIN", "ESP-NOW and WiFi STA coexist on same channel");
    
    // Display initial ready screen (splash was already shown during LittleFS init)
    displayInitialScreen();
    
    LOG_INFO("MAIN", "===== Setup complete =====");
    smart_delay(1000);
    
    // Don't clear screen here - let data display handle it when it starts
    // tft.fillScreen(Display::tft_background);
    
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

    // Initialize centralized receiver connection/data state manager
    ConnectionStateManager::init();
    
    // CRITICAL: Setup message routes BEFORE starting worker task
    // This prevents race condition where PROBE messages arrive before handlers are registered
    LOG_DEBUG("MAIN", "Setting up ESP-NOW message routes...");
    setup_message_routes();  // From espnow_tasks.cpp
    LOG_DEBUG("MAIN", "ESP-NOW message routes initialized");
    
    // Create FreeRTOS tasks
    LOG_DEBUG("MAIN", "Creating FreeRTOS tasks...");

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
            return ConnectionStateManager::is_transmitter_connected();
        },
        5000,  // 5 second interval
        1,     // Low priority
        4096   // Stack size (increased for MqttLogger usage)
    );
    
    // Task: MQTT Client (priority 0, core 1) - low priority, receives spec data
    xTaskCreatePinnedToCore(
        task_mqtt_client,
        "MqttClient",
        4096,
        NULL,
        0,
        NULL,
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
    SystemStateManager::instance().init();
    transition_to_state(SystemState::WAITING_FOR_TRANSMITTER);
    
    // NOW register ESP-NOW callbacks (queue is ready)
    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_espnow_sent);
    LOG_DEBUG("MAIN", "ESP-NOW callbacks registered");
    
    transition_to_state(SystemState::WAITING_FOR_TRANSMITTER);
}

// ═══════════════════════════════════════════════════════════════════════
// LOOP (now minimal - tasks handle all functionality)
// ═══════════════════════════════════════════════════════════════════════

void loop() {
    // All functionality is now handled by FreeRTOS tasks
    // Heartbeat periodic check
    RxHeartbeatManager::instance().tick();

    // Receiver-side timeout/state transitions
    SystemStateManager::instance().update();

    // Yield to scheduler
    smart_delay(10);
}
