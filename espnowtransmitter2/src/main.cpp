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
#include <ethernet_utilities.h>

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

// ESP-NOW handlers
#include "espnow/message_handler.h"
#include "espnow/discovery_task.h"
#include "espnow/data_sender.h"

// Global queue for ESP-NOW messages
QueueHandle_t espnow_message_queue = nullptr;

// Required by espnow_transmitter library
QueueHandle_t espnow_rx_queue = nullptr;

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(1000);
    LOG_INFO("\n=== ESP-NOW Transmitter (Modular) ===");
    LOG_INFO("Build: %s %s", __DATE__, __TIME__);
    
    // Initialize Ethernet
    LOG_INFO("Initializing Ethernet...");
    if (!EthernetManager::instance().init()) {
        LOG_ERROR("Ethernet initialization failed!");
    }
    
    // Initialize WiFi for ESP-NOW
    LOG_INFO("Initializing WiFi for ESP-NOW...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    // Don't force channel yet - let discover_and_lock_channel() scan and find receiver
    
    uint8_t mac[6];
    WiFi.macAddress(mac);
    LOG_DEBUG("WiFi MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // Initialize ESP-NOW library
    LOG_INFO("Initializing ESP-NOW...");
    espnow_message_queue = xQueueCreate(
        task_config::ESPNOW_MESSAGE_QUEUE_SIZE, 
        sizeof(espnow_queue_msg_t)
    );
    if (espnow_message_queue == nullptr) {
        LOG_ERROR("Failed to create ESP-NOW message queue!");
        return;
    }
    
    // Initialize ESP-NOW (uses library function)
    init_espnow(espnow_message_queue);
    LOG_DEBUG("ESP-NOW initialized successfully");
    
    // Start message handler (highest priority - processes incoming messages)
    // MUST start BEFORE discover_and_lock_channel() so it can process ACKs!
    EspnowMessageHandler::instance().start_rx_task(espnow_message_queue);
    delay(100);  // Let RX task initialize
    
    // Perform initial channel discovery (scans all channels, sends PROBE, waits for ACK)
    LOG_INFO("Starting channel discovery (scanning channels 1-13)...");
    discover_and_lock_channel();
    
    // Wait for Ethernet to stabilize
    LOG_DEBUG("Waiting for Ethernet connection...");
    unsigned long eth_start = millis();
    while (!EthernetManager::instance().is_connected() && 
           (millis() - eth_start < 10000)) {
        delay(500);
    }
    
    if (EthernetManager::instance().is_connected()) {
        LOG_INFO("Ethernet connected: %s", EthernetManager::instance().get_local_ip().toString().c_str());
        
        // Initialize OTA
        LOG_DEBUG("Initializing OTA server...");
        OtaManager::instance().init_http_server();
        
        // Initialize MQTT
        if (config::features::MQTT_ENABLED) {
            LOG_DEBUG("Initializing MQTT...");
            MqttManager::instance().init();
        }
    } else {
        LOG_WARN("Ethernet not connected, network features disabled");
    }
    
    // Start ESP-NOW tasks
    LOG_DEBUG("Starting ESP-NOW tasks...");
    
    // RX task already started before discovery
    
    // Initialize transmitter data (set starting SOC value)
    tx_data.soc = 20;
    randomSeed(esp_random());
    
    // Start data sender (sends test data when active)
    DataSender::instance().start();
    
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
        LOG_INFO("Network time utilities initialized");
        if (start_ethernet_utilities_task()) {
            LOG_DEBUG("Background NTP sync task started");
        } else {
            LOG_WARN("Failed to start NTP sync task");
        }
    } else {
        LOG_WARN("Failed to initialize network time utilities");
    }
    
    LOG_INFO("Setup complete!");
    LOG_INFO("=================================");
}

void loop() {
    // All work is done in FreeRTOS tasks
    // Main loop can be used for watchdog feeding or low-priority work
    vTaskDelay(pdMS_TO_TICKS(1000));
}
