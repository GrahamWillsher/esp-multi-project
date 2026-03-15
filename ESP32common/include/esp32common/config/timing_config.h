#pragma once

#include <cstdint>

/**
 * Shared timing contract for cross-codebase behavior.
 *
 * This is the canonical source of timing constants that are shared across
 * transmitter, receiver, and common utilities.
 */
namespace TimingConfig {

// ============================================================================
// INITIALIZATION & STARTUP
// ============================================================================
constexpr uint32_t SERIAL_INIT_DELAY_MS = 1000;
constexpr uint32_t WIFI_RADIO_STABILIZATION_MS = 100;
constexpr uint32_t POST_INIT_DELAY_MS = 1000;
constexpr uint32_t COMPONENT_INIT_DELAY_MS = 100;

// ============================================================================
// DISCOVERY & CHANNEL HOPPING
// ============================================================================
constexpr uint32_t DISCOVERY_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t CHANNEL_STABILIZATION_MS = 150;
constexpr uint32_t PEER_REGISTRATION_DELAY_MS = 100;
constexpr uint32_t CHANNEL_SWITCHING_DELAY_MS = 300;

constexpr uint32_t PROBE_INTERVAL_MS = 100;
constexpr uint32_t TRANSMIT_DURATION_PER_CHANNEL_MS = 1000;

constexpr uint32_t RESTART_INITIAL_BACKOFF_MS = 500;
constexpr uint32_t RESTART_STABILIZATION_DELAY_MS = 100;

constexpr uint32_t RECOVERY_RETRY_DELAY_MS = 5000;
constexpr uint32_t RECOVERY_TIMEOUT_MS = 60000;

// ============================================================================
// HEARTBEAT & CONNECTION MONITORING
// ============================================================================
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 10000;
constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 30000;
constexpr uint32_t HEARTBEAT_ACK_TIMEOUT_MS = 1000;

// ============================================================================
// DATA TRANSMISSION
// ============================================================================
constexpr uint32_t DATA_TRANSMIT_INTERVAL_MS = 50;
constexpr uint32_t ESPNOW_SEND_INTERVAL_MS = 2000;
constexpr uint32_t DATA_SEND_TIMEOUT_MS = 500;

// ============================================================================
// VERSION BEACONS
// ============================================================================
constexpr uint32_t VERSION_BEACON_INTERVAL_MS = 15000;
constexpr uint32_t MIN_BEACON_INTERVAL_MS = 1000;

// ============================================================================
// NETWORK - ETHERNET
// ============================================================================
constexpr uint32_t ETHERNET_PHY_RESET_TIMEOUT_MS = 5000;
constexpr uint32_t ETHERNET_CONFIG_APPLY_TIMEOUT_MS = 5000;
constexpr uint32_t ETHERNET_LINK_ACQUIRING_TIMEOUT_MS = 5000;
constexpr uint32_t ETHERNET_IP_ACQUIRING_TIMEOUT_MS = 30000;
constexpr uint32_t ETHERNET_RECOVERY_TIMEOUT_MS = 60000;
constexpr uint32_t ETHERNET_INIT_DELAY_MS = 150;
constexpr uint32_t ETHERNET_PHY_RESET_DELAY_MS = 2000;

// ============================================================================
// NETWORK - MQTT
// ============================================================================
constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t MQTT_INITIAL_RETRY_DELAY_MS = 5000;
constexpr uint32_t MQTT_MAX_RETRY_DELAY_MS = 300000;
constexpr uint32_t MQTT_PUBLISH_INTERVAL_MS = 10000;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
constexpr uint32_t MQTT_SUBSCRIBE_DELAY_MS = 100;
constexpr uint32_t MQTT_CONNECTION_STABILIZATION_MS = 500;
constexpr uint32_t MQTT_TASK_DELAY_MS = 5000;
constexpr uint32_t MQTT_LOOP_DELAY_MS = 1000;

// ============================================================================
// NETWORK - NTP / OTA
// ============================================================================
constexpr uint32_t NTP_RESYNC_INTERVAL_MS = 3600000;
constexpr uint32_t OTA_TIMEOUT_MS = 60000;
constexpr uint32_t OTA_PRE_REBOOT_DELAY_MS = 1000;

// ============================================================================
// MESSAGE HANDLING / LOOPS / SYNCHRONIZATION
// ============================================================================
constexpr uint32_t REBOOT_DELAY_MS = 1000;
constexpr uint32_t SETTINGS_UPDATE_DELAY_MS = 1000;
constexpr uint32_t COMPONENT_CONFIG_DELAY_MS = 1000;

constexpr uint32_t MAIN_LOOP_DELAY_MS = 1000;
constexpr uint32_t QUEUE_FLUSH_POLL_DELAY_MS = 10;

constexpr uint32_t MUTEX_TIMEOUT_MS = 10;
constexpr uint32_t CAN_RX_TIMEOUT_MS = 10;

// ============================================================================
// Shared sanity checks
// ============================================================================
static_assert(MQTT_MAX_RETRY_DELAY_MS >= MQTT_INITIAL_RETRY_DELAY_MS,
              "MQTT_MAX_RETRY_DELAY_MS must be >= MQTT_INITIAL_RETRY_DELAY_MS");
static_assert(HEARTBEAT_TIMEOUT_MS > HEARTBEAT_INTERVAL_MS,
              "HEARTBEAT_TIMEOUT_MS must be > HEARTBEAT_INTERVAL_MS");

} // namespace TimingConfig
