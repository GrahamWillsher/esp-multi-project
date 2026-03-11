#pragma once

#include <cstdint>

/**
 * TimingConfig - Centralized timing constants for the transmitter application
 * 
 * Benefits:
 * - Single source of truth for all timing values
 * - Easy tuning and optimization
 * - Clear documentation of timing intent
 * - Consistency across modules
 * - Prevents magic numbers scattered in code
 */
namespace TimingConfig {
    
    // ═══════════════════════════════════════════════════════════════════
    // INITIALIZATION & STARTUP
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t SERIAL_INIT_DELAY_MS = 1000;         // Serial port stabilization
    constexpr uint32_t WIFI_RADIO_STABILIZATION_MS = 100;   // WiFi radio startup
    constexpr uint32_t POST_INIT_DELAY_MS = 1000;           // General post-init delay
    constexpr uint32_t COMPONENT_INIT_DELAY_MS = 100;       // Between component inits
    
    // ═══════════════════════════════════════════════════════════════════
    // DISCOVERY & CHANNEL HOPPING
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t DISCOVERY_RETRY_INTERVAL_MS = 5000;     // 5s retry in active mode (vs 10s passive)
    constexpr uint32_t CHANNEL_STABILIZATION_MS = 150;         // Industrial-grade: WiFi driver stabilization
    constexpr uint32_t PEER_REGISTRATION_DELAY_MS = 100;       // Peer registration stabilization
    constexpr uint32_t CHANNEL_SWITCHING_DELAY_MS = 300;       // Channel switch stabilization
    
    // Active hopping parameters
    constexpr uint32_t PROBE_INTERVAL_MS = 100;                // PROBE broadcast frequency per channel
    constexpr uint32_t TRANSMIT_DURATION_PER_CHANNEL_MS = 1000; // Time spent on each channel
    
    // Restart backoff
    constexpr uint32_t RESTART_INITIAL_BACKOFF_MS = 500;       // Base: 500, 1000, 2000ms (exponential)
    constexpr uint32_t RESTART_STABILIZATION_DELAY_MS = 100;   // Post-restart stabilization
    
    // Recovery timeouts
    constexpr uint32_t RECOVERY_RETRY_DELAY_MS = 5000;         // 5s before retry in recovery
    constexpr uint32_t RECOVERY_TIMEOUT_MS = 60000;            // 60s max recovery time
    
    // ═══════════════════════════════════════════════════════════════════
    // HEARTBEAT & CONNECTION MONITORING
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t HEARTBEAT_INTERVAL_MS = 10000;          // 10 second heartbeat
    constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 30000;           // 30s without response = disconnect
    constexpr uint32_t HEARTBEAT_ACK_TIMEOUT_MS = 1000;        // 1s wait for ACK
    
    // ═══════════════════════════════════════════════════════════════════
    // DATA TRANSMISSION
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t DATA_TRANSMIT_INTERVAL_MS = 50;         // 20 msg/sec max (from transmission_task)
    constexpr uint32_t ESPNOW_SEND_INTERVAL_MS = 2000;         // Legacy: 2s data transmit rate
    constexpr uint32_t DATA_SEND_TIMEOUT_MS = 500;             // ESP-NOW send timeout
    
    // ═══════════════════════════════════════════════════════════════════
    // VERSION BEACONS
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t VERSION_BEACON_INTERVAL_MS = 15000;     // 15 second version beacon
    constexpr uint32_t MIN_BEACON_INTERVAL_MS = 1000;          // Rate limit: 1s minimum between beacons
    
    // ═══════════════════════════════════════════════════════════════════
    // NETWORK - ETHERNET
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t ETHERNET_PHY_RESET_TIMEOUT_MS = 5000;      // PHY reset timeout
    constexpr uint32_t ETHERNET_CONFIG_APPLY_TIMEOUT_MS = 5000;   // Config apply timeout
    constexpr uint32_t ETHERNET_LINK_ACQUIRING_TIMEOUT_MS = 5000; // Link acquisition timeout
    constexpr uint32_t ETHERNET_IP_ACQUIRING_TIMEOUT_MS = 30000;  // DHCP timeout (critical)
    constexpr uint32_t ETHERNET_RECOVERY_TIMEOUT_MS = 60000;      // Error recovery timeout
    constexpr uint32_t ETHERNET_INIT_DELAY_MS = 150;              // Init stabilization
    constexpr uint32_t ETHERNET_PHY_RESET_DELAY_MS = 2000;        // PHY reset delay
    
    // ═══════════════════════════════════════════════════════════════════
    // NETWORK - MQTT
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = 10000;           // 10s connection timeout
    constexpr uint32_t MQTT_INITIAL_RETRY_DELAY_MS = 5000;        // 5s initial retry delay
    constexpr uint32_t MQTT_MAX_RETRY_DELAY_MS = 300000;          // 5min max retry delay
    constexpr uint32_t MQTT_PUBLISH_INTERVAL_MS = 10000;          // 10s telemetry publish interval
    constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;         // 5s reconnect interval (legacy)
    constexpr uint32_t MQTT_SUBSCRIBE_DELAY_MS = 100;             // Delay between subscriptions
    constexpr uint32_t MQTT_CONNECTION_STABILIZATION_MS = 500;    // Post-connection stabilization
    constexpr uint32_t MQTT_TASK_DELAY_MS = 5000;                 // Main task loop delay
    constexpr uint32_t MQTT_LOOP_DELAY_MS = 1000;                 // Loop processing delay
    
    // ═══════════════════════════════════════════════════════════════════
    // NETWORK - NTP
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t NTP_RESYNC_INTERVAL_MS = 3600000;          // 1 hour resync
    
    // ═══════════════════════════════════════════════════════════════════
    // NETWORK - OTA
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t OTA_TIMEOUT_MS = 60000;                    // 60s OTA timeout
    constexpr uint32_t OTA_PRE_REBOOT_DELAY_MS = 1000;            // Delay before reboot
    
    // ═══════════════════════════════════════════════════════════════════
    // MESSAGE HANDLING
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t REBOOT_DELAY_MS = 1000;                    // Delay before reboot
    constexpr uint32_t SETTINGS_UPDATE_DELAY_MS = 1000;           // Settings update processing
    constexpr uint32_t COMPONENT_CONFIG_DELAY_MS = 1000;          // Component config delay
    
    // ═══════════════════════════════════════════════════════════════════
    // TASK LOOP DELAYS
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t MAIN_LOOP_DELAY_MS = 1000;                 // Main loop iteration
    constexpr uint32_t QUEUE_FLUSH_POLL_DELAY_MS = 10;            // Queue flush polling
    
    // ═══════════════════════════════════════════════════════════════════
    // CACHE & MUTEX
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t MUTEX_TIMEOUT_MS = 10;                     // Non-blocking mutex timeout
    
    // ═══════════════════════════════════════════════════════════════════
    // CAN BUS
    // ═══════════════════════════════════════════════════════════════════
    
    constexpr uint32_t CAN_RX_TIMEOUT_MS = 10;                    // CAN receive timeout
    
} // namespace TimingConfig
