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
#include <WiFi.h>
#include <esp_wifi.h>
#include <ETH.h>
#include <espnow_transmitter.h>
#include <espnow_send_utils.h>
#include <ethernet_utilities.h>
#include <firmware_version.h>
#include <firmware_metadata.h>

// Configuration
#include "config/hardware_config.h"
#include "config/network_config.h"
#include "config/task_config.h"
#include "config/logging_config.h"

// Network managers
#include "network/ethernet_manager.h"
#include "network/mqtt_manager.h"
#include "network/ota_manager.h"
#include "network/mqtt_task.h"
#include "network/time_manager.h"
#include <mqtt_manager.h>  // For MqttConfigManager

// ESP-NOW handlers
#include "espnow/message_handler.h"
#include "espnow/discovery_task.h"
#include "espnow/data_sender.h"
#include "espnow/version_beacon_manager.h"
#include "espnow/enhanced_cache.h"          // Section 11: Dual storage cache
#include "espnow/transmission_task.h"        // Section 11: Background transmission
#include "espnow/heartbeat_manager.h"        // Heartbeat with sequence tracking and ACK
#include "espnow/tx_connection_handler.h"
#include <channel_manager.h>                 // Centralized channel management
#include "espnow/heartbeat_manager.h"         // Section 11: Heartbeat protocol
#include <connection_manager.h>
#include <connection_event_processor.h>

// Settings manager
#include "settings/settings_manager.h"

// Phase 1: Dummy data generator (REMOVED IN PHASE 2 - clean codebase)
// #include "testing/dummy_data_generator.h"

// MQTT Logger
#include <mqtt_logger.h>

// Global queue for ESP-NOW messages
QueueHandle_t espnow_message_queue = nullptr;

// Discovery queue for PROBE/ACK messages during active hopping
// Separate from main queue to prevent RX task from consuming discovery messages
QueueHandle_t espnow_discovery_queue = nullptr;

// Required by espnow_transmitter library
QueueHandle_t espnow_rx_queue = nullptr;
void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(1000);
    LOG_INFO("MAIN", "\n=== ESP-NOW Transmitter (Modular) ===");
    
    // Display firmware metadata (embedded in binary)
    char fwInfo[128];
    FirmwareMetadata::getInfoString(fwInfo, sizeof(fwInfo), false);
    LOG_INFO("MAIN", "%s", fwInfo);
    
    // Display build date if metadata is valid
    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        LOG_INFO("MAIN", "Built: %s", FirmwareMetadata::metadata.build_date);
    }
    
    LOG_INFO("MAIN", "Device: %s", DEVICE_NAME);
    LOG_INFO("MAIN", "Protocol Version: %d", PROTOCOL_VERSION);
    
    // Initialize Ethernet
    LOG_INFO("ETHERNET", "Initializing Ethernet...");
    if (!EthernetManager::instance().init()) {
        LOG_ERROR("ETHERNET", "Ethernet initialization failed!");
    }
    
    // Initialize WiFi for ESP-NOW
    LOG_INFO("WIFI", "Initializing WiFi for ESP-NOW...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    // SECTION 11 ARCHITECTURE: Transmitter-active channel hopping
    // No need to force channel - active hopping will discover receiver's channel
    // 1s per channel (13s max) vs 6s per channel (78s max) in Section 10
    
    uint8_t mac[6];
    WiFi.macAddress(mac);
    LOG_DEBUG("WIFI", "WiFi MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // Initialize ESP-NOW library
    LOG_INFO("ESPNOW", "Initializing ESP-NOW...");
    
    // Create main application queue (for RX task)
    espnow_message_queue = xQueueCreate(
        task_config::ESPNOW_MESSAGE_QUEUE_SIZE, 
        sizeof(espnow_queue_msg_t)
    );
    if (espnow_message_queue == nullptr) {
        LOG_ERROR("ESPNOW", "Failed to create ESP-NOW message queue!");
        return;
    }
    
    // Create separate discovery queue (for active hopping PROBE/ACK)
    // Prevents RX task from consuming discovery messages
    espnow_discovery_queue = xQueueCreate(
        20,  // Smaller queue - only for discovery messages
        sizeof(espnow_queue_msg_t)
    );
    if (espnow_discovery_queue == nullptr) {
        LOG_ERROR("ESPNOW", "Failed to create ESP-NOW discovery queue!");
        return;
    }
    LOG_DEBUG("ESPNOW", "Created separate discovery queue for active hopping");
    
    // Initialize ESP-NOW (uses library function)
    init_espnow(espnow_message_queue);
    LOG_DEBUG("ESPNOW", "ESP-NOW initialized successfully");
    
    // Start message handler (highest priority - processes incoming messages)
    // MUST start BEFORE passive scanning so it can process PROBE messages from receiver!
    EspnowMessageHandler::instance().start_rx_task(espnow_message_queue);
    delay(100);  // Let RX task initialize
    
    // PHASE B: Initialize channel manager (BEFORE connection manager)
    LOG_INFO("CHANNEL", "Initializing channel manager...");
    if (!ChannelManager::instance().init()) {
        LOG_ERROR("CHANNEL", "Failed to initialize channel manager!");
    }
    
    // PHASE B: Initialize common connection manager (AFTER first task starts)
    // Must be after FreeRTOS scheduler has started
    LOG_INFO("STATE", "Initializing common connection manager...");
    if (!EspNowConnectionManager::instance().init()) {
        LOG_ERROR("STATE", "Failed to initialize common connection manager!");
    }
    
    // Enable auto-reconnect and set timeout
    EspNowConnectionManager::instance().set_auto_reconnect(true);
    EspNowConnectionManager::instance().set_connecting_timeout_ms(30000);  // 30s timeout
    
    // Initialize transmitter connection handler (registers state callbacks)
    TransmitterConnectionHandler::instance().init();
    
    create_connection_event_processor(3, 0);
    
    // Initialize settings manager (loads from NVS or uses defaults)
    LOG_INFO("SETTINGS", "Initializing settings manager...");
    if (!SettingsManager::instance().init()) {
        LOG_ERROR("SETTINGS", "Failed to initialize settings manager");
    }
    
    // Initialize MQTT config manager with hardcoded config from network_config.h
    // This populates MqttConfigManager so version beacons can send correct config
    LOG_INFO("MQTT", "Initializing MQTT config manager...");
    if (!MqttConfigManager::loadConfig()) {
        // No config in NVS, use hardcoded defaults from network_config.h
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
    // OLD ARCHITECTURE (Section 10 - receiver-master, passive scanning):
    //   - Transmitter passively scans channels listening for receiver PROBE
    //   - 6s per channel, 78s max discovery time
    //   - Battery Emulator not yet migrated, blocking concerns
    //
    // NEW ARCHITECTURE (Section 11 - transmitter-active, hopping):
    //   - Transmitter actively broadcasts PROBE channel-by-channel
    //   - 1s per channel, 13s max discovery time (6x faster)
    //   - Enhanced cache with dual storage (transient + state)
    //   - Background transmission task (non-blocking, Priority 2, Core 1)
    //   - Keep-alive manager (10s heartbeat, 90s timeout)
    //   - Cache-first pattern (all data through EnhancedCache)
    //   - TX-only NVS persistence for state data
    //   - Works regardless of boot order, auto-recovers from router channel changes
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
    
    // Start active channel hopping in background (non-blocking)
    // Scans channels 1-13, broadcasts PROBE 1s per channel
    // When receiver ACKs: locks channel, flushes cache, continues normally
    TransmitterConnectionHandler::instance().start_discovery();
    
    LOG_INFO("DISCOVERY", "Active hopping started - continuing with network initialization...");
    LOG_INFO("DISCOVERY", "(ESP-NOW connection will be established asynchronously)");
    // ═══════════════════════════════════════════════════════════════════════
    
    // Continue with Ethernet initialization (works independently of ESP-NOW)
    if (EthernetManager::instance().is_connected()) {
        LOG_INFO("ETHERNET", "Ethernet connected: %s", EthernetManager::instance().get_local_ip().toString().c_str());
        
        // Initialize OTA
        LOG_DEBUG("OTA", "Initializing OTA server...");
        OtaManager::instance().init_http_server();
        
        // Initialize MQTT (logger will be initialized after connection in mqtt_task)
        if (config::features::MQTT_ENABLED) {
            LOG_DEBUG("MQTT", "Initializing MQTT...");
            MqttManager::instance().init();
        }
    } else {
        LOG_WARN("ETHERNET", "Ethernet not connected, network features disabled");
    }
    
    // Start ESP-NOW tasks
    LOG_DEBUG("ESPNOW", "Starting ESP-NOW tasks...");
    
    // RX task already started before discovery
    
    // Section 11: Start background transmission task (Priority 2 - LOW, Core 1)
    // Reads from EnhancedCache and transmits via ESP-NOW (non-blocking)
    TransmissionTask::instance().start(task_config::PRIORITY_LOW, 1);
    LOG_INFO("ESPNOW", "Background transmission task started (Priority 2, Core 1)");
    
    // Initialize heartbeat manager with sequence tracking and ACK
    HeartbeatManager::instance().init();
    LOG_INFO("HEARTBEAT", "Heartbeat manager initialized (10s interval, ACK-based)");
    
    // Initialize transmitter data (set starting SOC value)
    tx_data.soc = 20;
    randomSeed(esp_random());
    
    // PHASE 1: Use dummy data generator instead of real data sender
    // This will be replaced in Phase 4 with real control loop
    LOG_INFO("MAIN", "===== PHASE 1: USING DUMMY DATA GENERATOR =====");
    LOG_INFO("MAIN", "This is TEMPORARY - will be replaced in Phase 4");
    // DummyData::start(task_config::PRIORITY_LOW, 1);  // REMOVED IN PHASE 2 - clean codebase
    LOG_INFO("MAIN", "Dummy data generator removed - using real data only");
    
    // Original data sender commented out for Phase 1
    // DataSender::instance().start();
    
    // Start discovery task (periodic announcements until receiver connects)
    DiscoveryTask::instance().start();
    
    // Start MQTT task (lowest priority - background telemetry)
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
    
    // Delay before starting network time utilities
    delay(1000);
    
    // Initialize and start network time utilities (NTP sync + connectivity monitoring)
    if (init_ethernet_utilities()) {
        LOG_INFO("TIME", "Network time utilities initialized");
        if (start_ethernet_utilities_task()) {
            LOG_DEBUG("TIME", "Background NTP sync task started");
        } else {
            LOG_WARN("TIME", "Failed to start NTP sync task");
        }
    } else {
        LOG_WARN("TIME", "Failed to initialize network time utilities");
    }
    
    // Initialize TimeManager for time synchronization with receiver
    LOG_INFO("TIME", "Initializing TimeManager for time sync...");
    TimeManager::instance().init("pool.ntp.org");
    LOG_INFO("TIME", "TimeManager initialized");
    
    // Initialize version beacon manager (after all other systems are up)
    VersionBeaconManager::instance().init();
    LOG_INFO("VERSION", "Version beacon manager initialized (15s heartbeat)");
    
    // Initialize heartbeat manager (after connection manager is ready)
    HeartbeatManager::instance().init();
    LOG_INFO("HEARTBEAT", "Heartbeat manager initialized (10s interval)");
    
    LOG_INFO("MAIN", "Setup complete!");
    LOG_INFO("MAIN", "=================================");
}

void loop() {
    // All work is done in FreeRTOS tasks
    // Main loop handles periodic health checks and monitoring
    
    static uint32_t last_state_validation = 0;
    static uint32_t last_metrics_report = 0;
    static uint32_t last_peer_audit = 0;
    
    uint32_t now = millis();
    
    // Periodic state validation (every 30 seconds) - Phase 2
    if (now - last_state_validation > 30000) {
        if (!DiscoveryTask::instance().validate_state()) {
            LOG_WARN("MAIN", "State validation failed - triggering self-healing restart");
            DiscoveryTask::instance().restart();
        }
        last_state_validation = now;
    }
    
    // Recovery state machine update - Phase 2
    DiscoveryTask::instance().update_recovery();
    
    // Handle deferred logging from timer callbacks
    EspnowSendUtils::handle_deferred_logging();
    
    // Version beacon periodic update (every 15s heartbeat) - Phase 4
    VersionBeaconManager::instance().update();
    
    // Heartbeat periodic update (every 10s) - Section 11
    HeartbeatManager::instance().tick();
    
    // Metrics reporting (every 5 minutes) - Phase 3
    if (now - last_metrics_report > 300000) {
        DiscoveryTask::instance().get_metrics().log_summary();
        last_metrics_report = now;
    }
    
    // Peer state audit (every 2 minutes, if debug enabled) - Phase 2
    #if LOG_LEVEL >= LOG_LEVEL_DEBUG
    if (now - last_peer_audit > 120000) {
        DiscoveryTask::instance().audit_peer_state();
        last_peer_audit = now;
    }
    #endif
    
    vTaskDelay(pdMS_TO_TICKS(1000));
}
