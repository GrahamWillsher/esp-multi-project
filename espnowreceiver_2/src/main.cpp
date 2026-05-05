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
#include "display/display_update_queue.h"

#include "espnow/espnow_callbacks.h"
#include "espnow/espnow_send.h"
#include "espnow/espnow_tasks.h"
#include "espnow/rx_connection_handler.h"
#include "espnow/rx_heartbeat_manager.h"
#include "espnow/rx_state_machine.h"
#include "espnow/type_catalog_cache.h"
#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_task.h"
#include "hal/hardware_config.h"
#include <esp32common/espnow/common.h>
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/connection_event_processor.h>
#include <esp32common/espnow/tx_scheduler.h>
#include <channel_manager.h>
#include <espnow_peer_manager.h>
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

extern void notify_sse_data_updated();

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

    bool first_frame = true;
    bool led_is_on = false;
    uint32_t next_toggle_ms = 0;
    uint8_t heartbeat_phase = 0;  // 0=beat1_on, 1=interbeat_off, 2=beat2_on, 3=pause_off
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
            heartbeat_phase = 0;
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
                // Profile 1: beat1_on(120) -> interbeat_off(100) -> beat2_on(120) -> pause_off(760)
                if (now_ms >= next_toggle_ms) {
                    if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                        switch (heartbeat_phase) {
                            case 0: // beat 1 on
                                set_led(color);
                                led_is_on = true;
                                next_toggle_ms = now_ms + LedPatternTiming::kConfig.heartbeat.beat1_on_ms;
                                heartbeat_phase = 1;
                                break;
                            case 1: // interbeat off
                                clear_led();
                                led_is_on = false;
                                next_toggle_ms = now_ms + LedPatternTiming::kConfig.heartbeat.interbeat_off_ms;
                                heartbeat_phase = 2;
                                break;
                            case 2: // beat 2 on
                                set_led(color);
                                led_is_on = true;
                                next_toggle_ms = now_ms + LedPatternTiming::kConfig.heartbeat.beat2_on_ms;
                                heartbeat_phase = 3;
                                break;
                            case 3: // long pause off
                            default:
                                clear_led();
                                led_is_on = false;
                                next_toggle_ms = now_ms + LedPatternTiming::kConfig.heartbeat.pause_off_ms;
                                heartbeat_phase = 0;
                                break;
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
                        next_toggle_ms = now_ms + (led_is_on
                            ? LedPatternTiming::kConfig.flash.on_ms
                            : LedPatternTiming::kConfig.flash.off_ms);
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

static esp_err_t send_config_section_request(const uint8_t* mac,
                                             config_section_t section,
                                             uint32_t requested_version = 0) {
    config_section_request_t request{};
    request.type = msg_config_section_request;
    request.section = section;
    request.requested_version = requested_version;
    return EspnowTxScheduler::send(mac, &request, sizeof(request), "CONFIG_SECTION_REQ");
}

static bool send_receiver2_initialization_burst(void* /*context*/, const uint8_t* transmitter_mac) {
    request_data_t request{msg_request_data, subtype_power_profile};
    esp_err_t result = EspnowTxScheduler::send(transmitter_mac, &request, sizeof(request), "REQUEST_DATA");
    if (result == ESP_OK) {
        LOG_INFO("CONN_HANDLER", "[INIT] Sent power profile request");
    } else {
        LOG_WARN("CONN_HANDLER", "[INIT] Failed power profile request: %s", esp_err_to_name(result));
    }

    version_announce_t announce{};
    announce.type = msg_version_announce;
    announce.firmware_version = FW_VERSION_NUMBER;
    announce.protocol_version = PROTOCOL_VERSION;
    strncpy(announce.device_type, DEVICE_NAME, sizeof(announce.device_type) - 1);
    strncpy(announce.build_date, __DATE__, sizeof(announce.build_date) - 1);
    strncpy(announce.build_time, __TIME__, sizeof(announce.build_time) - 1);

    result = EspnowTxScheduler::send(transmitter_mac, &announce, sizeof(announce), "VERSION_ANNOUNCE");
    if (result == ESP_OK) {
        LOG_INFO("CONN_HANDLER", "[INIT] Sent version info: %d.%d.%d",
                 FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    } else {
        LOG_WARN("CONN_HANDLER", "[INIT] Failed version announce: %s", esp_err_to_name(result));
    }

    LOG_INFO("CONN_HANDLER", "[INIT] Paced init started - high-priority requests sent, config/LED/catalogs deferred to tick()");
    return true;
}

static void on_receiver2_connected(void* /*context*/) {
    RxStateMachine::instance().on_connection_established();
    RxHeartbeatManager::instance().on_connection_established();
}

static void on_receiver2_connection_lost(void* /*context*/) {
    RxStateMachine::instance().on_connection_lost();
}

static void on_receiver2_config_update_sent(void* /*context*/) {
    RxStateMachine::instance().on_config_update_sent();
}

static void disconnect_receiver2_mqtt(void* /*context*/) {
    MqttClient::disconnect();
}

static void on_receiver2_tx_reboot(void* /*context*/) {
    ReceiverConnectionHandler::instance().on_transmitter_reboot_detected();
}

static void on_receiver2_heartbeat_payload(void* /*context*/, const heartbeat_t* hb, const uint8_t* /*mac*/) {
    TransmitterManager::updateTimeData(hb->uptime_ms, hb->unix_time, hb->utc_offset_min, hb->time_source);
    TransmitterManager::updateHeartbeatFlags(hb->flags);
    notify_sse_data_updated();
}

static void on_receiver2_heartbeat_ack_enqueue_failure(void* /*context*/, esp_err_t err) {
    if (err == ESP_ERR_ESPNOW_NO_MEM) {
        ReceiverConnectionHandler::instance().on_ack_send_pressure("heartbeat ACK enqueue no-mem");
    }
}

static uint8_t receiver2_connection_state(void* /*context*/) {
    return static_cast<uint8_t>(RxStateMachine::instance().connection_state());
}

static uint32_t receiver2_link_activity_ms(void* /*context*/) {
    return ReceiverConnectionHandler::instance().get_last_rx_time_ms();
}

static bool receiver2_has_recent_power_data(void* /*context*/,
                                            const ReceiverConnectionHandler& /*handler*/,
                                            uint32_t now_ms,
                                            uint32_t freshness_ms) {
    const auto rx_state = RxStateMachine::instance().connection_state();
    const auto rx_stats = RxStateMachine::instance().stats();
    const bool recent_power_data =
        (rx_stats.last_message_ms > 0) &&
        ((now_ms - rx_stats.last_message_ms) <= freshness_ms);
    return (rx_state == EspNowDeviceState::ACTIVE) && recent_power_data;
}

static void run_receiver2_project_tick(void* /*context*/, ReceiverConnectionHandler& handler, uint32_t now) {
    auto& led_sync = handler.led_sync_policy();
    if (led_sync.is_pending()) {
        if (led_sync.tick(now) && send_led_state_request()) {
            led_sync.mark_sent(now);
        }
        if (!led_sync.is_pending() && led_sync.attempt_count() >= RxLedSyncPolicy::MAX_ATTEMPTS) {
            LOG_WARN("CONN_HANDLER", "[LED_SYNC] No LED response after %u attempt(s)",
                     static_cast<unsigned>(led_sync.attempt_count()));
        }
    }

    auto& catalog_retry = handler.catalog_retry_policy();
    if (!catalog_retry.is_due(now, handler.connected_at_ms())) {
        return;
    }

    catalog_retry.tick_item("catalog versions",
        !catalog_retry.versions_received(),
        &send_type_catalog_versions_request,
        catalog_retry.versions_retry_count, now);

    catalog_retry.tick_item("battery catalog",
        TypeCatalogCache::battery_refresh_required() || !TypeCatalogCache::has_battery_entries(),
        &send_battery_types_request,
        catalog_retry.battery_retry_count, now);

    catalog_retry.tick_item("inverter catalog",
        TypeCatalogCache::inverter_refresh_required() || !TypeCatalogCache::has_inverter_entries(),
        &send_inverter_types_request,
        catalog_retry.inverter_retry_count, now);

    catalog_retry.tick_item("inverter interfaces",
        !TypeCatalogCache::has_inverter_interface_entries(),
        &send_inverter_interfaces_request,
        catalog_retry.interface_retry_count, now);

    catalog_retry.mark_ticked(now);
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

    ReceiverConnectionHandlerConfig handler_config{};
    handler_config.request_retry_interval_ms = 2000;

    ReceiverConnectionHandlerHooks handler_hooks{};
    handler_hooks.on_connected = &on_receiver2_connected;
    handler_hooks.on_connection_lost = &on_receiver2_connection_lost;
    handler_hooks.disconnect_mqtt = &disconnect_receiver2_mqtt;
    handler_hooks.on_config_update_sent = &on_receiver2_config_update_sent;
    handler_hooks.send_initialization_burst = &send_receiver2_initialization_burst;
    handler_hooks.has_recent_power_data = &receiver2_has_recent_power_data;
    handler_hooks.run_project_specific_tick = &run_receiver2_project_tick;

    ReceiverConnectionHandler::instance().configure(handler_config);
    ReceiverConnectionHandler::instance().configure_hooks(handler_hooks);

    RxHeartbeatManagerConfig heartbeat_config{};
    heartbeat_config.use_link_activity_as_keepalive = true;

    RxHeartbeatManagerHooks heartbeat_hooks{};
    heartbeat_hooks.on_transmitter_reboot_detected = &on_receiver2_tx_reboot;
    heartbeat_hooks.on_heartbeat_payload = &on_receiver2_heartbeat_payload;
    heartbeat_hooks.on_heartbeat_ack_enqueue_failure = &on_receiver2_heartbeat_ack_enqueue_failure;
    heartbeat_hooks.get_connection_state = &receiver2_connection_state;
    heartbeat_hooks.get_link_activity_time_ms = &receiver2_link_activity_ms;

    RxHeartbeatManager::instance().configure(heartbeat_config);
    RxHeartbeatManager::instance().configure_hooks(heartbeat_hooks);
    ReceiverConnectionHandler::instance().init();

    RxHeartbeatManager::instance().init();
    LOG_INFO("HEARTBEAT", "RX Heartbeat manager initialized (90s timeout)");

    SystemStateManager::instance().init();

    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_espnow_sent);
    LOG_DEBUG("MAIN", "ESP-NOW callbacks registered");

    // Register broadcast peer so the receiver can receive TX's broadcast PROBE
    // frames during channel-hop scanning.  No data is ever sent by the receiver
    // as a broadcast — this is receive-side registration only.
    if (!EspnowPeerManager::add_broadcast_peer()) {
        LOG_WARN("MAIN", "Failed to register broadcast peer — TX probe reception may fail");
    } else {
        LOG_INFO("MAIN", "Broadcast peer registered (RX-side, receive-only)");
    }

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
