/**
 * ESP-NOW Transmitter - Modular Architecture
 * 
 * Hardware: Olimex ESP32-POE-ISO (WROVER)
 * Features:
 *  - ESP-NOW transmitter (periodic data + discovery)
 *  - Ethernet connectivity (W5500)
 *  - MQTT telemetry publishing
 *  - HTTP OTA firmware updates
 *  - NTP time synchronization
 * 
 * Architecture:
 *  - Singleton managers for all services
 *  - 4 FreeRTOS tasks: RX, data sender, discovery, MQTT
 *  - Clean configuration separation
 */

#include <Arduino.h>
#include <WiFi.h>           // Direct use: WiFi.mode/disconnect/config/macAddress
#include <ETH.h>
#include <espnow_transmitter.h>
#include <espnow_send_utils.h>
#include <firmware_version.h>  // DEVICE_NAME, PROTOCOL_VERSION, FW_VERSION_*
#include <firmware_metadata.h>
#include <runtime_common_utils/ota_boot_guard.h>

// Configuration
#include "config/hardware_config.h"
#include "config/network_config.h"
#include "config/task_config.h"
#include "config/logging_config.h"
#include <esp32common/config/timing_config.h>

// Network managers
#include "network/ethernet_manager.h"  // Provides ETH.h transitively

// Queue management
#include "queue/espnow_queue_manager.h"
#include <runtime_common_utils/bootstrap_phase_runner.h>
#include "runtime/runtime_context.h"
#include "network/mqtt_manager.h"
#include "network/ota_manager.h"
#include "network/service_supervisor.h"
#include "network/mqtt_task.h"
#include "network/time_manager.h"
#include <mqtt_manager.h>  // For MqttConfigManager

// Battery Emulator HAL (for GPIO configuration)
#include "battery_emulator/devboard/hal/hal.h"

// ESP-NOW handlers
#include "espnow/message_handler.h"
#include "espnow/discovery_task.h"
#include "espnow/data_sender.h"
#include "espnow/version_beacon_manager.h"
#include "espnow/enhanced_cache.h"          // Section 11: Dual storage cache
#include "espnow/transmission_task.h"        // Section 11: Background transmission
#include "espnow/heartbeat_manager.h"        // Heartbeat with sequence tracking and ACK
#include "espnow/tx_connection_handler.h"
#include "espnow/tx_state_machine.h"
#include <channel_manager.h>                 // Centralized channel management
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/connection_event_processor.h>

// Settings manager
#include "settings/settings_manager.h"
#include "system_settings.h"

// Data layer
#include "datalayer/static_data.h"

// Test data configuration (Phase 2)
#include "test_data/test_data_config.h"

// Phase 4a: Battery Emulator integration
#if CONFIG_CAN_ENABLED
#include "battery_emulator/datalayer/datalayer.h"
#include "battery_emulator/communication/nvm/comm_nvm.h"
#include "battery_emulator/test_data_generator.h"
#include "communication/can/can_driver.h"
#include "battery/battery_manager.h"
#endif

// =============================================================================
// BOOTSTRAP PHASE FUNCTIONS
// =============================================================================
// setup() is decomposed into 8 ordered phases.  Each phase owns exactly one
// system layer and documents its dependency contracts inline.  Execution order
// must be preserved: later phases depend on resources established by earlier
// ones (see ordering contracts in each function).
// =============================================================================

// --- Phase 1: Hardware -------------------------------------------------------
// Initialise physical hardware: serial port, HAL GPIO, firmware metadata.
// No subsystem dependencies.
static void bootstrap_hardware() {
    Serial.begin(hardware::SERIAL_BAUD_RATE);
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::SERIAL_INIT_DELAY_MS));
    LOG_INFO("MAIN", "\n=== ESP-NOW Transmitter (Modular) ===");

    // Initialize hardware abstraction layer (GPIO configuration for Waveshare HAT)
    init_hal();
    LOG_INFO("HAL", "Hardware abstraction layer initialized: %s", esp32hal->name());

    // Display firmware metadata (embedded in binary)
    char fwInfo[128];
    FirmwareMetadata::getInfoString(fwInfo, sizeof(fwInfo), false);
    LOG_INFO("MAIN", "%s", fwInfo);

    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        LOG_INFO("MAIN", "Built: %s", FirmwareMetadata::metadata.build_date);
    }

    LOG_INFO("MAIN", "Device: %s", DEVICE_NAME);
    LOG_INFO("MAIN", "Protocol Version: %d", PROTOCOL_VERSION);

    OtaBootGuard::begin("TX_BOOT_GUARD");
}

// --- Phase 2: Persistence / Config ------------------------------------------
// Load NVS-backed system settings; configure WiFi radio for ESP-NOW use.
// WiFi MUST be configured BEFORE Ethernet (radio stabilisation requirement).
static void bootstrap_persistence() {
    LOG_INFO("SETTINGS", "Initializing system settings...");
    if (!SystemSettings::instance().init()) {
        LOG_ERROR("SETTINGS", "System settings initialization failed");
    }

    // Initialize WiFi for ESP-NOW (BEFORE Ethernet to avoid disruption).
    // STA mode with no IP/gateway — Ethernet is the default route for all
    // network traffic (MQTT, NTP, OTA, HTTP).
    LOG_INFO("WIFI", "Initializing WiFi for ESP-NOW...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    // CRITICAL: Explicitly clear WiFi IP to force routing via Ethernet
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::WIFI_RADIO_STABILIZATION_MS));

    uint8_t mac[6];
    WiFi.macAddress(mac);
    LOG_DEBUG("WIFI", "WiFi MAC: %02X:%02X:%02X:%02X:%02X:%02X",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// --- Phase 3: Battery subsystem ---------------------------------------------
// ORDERING CONTRACT (must be preserved exactly — any reordering breaks the
// cell-count dependency chain):
//   1. init_stored_settings()               — load battery type from NVS
//   2. CANDriver::init()                    — start hardware CAN peripheral
//   3. BatteryManager::init_primary_battery() — calls battery->setup(),
//      sets global battery* pointer and populates
//      datalayer.battery.info.number_of_cells
//
// bootstrap_data_layer() (Phase 6) depends on datalayer.battery.info being
// populated here before StaticData::update_battery_specs() and
// TestDataGenerator::update() are called.
static void bootstrap_battery() {
#if CONFIG_CAN_ENABLED
    // Load battery type and other settings from NVS
    LOG_INFO("BATTERY", "Loading battery configuration from NVS...");
    init_stored_settings();

    // Initialize CAN driver (uses HSPI — no GPIO conflicts with Ethernet)
    LOG_INFO("CAN", "Initializing CAN driver...");
    if (!CANDriver::instance().init()) {
        LOG_ERROR("CAN", "CAN initialization failed!");
    } else {
        LOG_INFO("CAN", "✓ CAN driver ready");
    }

    // Initialize battery after CAN (matches original Battery Emulator order)
    LOG_INFO("BATTERY", "Initializing battery (type: %d)...", (int)user_selected_battery_type);
    if (BatteryManager::instance().init_primary_battery(user_selected_battery_type)) {
        LOG_INFO("BATTERY", "✓ Battery initialized: %u cells configured",
                 datalayer.battery.info.number_of_cells);
    } else {
        LOG_WARN("BATTERY", "Battery initialization returned false (may be None type)");
    }

    LOG_INFO("DATALAYER", "✓ Datalayer initialized");
#endif
}

// --- Phase 4: Connectivity primitives ---------------------------------------
// Bring up Ethernet with its service-lifecycle callbacks (H2), then
// initialise the ESP-NOW radio and queue layer.
// Service start/stop is deferred entirely to the callback bodies — no inline
// is_connected() gate here (see H2 implementation notes).
static void bootstrap_connectivity() {
    // Initialize Ethernet (AFTER WiFi radio is stable — see Phase 2)
    LOG_INFO("ETHERNET", "Initializing Ethernet...");
    if (!EthernetManager::instance().init()) {
        LOG_ERROR("ETHERNET", "Ethernet initialization failed!");
    }

    // Phase D follow-up: one owner for Ethernet-dependent service lifecycle.
    // The supervisor registers the Ethernet callbacks once and replays current
    // state immediately if the link is already up.
    ServiceSupervisor::instance().attach_to_ethernet();

    // Initialize ESP-NOW queue layer (must precede init_espnow)
    LOG_INFO("ESPNOW", "Initializing ESP-NOW...");
    if (!EspnowQueueManager::instance().init(
            task_config::ESPNOW_MESSAGE_QUEUE_SIZE,  // Message queue: 10
            20,                                       // Discovery queue: 20
            30                                        // RX queue: 30
        )) {
        LOG_ERROR("ESPNOW", "Failed to initialize queue manager!");
        return;
    }

    // Bind queue handles into runtime context (also synchronizes ISR-required globals).
    RuntimeContext::instance().bind_espnow_queues(
        EspnowQueueManager::instance().get_message_queue(),
        EspnowQueueManager::instance().get_discovery_queue(),
        EspnowQueueManager::instance().get_rx_queue()
    );

    init_espnow(RuntimeContext::instance().espnow_message_queue());
    LOG_DEBUG("ESPNOW", "ESP-NOW initialized successfully");
}

// --- Phase 5: ESP-NOW state machines & discovery ----------------------------
// Wire up every ESP-NOW state-machine layer in strict dependency order:
//   RX task → ChannelManager → ConnectionManager →
//   TransmitterConnectionHandler → TxStateMachine → MessageHandler →
//   ConnectionEventProcessor → SettingsManager → MqttConfigManager
// Finishes with NVS cache restore and active channel-hopping start.
static void bootstrap_espnow() {
    // MUST start BEFORE passive scanning — processes PROBE messages from receiver
    EspnowMessageHandler::instance().start_rx_task(RuntimeContext::instance().espnow_message_queue());
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::COMPONENT_INIT_DELAY_MS));

    LOG_INFO("CHANNEL", "Initializing channel manager...");
    if (!ChannelManager::instance().init()) {
        LOG_ERROR("CHANNEL", "Failed to initialize channel manager!");
    }

    // Must be after FreeRTOS scheduler has started (first task above ensures this)
    LOG_INFO("STATE", "Initializing common connection manager...");
    if (!EspNowConnectionManager::instance().init()) {
        LOG_ERROR("STATE", "Failed to initialize common connection manager!");
    }
    EspNowConnectionManager::instance().set_auto_reconnect(true);
    EspNowConnectionManager::instance().set_connecting_timeout_ms(timing::ESPNOW_CONNECTING_TIMEOUT_MS);

    // Initialize transmitter connection handler (registers state callbacks)
    TransmitterConnectionHandler::instance().init();
    // Initialize transmitter runtime state machine
    TxStateMachine::instance().init();
    // Message routes are registered in EspnowMessageHandler singleton construction;
    // connection/device state transitions are owned solely by TransmitterConnectionHandler.

    create_connection_event_processor(3, 0);

    // Initialize settings manager (loads from NVS or uses defaults)
    LOG_INFO("SETTINGS", "Initializing settings manager...");
    if (!SettingsManager::instance().init()) {
        LOG_ERROR("SETTINGS", "Failed to initialize settings manager");
    }

    // Initialize MQTT config manager (populates version beacon config data)
    LOG_INFO("MQTT", "Initializing MQTT config manager...");
    if (!MqttConfigManager::loadConfig()) {
        // No config in NVS — seed from hardcoded defaults in network_config.h
        LOG_INFO("MQTT", "No MQTT config in NVS, using hardcoded defaults");
        IPAddress mqtt_server;
        mqtt_server.fromString(config::get_mqtt_config().server);
        MqttConfigManager::saveConfig(
            config::features::MQTT_ENABLED,
            mqtt_server,
            config::get_mqtt_config().port,
            config::get_mqtt_config().username,
            config::get_mqtt_config().password,
            config::get_mqtt_config().client_id
        );
    }

    // ═══════════════════════════════════════════════════════════════════════
    // SECTION 11: TRANSMITTER-ACTIVE ARCHITECTURE
    // ═══════════════════════════════════════════════════════════════════════
    // Transmitter actively broadcasts PROBE channel-by-channel (1s/channel,
    // 13s max — 6× faster than the Section 10 receiver-master approach).
    // Enhanced cache with dual storage (transient + state); background
    // transmission task (non-blocking, Priority 2, Core 1).  Works regardless
    // of boot order; auto-recovers from router channel changes.
    // ═══════════════════════════════════════════════════════════════════════
    LOG_INFO("DISCOVERY", "╔═══════════════════════════════════════════════════════════════╗");
    LOG_INFO("DISCOVERY", "║  SECTION 11: Transmitter-Active Channel Hopping              ║");
    LOG_INFO("DISCOVERY", "╚═══════════════════════════════════════════════════════════════╝");

    // Restore state configurations from NVS (TX-only persistence)
    LOG_INFO("CACHE", "Restoring state from NVS (TX-only persistence)...");
    EnhancedCache::instance().restore_all_from_nvs();

    LOG_INFO("DISCOVERY", "Starting active channel hopping (1s/channel, 13s max)");
    LOG_INFO("DISCOVERY", "This is NON-BLOCKING - Ethernet and MQTT work independently");
    LOG_INFO("DISCOVERY", "Battery data cached until ESP-NOW connection established");

    // Start active hopping in background (non-blocking):
    // scans channels 1-13, broadcasts PROBE 1s per channel.
    // When receiver ACKs: locks channel, flushes cache, continues normally.
    TransmitterConnectionHandler::instance().start_discovery();

    LOG_INFO("DISCOVERY", "Active hopping started - continuing with network initialization...");
    LOG_INFO("DISCOVERY", "(ESP-NOW connection will be established asynchronously)");
}

// --- Phase 6: Data layer preparation ----------------------------------------
// Populate StaticData battery/inverter specs from system settings, then
// synchronise datalayer cell count for test data consumers.
//
// ORDERING CONTRACT: Must run AFTER bootstrap_battery() (Phase 3).
//   StaticData::update_battery_specs() reads datalayer.battery.info which is
//   populated by BatteryManager::init_primary_battery().
//   TestDataGenerator::update() also requires the correct cell count.
//   Pre-population of datalayer.battery.info.number_of_cells here acts as a
//   safe fallback until the real BMS value arrives via CAN in loop().
static void bootstrap_data_layer() {
    LOG_DEBUG("STATIC_DATA", "Initializing battery configuration...");
    StaticData::init();
    StaticData::update_battery_specs(SystemSettings::instance().get_battery_profile_type());
    StaticData::update_inverter_specs(SystemSettings::instance().get_inverter_type());

    // CRITICAL: Pre-populate datalayer cell count from battery profile.
    // BatteryManager::init_primary_battery() → battery->setup() sets this,
    // but may still be 0 if battery hasn't run.  Use battery_specs as the
    // authoritative source until the real BMS value arrives via CAN.
    datalayer.battery.info.number_of_cells = StaticData::get_battery_specs().number_of_cells;
    LOG_INFO("TEST_DATA", "Pre-initialized datalayer with %u cells from battery profile",
             datalayer.battery.info.number_of_cells);

    // Initialize test data configuration system (NVS-backed, runtime control)
    LOG_INFO("TEST_DATA_CONFIG", "Initializing test data configuration system...");
    TestDataConfig::init();
    LOG_INFO("TEST_DATA_CONFIG", "✓ Test data configuration initialized");

#if CONFIG_CAN_ENABLED
    RuntimeContext::instance().set_tx_soc(datalayer.battery.status.reported_soc / 100);  // Convert pptt to percentage
#else
    RuntimeContext::instance().set_tx_soc(50);  // Start at 50% for test mode
#endif
    randomSeed(esp_random());

#if CONFIG_CAN_ENABLED
    // CRITICAL: Always initialize with battery's cell count before MQTT starts.
    // Fixes the 108-cell fallback bug when battery has 96 cells (Nissan Leaf etc.).
    // Must happen AFTER battery setup (Phase 3) but BEFORE MQTT starts publishing.
    LOG_INFO("TEST_DATA", "Initializing test data generator with battery configuration...");
    TestDataGenerator::update();  // First call triggers init() with correct cell count
    LOG_INFO("TEST_DATA", "✓ Test data generator initialized with %u cells",
             datalayer.battery.info.number_of_cells);

    // Apply test data configuration loaded from NVS
    LOG_INFO("TEST_DATA_CONFIG", "Applying saved test data configuration...");
    TestDataConfig::apply_config();
    LOG_INFO("TEST_DATA_CONFIG", "✓ Configuration applied, mode: %s",
             TestDataConfig::mode_to_string(TestDataConfig::get_config().mode));
#endif
}

// --- Phase 7: FreeRTOS task launch ------------------------------------------
// Start all long-running background tasks.
// Order: TransmissionTask first (lowest layer), then DataSender (feeds it),
// then DiscoveryTask and MQTT task.  Once this phase returns all data-flow
// pipelines are active.
static void bootstrap_tasks() {
    LOG_DEBUG("ESPNOW", "Starting ESP-NOW tasks...");
    // (RX task was already started in bootstrap_espnow — it must precede discovery)

    // Background transmission task (Priority 2 — LOW, Core 1)
    // Reads from EnhancedCache and transmits via ESP-NOW (non-blocking)
    TransmissionTask::instance().start(task_config::PRIORITY_LOW, 1);
    LOG_INFO("ESPNOW", "Background transmission task started (Priority 2, Core 1)");

    HeartbeatManager::instance().init();
    LOG_INFO("HEARTBEAT", "Heartbeat manager initialized (10s interval, ACK-based)");

#if CONFIG_CAN_ENABLED
    LOG_INFO("MAIN", "===== PHASE 4a: REAL BATTERY DATA =====");
    LOG_INFO("MAIN", "Using CAN bus data from datalayer");
    DataSender::instance().start();
    LOG_INFO("MAIN", "✓ Data sender started (real battery data)");
#else
    LOG_INFO("MAIN", "Using simulated test data (CAN disabled)");
    DataSender::instance().start();
#endif

    // Start discovery task (periodic announcements until receiver connects)
    DiscoveryTask::instance().start();

    // Start MQTT task (lowest priority — background telemetry)
    if (config::features::MQTT_ENABLED) {
        xTaskCreate(
            task_mqtt_loop,
            "mqtt_task",
            task_config::STACK_SIZE_MQTT,
            nullptr,
            task_config::PRIORITY_LOW,
            nullptr
        );
    }
}

// --- Phase 8: Post-start network services -----------------------------------
// Start time/view services that are independent of direct Ethernet callback
// ownership (NTP utilities are now owned by ServiceSupervisor lifecycle).
static void bootstrap_network_services() {
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::POST_INIT_DELAY_MS));

    LOG_INFO("TIME", "Initializing TimeManager for time sync...");
    TimeManager::instance().init("pool.ntp.org");
    LOG_INFO("TIME", "TimeManager initialized");

    VersionBeaconManager::instance().init();
    LOG_INFO("VERSION", "Version beacon manager initialized (30s heartbeat)");
}

// =============================================================================
// ENTRY POINT
// =============================================================================

void setup() {
    static const BootstrapPhaseRunner::Phase kBootstrapPhases[] = {
        {"hardware", bootstrap_hardware},
        {"persistence", bootstrap_persistence},
        {"battery", bootstrap_battery},
        {"connectivity", bootstrap_connectivity},
        {"espnow", bootstrap_espnow},
        {"data_layer", bootstrap_data_layer},
        {"tasks", bootstrap_tasks},
        {"network_services", bootstrap_network_services},
    };

    BootstrapPhaseRunner::run_phases(
        kBootstrapPhases,
        sizeof(kBootstrapPhases) / sizeof(kBootstrapPhases[0])
    );

    if (OtaBootGuard::is_pending_verification()) {
        const bool heap_ok = ESP.getFreeHeap() > 32768;
        const bool ethernet_not_fatal = EthernetManager::instance().get_state() != EthernetConnectionState::ERROR_STATE;
        const bool espnow_queue_ready = RuntimeContext::instance().espnow_message_queue() != nullptr;

        if (heap_ok && ethernet_not_fatal && espnow_queue_ready) {
            OtaBootGuard::confirm_running_app("transmitter setup health gate passed");
            LOG_INFO("BOOT_GUARD", "Transmitter app confirmed valid after setup health gate");
        } else {
            LOG_ERROR("BOOT_GUARD", "Setup health gate failed (heap_ok=%d, ethernet_not_fatal=%d, espnow_queue_ready=%d); triggering rollback",
                      heap_ok ? 1 : 0,
                      ethernet_not_fatal ? 1 : 0,
                      espnow_queue_ready ? 1 : 0);
            OtaBootGuard::trigger_rollback_and_reboot("transmitter setup health gate failed");
        }
    }

    LOG_INFO("MAIN", "Setup complete! All 8 bootstrap phases done.");
    LOG_INFO("MAIN", "=================================");
}

void loop() {
#if CONFIG_CAN_ENABLED
    // Phase 4a: Process CAN messages (high priority)
    CANDriver::instance().update();
    
    // Phase 4a: Update periodic BMS transmitters (battery data publishing)
    BatteryManager::instance().update_transmitters(millis());
#endif
    
    // ✅ NEW: Update Ethernet state machine (check timeouts, recovery transitions)
    static uint32_t last_eth_update = 0;
    uint32_t now = millis();
    if (now - last_eth_update > timing::ETH_STATE_MACHINE_UPDATE_INTERVAL_MS) {
        EthernetManager::instance().update_state_machine();
        last_eth_update = now;
    }
    
    // All work is done in FreeRTOS tasks
    // Main loop handles periodic health checks and monitoring
    
    static uint32_t last_state_validation = 0;
    static uint32_t last_metrics_report = 0;
    static uint32_t last_peer_audit = 0;
#if CONFIG_CAN_ENABLED
    static uint32_t last_can_stats = 0;
    
    // Phase 4a: Periodic CAN statistics (every 10 seconds)
    if (now - last_can_stats > timing::CAN_STATS_LOG_INTERVAL_MS) {
        if (CANDriver::instance().is_ready()) {
            LOG_INFO("CAN", "Stats: RX=%u, TX=%u, Errors=%u, BMS=%s",
                     CANDriver::instance().get_rx_count(),
                     CANDriver::instance().get_tx_count(),
                     CANDriver::instance().get_error_count(),
                     datalayer.battery.status.real_bms_status == BatteryEmulator_real_bms_status_enum::BMS_ACTIVE ? "connected" : "disconnected");
        }
        last_can_stats = now;
    }
#endif
    
    // Periodic state validation (every 30 seconds) - Phase 2
    if (now - last_state_validation > timing::STATE_VALIDATION_INTERVAL_MS) {
        if (!DiscoveryTask::instance().validate_state()) {
            LOG_WARN("MAIN", "State validation failed - triggering self-healing restart");
            DiscoveryTask::instance().restart();
        }
        last_state_validation = now;
    }
    
    // Recovery state machine update - Phase 2
    DiscoveryTask::instance().update_recovery();

    // Progress deferred/backoff-aware discovery starts while CONNECTING
    TransmitterConnectionHandler::instance().tick();
    
    // Handle deferred logging from timer callbacks
    EspnowSendUtils::handle_deferred_logging();
    
    // Version beacon periodic update (every 30s heartbeat) - Phase 4
    VersionBeaconManager::instance().update();
    
    // Heartbeat periodic update (every 10s) - Section 11
    HeartbeatManager::instance().tick();
    
    // Metrics reporting (every 5 minutes) - Phase 3
    if (now - last_metrics_report > timing::METRICS_REPORT_INTERVAL_MS) {
        DiscoveryTask::instance().get_metrics().log_summary();
        last_metrics_report = now;
    }
    
    // Peer state audit (every 2 minutes, if debug enabled) - Phase 2
    #if LOG_LEVEL >= LOG_LEVEL_DEBUG
    if (now - last_peer_audit > timing::PEER_AUDIT_INTERVAL_MS) {
        DiscoveryTask::instance().audit_peer_state();
        last_peer_audit = now;
    }
    #endif
    
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::MAIN_LOOP_DELAY_MS));
}
