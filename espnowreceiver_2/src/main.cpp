/*
 * ESP32 T-Display-S3 - ESP-NOW Receiver with Display
 * Using official TFT_eSPI library as used in LilyGo examples
 * 
 * *** PHASE 2: File split into modular structure ***
 * *** PHASE 3: Display HAL abstraction for testability ***
 */

#include "common.h"
#include "helpers.h"
#include "config/task_config.h"
#include "config/runtime_task_startup.h"
#include "state_machine.h"
#include <runtime_common_utils/bootstrap_phase_runner.h>
#include "display/display_led.h"
#include "display/display.h"
#include "display/display_splash.h"
#include "display/display_update_queue.h"

#include "espnow/espnow_callbacks.h"
#include "espnow/espnow_tasks.h"
#include "espnow/rx_connection_handler.h"
#include "espnow/rx_heartbeat_manager.h"
#include "espnow/rx_state_machine.h"
#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_task.h"
#include "hal/hardware_config.h"
#ifdef USE_LVGL
#include "hal/display/lvgl_driver.h"
#endif
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/connection_event_processor.h>
#include <channel_manager.h>
#include <esp32common/config/timing_config.h>
#include "config/wifi_setup.h"
#include "config/littlefs_init.h"
#include "../lib/webserver/webserver.h"
#include "../lib/webserver/utils/transmitter_manager.h"
#include "../lib/webserver/utils/receiver_config_manager.h"
#include "../lib/receiver_config/receiver_config_manager.h"  // ReceiverNetworkConfig
#include <espnow_discovery.h>  // Common ESP-NOW discovery component
#include <firmware_version.h>
#include <firmware_metadata.h>  // Embed firmware metadata in binary
#include <runtime_common_utils/ota_boot_guard.h>
#include <runtime_common_utils/setup_health_gate.h>

// ═══════════════════════════════════════════════════════════════════════
// Globals
// ═══════════════════════════════════════════════════════════════════════

// DEBUG SWITCH: keep disabled for normal boot; this probe uses direct TFT test frames.
static constexpr bool PRE_LITTLEFS_DEBUG_HALT = false;

static void log_timing_policy() {
    LOG_INFO("TIMING", "Startup: serial=%lu wifi_stabilize=%lu post_init=%lu component=%lu",
             static_cast<unsigned long>(TimingConfig::STARTUP.serial_init_delay_ms),
             static_cast<unsigned long>(TimingConfig::STARTUP.wifi_radio_stabilization_ms),
             static_cast<unsigned long>(TimingConfig::STARTUP.post_init_delay_ms),
             static_cast<unsigned long>(TimingConfig::STARTUP.component_init_delay_ms));
    LOG_INFO("TIMING", "Discovery: retry=%lu deferred=%lu announce=%lu probe=%lu tx_per_channel=%lu",
             static_cast<unsigned long>(TimingConfig::DISCOVERY.retry_interval_ms),
             static_cast<unsigned long>(TimingConfig::DISCOVERY.deferred_poll_ms),
             static_cast<unsigned long>(TimingConfig::DISCOVERY.announcement_interval_ms),
             static_cast<unsigned long>(TimingConfig::DISCOVERY.probe_interval_ms),
             static_cast<unsigned long>(TimingConfig::DISCOVERY.transmit_duration_per_channel_ms));
    LOG_INFO("TIMING", "Heartbeat: interval=%lu timeout=%lu connect=%lu ack=%lu",
             static_cast<unsigned long>(TimingConfig::HEARTBEAT.interval_ms),
             static_cast<unsigned long>(TimingConfig::HEARTBEAT.timeout_ms),
             static_cast<unsigned long>(TimingConfig::HEARTBEAT.espnow_connecting_timeout_ms),
             static_cast<unsigned long>(TimingConfig::HEARTBEAT.ack_timeout_ms));
    LOG_INFO("TIMING", "MQTT: startup=%lu poll=%lu reconnect=%lu publish=%lu max_retry=%lu",
             static_cast<unsigned long>(TimingConfig::MQTT.task_startup_delay_ms),
             static_cast<unsigned long>(TimingConfig::MQTT.task_poll_ms),
             static_cast<unsigned long>(TimingConfig::MQTT.reconnect_interval_ms),
             static_cast<unsigned long>(TimingConfig::MQTT.publish_interval_ms),
             static_cast<unsigned long>(TimingConfig::MQTT.max_retry_delay_ms));
    LOG_INFO("TIMING", "Loops: main=%lu queue_flush=%lu metrics=%lu peer_audit=%lu",
             static_cast<unsigned long>(TimingConfig::LOOPS.main_loop_delay_ms),
             static_cast<unsigned long>(TimingConfig::LOOPS.queue_flush_poll_delay_ms),
             static_cast<unsigned long>(TimingConfig::LOOPS.metrics_report_interval_ms),
             static_cast<unsigned long>(TimingConfig::LOOPS.peer_audit_interval_ms));
}

static void task_led_renderer(void* parameter) {
    (void)parameter;

    // Initialize gradients once display/mutex are ready
    if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        init_led_gradients();
        xSemaphoreGive(RTOS::tft_mutex);
    }

    bool first_frame = true;
    bool led_is_on = false;
    uint32_t next_toggle_ms = 0;
    LEDColor last_color = LED_ORANGE;
    LEDEffect last_effect = LED_EFFECT_FLASH;

    for (;;) {
        const uint32_t now_ms = millis();
        const LEDColor color = ESPNow::current_led_color;
        const LEDEffect effect = ESPNow::current_led_effect;

        // Reset animation state on mode/color change
        if (first_frame || color != last_color || effect != last_effect) {
            led_is_on = false;
            next_toggle_ms = now_ms;
        }

        switch (effect) {
            case LED_EFFECT_CONTINUOUS: {
                // Solid ON, redraw only on change
                if (!led_is_on || first_frame || color != last_color || effect != last_effect) {
                    if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        set_led(color);
                        xSemaphoreGive(RTOS::tft_mutex);
                        led_is_on = true;
                    }
                }
                break;
            }

            case LED_EFFECT_HEARTBEAT: {
                // Brief pulse (on ~180ms, off ~1020ms), non-blocking
                if (now_ms >= next_toggle_ms) {
                    if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                        if (led_is_on) {
                            clear_led();
                            led_is_on = false;
                            next_toggle_ms = now_ms + 1020;
                        } else {
                            set_led(color);
                            led_is_on = true;
                            next_toggle_ms = now_ms + 180;
                        }
                        xSemaphoreGive(RTOS::tft_mutex);
                    }
                }
                break;
            }

            case LED_EFFECT_FLASH:
            default: {
                // Symmetric blink (on 500ms, off 500ms), non-blocking
                if (now_ms >= next_toggle_ms) {
                    if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                        if (led_is_on) {
                            clear_led();
                            led_is_on = false;
                        } else {
                            set_led(color);
                            led_is_on = true;
                        }
                        next_toggle_ms = now_ms + 500;
                        xSemaphoreGive(RTOS::tft_mutex);
                    }
                }
                break;
            }
        }

        first_frame = false;
        last_color = color;
        last_effect = effect;

        // Keep task responsive and low-contention
        smart_delay(20);
    }
}

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
    tft.init();
    tft.setRotation(1);  // Landscape
    tft.setSwapBytes(true);
    LOG_WARN("PREBOOT", "TFT hardware initialized");

    LOG_WARN("PREBOOT", "Step 1: Backlight forced OFF for 2s");
    tft.fillScreen(TFT_BLACK);
    smart_delay(2000);

    // Turn backlight ON while keeping black frame, to catch unexpected white frame
    LOG_WARN("PREBOOT", "Step 2: Backlight ON, screen should remain BLACK for 3s");
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(HardwareConfig::BACKLIGHT_PWM_CHANNEL, 255);
    #else
    ledcWrite(HardwareConfig::GPIO_BACKLIGHT, 255);
    #endif
    tft.fillScreen(TFT_BLACK);
    smart_delay(3000);

    // Visual checkpoints so we know direct panel writes are stable pre-LittleFS
    LOG_WARN("PREBOOT", "Step 3: Showing RED/GREEN/BLUE test frames");
    tft.fillScreen(TFT_RED);
    smart_delay(1000);
    tft.fillScreen(TFT_GREEN);
    smart_delay(1000);
    tft.fillScreen(TFT_BLUE);
    smart_delay(1000);
    tft.fillScreen(TFT_BLACK);

    LOG_WARN("PREBOOT", "HALT: Program stopped BEFORE initlittlefs().");
    LOG_WARN("PREBOOT", "Observe display + serial logs now.");
    while (true) {
        smart_delay(50);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// BOOTSTRAP PHASE FUNCTIONS
// setup() is decomposed into ordered phases.  Each phase owns exactly one
// system layer.  Execution order must be preserved; later phases depend on
// resources established by earlier ones.
// ═══════════════════════════════════════════════════════════════════════

// --- Phase 1: Hardware -------------------------------------------------------
// Serial port, backlight suppression, firmware metadata, OTA boot guard.
// No subsystem dependencies.
static void bootstrap_hardware() {
    // Force backlight OFF immediately at boot to prevent pre-splash white flash
    pinMode(HardwareConfig::GPIO_BACKLIGHT, OUTPUT);
    digitalWrite(HardwareConfig::GPIO_BACKLIGHT, LOW);

    Serial.begin(115200);
    smart_delay(TimingConfig::SERIAL_INIT_DELAY_MS);
    LOG_INFO("MAIN", "\n========================================");
    LOG_INFO("MAIN", "ESP32 T-Display-S3 ESP-NOW Receiver");

    char fwInfo[128];
    FirmwareMetadata::getInfoString(fwInfo, sizeof(fwInfo), false);
    LOG_INFO("MAIN", "%s", fwInfo);

    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        LOG_INFO("MAIN", "Built: %s", FirmwareMetadata::metadata.build_date);
    }

    LOG_INFO("MAIN", "Build: %s %s", __DATE__, __TIME__);
    log_timing_policy();
    LOG_INFO("MAIN", "========================================");
    Serial.flush();

    OtaBootGuard::begin("RX_BOOT_GUARD");
}

// --- Phase 2: Display --------------------------------------------------------
// Hardware display init, optional pre-LittleFS debug halt.
// Depends on: GPIO (Phase 1).
static void bootstrap_display() {
    init_display();
    LOG_INFO("MAIN", "Display system initialized");

    if (PRE_LITTLEFS_DEBUG_HALT) {
        run_pre_littlefs_debug_and_halt();
    }
}

// --- Phase 3: Filesystem & Network Config ------------------------------------
// LittleFS mount, receiver NVS config load, WiFi bring-up.
// Depends on: display ready (splash shown during LittleFS init).
static void bootstrap_filesystem() {
    initlittlefs();
    ReceiverNetworkConfig::loadConfig();
    setupWiFi();
}

// --- Phase 4: Services -------------------------------------------------------
// Receiver/transmitter caches, webserver, ESP-NOW radio init.
// Depends on: WiFi (Phase 3).
static void bootstrap_services() {
    ReceiverConfigManager::init();
    TransmitterManager::init();

    LOG_INFO("MAIN", "Initializing web server...");
    init_webserver();
    LOG_INFO("MAIN", "Web server initialized");

    esp_wifi_set_ps(WIFI_PS_NONE);

    LOG_INFO("MAIN", "Initializing ESP-NOW...");
    if (esp_now_init() != ESP_OK) {
        handle_error(ErrorSeverity::FATAL, "ESP-NOW", "Initialization failed");
    }
    LOG_INFO("MAIN", "ESP-NOW initialized on WiFi channel %d", WiFi.channel());
    LOG_DEBUG("MAIN", "ESP-NOW and WiFi STA coexist on same channel");
}

// --- Phase 5: Display Content ------------------------------------------------
// Show initial ready screen after services are ready.
// Depends on: services (Phase 4).
static void bootstrap_display_content() {
    displayInitialScreen();
}

// --- Phase 6: FreeRTOS Tasks -------------------------------------------------
// Create runtime primitives (queues/mutexes) and start all background tasks.
// Depends on: services ready (Phase 4).
static void bootstrap_tasks() {
    RuntimeTaskStartup::create_runtime_primitives();
    RuntimeTaskStartup::start_runtime_tasks(task_led_renderer);
}

// --- Phase 7: ESP-NOW State Machines -----------------------------------------
// Wire up channel manager, connection manager, heartbeat, state machine,
// ESP-NOW callbacks and initial state transition.
// Depends on: tasks started (Phase 6) — connection manager requires scheduler.
static void bootstrap_espnow_state() {
    LOG_INFO("CHANNEL", "Initializing channel manager...");
    if (!ChannelManager::instance().init()) {
        LOG_ERROR("CHANNEL", "Failed to initialize channel manager!");
    }

    LOG_INFO("STATE", "Initializing common connection manager...");
    if (!EspNowConnectionManager::instance().init()) {
        LOG_ERROR("STATE", "Failed to initialize common connection manager!");
    }

    EspNowConnectionManager::instance().set_auto_reconnect(true);
    EspNowConnectionManager::instance().set_connecting_timeout_ms(TimingConfig::ESPNOW_CONNECTING_TIMEOUT_MS);

    create_connection_event_processor(3, 0);
    ReceiverConnectionHandler::instance().init();

    RxHeartbeatManager::instance().init();
    LOG_INFO("HEARTBEAT", "RX Heartbeat manager initialized (90s timeout)");

    SystemStateManager::instance().init();

    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_espnow_sent);
    LOG_DEBUG("MAIN", "ESP-NOW callbacks registered");

    transition_to_state(SystemState::WAITING_FOR_TRANSMITTER);
}

// ═══════════════════════════════════════════════════════════════════════

void setup() {
    static const BootstrapPhaseRunner::Phase kBootstrapPhases[] = {
        {"hardware",       bootstrap_hardware},
        {"display",        bootstrap_display},
        {"filesystem",     bootstrap_filesystem},
        {"services",       bootstrap_services},
        {"display_content",bootstrap_display_content},
        {"tasks",          bootstrap_tasks},
        {"espnow_state",   bootstrap_espnow_state},
    };

    BootstrapPhaseRunner::run_phases(
        kBootstrapPhases,
        sizeof(kBootstrapPhases) / sizeof(kBootstrapPhases[0])
    );

    {
        const SetupHealthGate::Check checks[] = {
            {"heap_ok", ESP.getFreeHeap() > 32768},
            {"mutex_ok", RTOS::tft_mutex != nullptr},
            {"espnow_queue_ok", ESPNow::queue != nullptr},
        };

        const SetupHealthGate::Outcome outcome = SetupHealthGate::apply(
            "RX_BOOT_GUARD",
            checks,
            sizeof(checks) / sizeof(checks[0]),
            "receiver setup health gate failed",
            "receiver setup health gate passed");

        if (outcome == SetupHealthGate::Outcome::Error) {
            LOG_ERROR("BOOT_GUARD", "Receiver setup health gate helper returned error");
        }
    }

    LOG_INFO("MAIN", "Setup complete! All 7 bootstrap phases done.");
}

// ═══════════════════════════════════════════════════════════════════════
// LOOP (now minimal - tasks handle all functionality)
// ═══════════════════════════════════════════════════════════════════════

void loop() {
    // All functionality is now handled by FreeRTOS tasks
    // Heartbeat periodic check
    RxHeartbeatManager::instance().tick();

    // Retry REQUEST_DATA if power-profile stream hasn't started yet
    ReceiverConnectionHandler::instance().tick();

    // Receiver-side timeout/state transitions
    SystemStateManager::instance().update();

    // Yield to scheduler
    smart_delay(10);
}
