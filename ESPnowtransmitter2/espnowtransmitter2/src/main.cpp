/**
 * Transmitter - Modular Architecture (MQTT transport mode)
 *
 * Hardware: Olimex ESP32-POE-ISO (WROVER)
 * Features:
 *  - Ethernet connectivity (W5500)
 *  - MQTT telemetry publishing / command routing
 *  - HTTP OTA firmware updates
 *  - NTP time synchronization
 *
 * Architecture:
 *  - Singleton managers for all services
 *  - Runtime MQTT task + service-lifecycle supervisors
 *  - Clean configuration separation
 */

#include <Arduino.h>
#include <WiFi.h>           // Direct use: WiFi.mode/disconnect/config/macAddress
#include <ETH.h>
#include <firmware_version.h>  // DEVICE_NAME, PROTOCOL_VERSION, FW_VERSION_*
#include <firmware_metadata.h>
#include <runtime_common_utils/ota_boot_guard.h>
#include <runtime_common_utils/setup_health_gate.h>

// Configuration
#include "config/hardware_config.h"
#include "config/network_config.h"
#include "config/task_config.h"
#include "config/runtime_task_startup.h"
#include "config/logging_config.h"
#include <esp32common/config/timing_config.h>

// Network managers
#include "network/ethernet_manager.h"  // Provides ETH.h transitively

#include <runtime_common_utils/bootstrap_phase_runner.h>
#include "network/mqtt_manager.h"
#include "network/ota_manager.h"
#include "network/service_supervisor.h"
#include "network/mqtt_task.h"
#include "network/time_manager.h"
#include "config/mqtt_config_manager.h"

// Battery Emulator HAL (single fixed TransmitterHal for Olimex ESP32-POE2)
#include "battery_emulator/devboard/hal/hal.h"

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
#include "battery_emulator/devboard/utils/events.h"
#include "battery_emulator/devboard/safety/safety.h"
#include <esp_system.h>
#endif

// =============================================================================
// BOOTSTRAP PHASE FUNCTIONS
// =============================================================================
// setup() is decomposed into 8 ordered phases.  Each phase owns exactly one
// system layer and documents its dependency contracts inline.  Execution order
// must be preserved: later phases depend on resources established by earlier
// ones (see ordering contracts in each function).
// =============================================================================

static void log_timing_policy() {
    LOG_INFO("TIMING", "Startup: serial=%lu wifi_stabilize=%lu post_init=%lu component=%lu",
             static_cast<unsigned long>(TimingConfig::STARTUP.serial_init_delay_ms),
             static_cast<unsigned long>(TimingConfig::STARTUP.wifi_radio_stabilization_ms),
             static_cast<unsigned long>(TimingConfig::STARTUP.post_init_delay_ms),
             static_cast<unsigned long>(TimingConfig::STARTUP.component_init_delay_ms));
    LOG_INFO("TIMING", "Link: heartbeat_interval=%lu heartbeat_timeout=%lu tx_timeout=%lu",
             static_cast<unsigned long>(TimingConfig::HEARTBEAT.interval_ms),
             static_cast<unsigned long>(TimingConfig::HEARTBEAT.timeout_ms),
             static_cast<unsigned long>(TimingConfig::HEARTBEAT.tx_timeout_ms));
    LOG_INFO("TIMING", "Ethernet: init=%lu phy_reset=%lu ip_wait=%lu recovery=%lu",
             static_cast<unsigned long>(TimingConfig::ETHERNET.init_delay_ms),
             static_cast<unsigned long>(TimingConfig::ETHERNET.phy_reset_delay_ms),
             static_cast<unsigned long>(TimingConfig::ETHERNET.ip_acquiring_timeout_ms),
             static_cast<unsigned long>(TimingConfig::ETHERNET.recovery_timeout_ms));
    LOG_INFO("TIMING", "MQTT: reconnect=%lu publish=%lu loop=%lu max_retry=%lu",
             static_cast<unsigned long>(TimingConfig::MQTT.reconnect_interval_ms),
             static_cast<unsigned long>(TimingConfig::MQTT.publish_interval_ms),
             static_cast<unsigned long>(TimingConfig::MQTT.loop_delay_ms),
             static_cast<unsigned long>(TimingConfig::MQTT.max_retry_delay_ms));
    LOG_INFO("TIMING", "Loops: main=%lu eth_update=%lu state_validation=%lu metrics=%lu",
             static_cast<unsigned long>(TimingConfig::LOOPS.main_loop_delay_ms),
             static_cast<unsigned long>(TimingConfig::LOOPS.eth_state_machine_update_interval_ms),
             static_cast<unsigned long>(TimingConfig::LOOPS.state_validation_interval_ms),
             static_cast<unsigned long>(TimingConfig::LOOPS.metrics_report_interval_ms));
}

// --- Phase 1: Hardware -------------------------------------------------------
// Initialise physical hardware: serial port, HAL GPIO, firmware metadata.
// No subsystem dependencies.
static void bootstrap_hardware() {
    Serial.begin(hardware::SERIAL_BAUD_RATE);
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::STARTUP.serial_init_delay_ms));
    LOG_INFO("MAIN", "\n=== MQTT Transport Transmitter (Modular) ===");

    // Initialize hardware abstraction layer (fixed TransmitterHal for Olimex ESP32-POE2)
    init_hal();
    LOG_INFO("HAL", "Board: Olimex ESP32-POE2");

    // Display firmware metadata (embedded in binary)
    char fwInfo[128];
    FirmwareMetadata::getInfoString(fwInfo, sizeof(fwInfo), false);
    LOG_INFO("MAIN", "%s", fwInfo);

    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        LOG_INFO("MAIN", "Built: %s", FirmwareMetadata::metadata.build_date);
    }

    LOG_INFO("MAIN", "Device: %s", DEVICE_NAME);
    LOG_INFO("MAIN", "Protocol Version: %d", PROTOCOL_VERSION);
    log_timing_policy();

    OtaBootGuard::begin("TX_BOOT_GUARD");
}

// --- Phase 2: Persistence / Config ------------------------------------------
// Load NVS-backed system settings and keep WiFi STA radio in a neutral state.
// Ethernet remains the primary network path.
static void bootstrap_persistence() {
    LOG_INFO("SETTINGS", "Initializing system settings...");
    if (!SystemSettings::instance().init()) {
        LOG_ERROR("SETTINGS", "System settings initialization failed");
    }

    // Initialize WiFi radio before Ethernet.
    // STA mode with no IP/gateway — Ethernet remains the default route for all
    // network traffic (MQTT, NTP, OTA, HTTP).
    LOG_INFO("WIFI", "Initializing WiFi STA radio...");
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
    // Initialize events system (MUST be first - required before any set_event() calls)
    init_events();

    // Record reset reason as the first event so the event log is never empty
    {
        const esp_reset_reason_t rst = esp_reset_reason();
        switch (rst) {
            case ESP_RST_POWERON:   set_event(EVENT_RESET_POWERON,   static_cast<uint8_t>(rst)); break;
            case ESP_RST_EXT:       set_event(EVENT_RESET_EXT,       static_cast<uint8_t>(rst)); break;
            case ESP_RST_SW:        set_event(EVENT_RESET_SW,        static_cast<uint8_t>(rst)); break;
            case ESP_RST_PANIC:     set_event(EVENT_RESET_PANIC,     static_cast<uint8_t>(rst)); break;
            case ESP_RST_INT_WDT:   set_event(EVENT_RESET_INT_WDT,   static_cast<uint8_t>(rst)); break;
            case ESP_RST_TASK_WDT:  set_event(EVENT_RESET_TASK_WDT,  static_cast<uint8_t>(rst)); break;
            case ESP_RST_WDT:       set_event(EVENT_RESET_WDT,       static_cast<uint8_t>(rst)); break;
            case ESP_RST_DEEPSLEEP: set_event(EVENT_RESET_DEEPSLEEP, static_cast<uint8_t>(rst)); break;
            case ESP_RST_BROWNOUT:  set_event(EVENT_RESET_BROWNOUT,  static_cast<uint8_t>(rst)); break;
            default:                set_event(EVENT_RESET_UNKNOWN,   static_cast<uint8_t>(rst)); break;
        }
        LOG_INFO("EVENTS", "Events initialized, reset reason: %d", static_cast<int>(rst));
    }

    // Load battery type and other settings from NVS
    // Get battery type from SystemSettings (Phase 2 already initialized it)
    SystemSettings& settings_ref = SystemSettings::instance();
    uint8_t battery_profile = settings_ref.get_battery_profile_type();
    
    // Diagnostics BEFORE any init
    LOG_INFO("BATTERY", "┌─────────────────────────────────────────────────────┐");
    LOG_INFO("BATTERY", "│ Boot Battery Init - Source of Truth Verification      │");
    LOG_INFO("BATTERY", "├─────────────────────────────────────────────────────┤");
    LOG_INFO("BATTERY", "│ SystemSettings.battery_profile = %u (BatteryType)    │", battery_profile);
    LOG_INFO("BATTERY", "│ CAN_ENABLED = yes, watchdog will check 1x/sec        │");
    LOG_INFO("BATTERY", "└─────────────────────────────────────────────────────┘");
    
    // Load non-battery settings from legacy store (still needed for inverter/charger/limits)
    LOG_INFO("BATTERY", "Loading non-battery settings from legacy store...");
    init_stored_settings();
    
    // Load all persisted settings (battery, power, CAN, contactor) from NVS into SettingsManager
    // CRITICAL: Must happen before MQTT publishes static settings, so pub/sub sees current values
    LOG_INFO("SETTINGS", "Initializing SettingsManager from NVS blobs...");
    if (SettingsManager::instance().init()) {
        LOG_INFO("SETTINGS", "✓ SettingsManager loaded: battery v%u, power v%u, can v%u, contactor v%u",
                 SettingsManager::instance().get_battery_settings_version(),
                 SettingsManager::instance().get_power_settings_version(),
                 SettingsManager::instance().get_can_settings_version(),
                 SettingsManager::instance().get_contactor_settings_version());
    } else {
        LOG_WARN("SETTINGS", "SettingsManager initialization had issues (may be first boot with no saved settings)");
    }
    
    // OVERRIDE battery type with SystemSettings value (source of truth)
    user_selected_battery_type = static_cast<BatteryType>(battery_profile);
    LOG_INFO("BATTERY", "Battery type set from SystemSettings: %u", static_cast<uint32_t>(user_selected_battery_type));

    // Initialize CAN driver (uses HSPI — no GPIO conflicts with Ethernet)
    LOG_INFO("CAN", "Initializing CAN driver...");
    if (!CANDriver::instance().init()) {
        LOG_ERROR("CAN", "CAN initialization failed!");
    } else {
        LOG_INFO("CAN", "✓ CAN driver ready");
    }

    // Initialize battery after CAN (matches original Battery Emulator order)
    LOG_INFO("BATTERY", "Initializing battery (type: %d)...", static_cast<int>(user_selected_battery_type));
    if (BatteryManager::instance().init_primary_battery(user_selected_battery_type)) {
        LOG_INFO("BATTERY", "✓ Battery initialized: %u cells configured",
                 datalayer.battery.info.number_of_cells);
        LOG_INFO("BATTERY", "✓ CAN watchdog is ACTIVE (will fire EVENT_CAN_BATTERY_MISSING after 60s idle)");
    } else {
        LOG_WARN("BATTERY", "Battery initialization returned false (may be None type)");
        if (user_selected_battery_type == BatteryType::None) {
            LOG_WARN("BATTERY", "⚠ Battery type is NONE - CAN watchdog will NOT fire");
            LOG_WARN("BATTERY", "  Configure battery via receiver UI or set BATTTYPE in NVS");
        }
    }

    LOG_INFO("DATALAYER", "✓ Datalayer initialized");
#endif
}

// --- Phase 4: Connectivity primitives ---------------------------------------
// Bring up Ethernet with its service-lifecycle callbacks (H2), then
// initialize network connectivity for MQTT services.
// Service start/stop is deferred entirely to callback owners.
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

    LOG_INFO("NETWORK", "MQTT transport mode active (no ESP-NOW bootstrap)");
}

// --- Phase 5: Data layer preparation ----------------------------------------
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

// --- Phase 6: FreeRTOS task launch ------------------------------------------
// Start all long-running background tasks.
// MQTT task remains active for telemetry/command routing.
static void bootstrap_tasks() {
    RuntimeTaskStartup::start_runtime_tasks();
}

// --- Phase 7: Post-start network services -----------------------------------
// Start time/view services that are independent of direct Ethernet callback
// ownership (NTP utilities are now owned by ServiceSupervisor lifecycle).
static void bootstrap_network_services() {
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::POST_INIT_DELAY_MS));

    LOG_INFO("TIME", "Initializing TimeManager for time sync...");
    TimeManager::instance().init("pool.ntp.org");
    LOG_INFO("TIME", "TimeManager initialized");
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
        {"data_layer", bootstrap_data_layer},
        {"tasks", bootstrap_tasks},
        {"network_services", bootstrap_network_services},
    };

    BootstrapPhaseRunner::run_phases(
        kBootstrapPhases,
        sizeof(kBootstrapPhases) / sizeof(kBootstrapPhases[0])
    );

    {
        const SetupHealthGate::Check checks[] = {
            {"heap_ok", ESP.getFreeHeap() > 32768},
            {"ethernet_not_fatal", EthernetManager::instance().get_state() != EthernetConnectionState::ERROR_STATE},
        };

        const SetupHealthGate::Outcome outcome = SetupHealthGate::apply(
            "TX_BOOT_GUARD",
            checks,
            sizeof(checks) / sizeof(checks[0]),
            "transmitter setup health gate failed",
            "transmitter setup health gate passed");

        if (outcome == SetupHealthGate::Outcome::Error) {
            LOG_ERROR("BOOT_GUARD", "Transmitter setup health gate helper returned error");
        }
    }

    LOG_INFO("MAIN", "Setup complete! All 7 bootstrap phases done.");
    LOG_INFO("MAIN", "=================================");
}

void loop() {
#if CONFIG_CAN_ENABLED
    // Phase 4a: Process CAN messages (high priority)
    CANDriver::instance().update();
    
    // Phase 4a: Update periodic BMS transmitters (battery data publishing)
    BatteryManager::instance().update_transmitters(millis());

    // Safety watchdog: CAN alive countdown, CPU temperature, voltage/cell checks.
    // Must be called at ~1 Hz — CAN_battery_still_alive counts down from 60 and
    // sets EVENT_CAN_BATTERY_MISSING when it reaches 0 (no CAN frames for 60 s).
    static uint32_t last_safety_check_ms = 0;
    {
        uint32_t now_ms = millis();
        if (now_ms - last_safety_check_ms >= 1000) {
            update_machineryprotection();
            last_safety_check_ms = now_ms;
        }
    }
#endif
    
    // ✅ NEW: Update Ethernet state machine (check timeouts, recovery transitions)
    static uint32_t last_eth_update = 0;
    uint32_t now = millis();
    if (now - last_eth_update > TimingConfig::ETH_STATE_MACHINE_UPDATE_INTERVAL_MS) {
        EthernetManager::instance().update_state_machine();
        last_eth_update = now;
    }
    
    // All work is done in FreeRTOS tasks
    // Main loop handles periodic health checks and monitoring
    
    static uint32_t last_state_validation = 0;
    static uint32_t last_metrics_report = 0;
#if CONFIG_CAN_ENABLED
    static uint32_t last_can_stats = 0;
    
    // Phase 4a: Periodic CAN statistics (every 10 seconds)
    if (now - last_can_stats > TimingConfig::CAN_STATS_LOG_INTERVAL_MS) {
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
    
    // Periodic runtime marker retained for future MQTT-side validation hooks.
    if (now - last_state_validation > TimingConfig::STATE_VALIDATION_INTERVAL_MS) {
        last_state_validation = now;
    }

    if (now - last_metrics_report > TimingConfig::METRICS_REPORT_INTERVAL_MS) {
        last_metrics_report = now;
    }
    
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::MAIN_LOOP_DELAY_MS));
}
