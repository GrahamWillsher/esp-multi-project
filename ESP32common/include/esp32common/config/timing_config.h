#pragma once

#include <cstdint>

/**
 * Shared timing contract for cross-codebase behavior.
 *
 * This is the canonical source of timing constants that are shared across
 * transmitter, receiver, and common utilities.
 */
namespace TimingConfig {

struct StartupTiming {
    uint32_t serial_init_delay_ms;
    uint32_t wifi_radio_stabilization_ms;
    uint32_t post_init_delay_ms;
    uint32_t component_init_delay_ms;
};

struct DiscoveryTiming {
    uint32_t retry_interval_ms;
    uint32_t deferred_poll_ms;
    uint32_t announcement_interval_ms;
    uint32_t channel_stabilization_ms;
    uint32_t peer_registration_delay_ms;
    uint32_t channel_switching_delay_ms;
    uint32_t probe_interval_ms;
    uint32_t transmit_duration_per_channel_ms;
    uint32_t restart_initial_backoff_ms;
    uint32_t restart_stabilization_delay_ms;
    uint32_t recovery_retry_delay_ms;
    uint32_t recovery_timeout_ms;
};

struct HeartbeatTiming {
    uint32_t interval_ms;
    uint32_t timeout_ms;
    uint32_t tx_timeout_ms;
    uint32_t espnow_connecting_timeout_ms;
    uint32_t ack_timeout_ms;
};

struct ReceiverEspnowTiming {
    uint32_t queue_receive_timeout_ms;
    uint32_t stale_timeout_ms;
    uint32_t config_update_grace_ms;
    uint32_t queue_stats_log_interval_ms;
};

struct DataTransmissionTiming {
    uint32_t data_transmit_interval_ms;
    uint32_t espnow_send_interval_ms;
    uint32_t send_timeout_ms;
};

struct VersionBeaconTiming {
    uint32_t interval_ms;
    uint32_t min_interval_ms;
};

struct EthernetTiming {
    uint32_t phy_reset_timeout_ms;
    uint32_t config_apply_timeout_ms;
    uint32_t link_acquiring_timeout_ms;
    uint32_t ip_acquiring_timeout_ms;
    uint32_t recovery_timeout_ms;
    uint32_t init_delay_ms;
    uint32_t phy_reset_delay_ms;
    uint32_t phy_power_assert_delay_ms;
};

struct MqttTiming {
    uint32_t connect_timeout_ms;
    uint32_t initial_retry_delay_ms;
    uint32_t max_retry_delay_ms;
    uint32_t publish_interval_ms;
    uint32_t reconnect_interval_ms;
    uint32_t subscribe_delay_ms;
    uint32_t connection_stabilization_ms;
    uint32_t task_delay_ms;
    uint32_t loop_delay_ms;
    uint32_t stats_log_interval_ms;
    uint32_t cell_publish_interval_ms;
    uint32_t event_publish_interval_ms;
    uint32_t task_startup_delay_ms;
    uint32_t task_poll_ms;
};

struct TimeSyncTiming {
    uint32_t ntp_resync_interval_ms;
};

struct OtaTiming {
    uint32_t timeout_ms;
    uint32_t pre_reboot_delay_ms;
};

struct LoopTiming {
    uint32_t reboot_delay_ms;
    uint32_t settings_update_delay_ms;
    uint32_t component_config_delay_ms;
    uint32_t main_loop_delay_ms;
    uint32_t queue_flush_poll_delay_ms;
    uint32_t eth_state_machine_update_interval_ms;
    uint32_t can_stats_log_interval_ms;
    uint32_t state_validation_interval_ms;
    uint32_t metrics_report_interval_ms;
    uint32_t peer_audit_interval_ms;
    uint32_t mutex_timeout_ms;
    uint32_t can_rx_timeout_ms;
};

constexpr StartupTiming STARTUP{
    1000,
    100,
    1000,
    100,
};

constexpr DiscoveryTiming DISCOVERY{
    5000,
    250,
    5000,
    150,
    100,
    300,
    100,
    1000,
    500,
    100,
    5000,
    60000,
};

constexpr HeartbeatTiming HEARTBEAT{
    10000,
    30000,
    35000,
    30000,
    1000,
};

constexpr ReceiverEspnowTiming RECEIVER_ESPNOW{
    1000,
    90000,
    5000,
    15000,
};

constexpr DataTransmissionTiming DATA_TRANSMISSION{
    50,
    2000,
    500,
};

constexpr VersionBeaconTiming VERSION_BEACON{
    15000,
    1000,
};

constexpr EthernetTiming ETHERNET{
    5000,
    5000,
    5000,
    30000,
    60000,
    150,
    2000,
    10,
};

constexpr MqttTiming MQTT{
    10000,
    5000,
    300000,
    10000,
    5000,
    100,
    500,
    5000,
    1000,
    30000,
    1000,
    5000,
    2000,
    100,
};

constexpr TimeSyncTiming TIME_SYNC{
    3600000,
};

constexpr OtaTiming OTA{
    60000,
    1000,
};

constexpr LoopTiming LOOPS{
    1000,
    1000,
    1000,
    1000,
    10,
    1000,
    10000,
    30000,
    300000,
    120000,
    10,
    10,
};

// ============================================================================
// INITIALIZATION & STARTUP
// ============================================================================
constexpr uint32_t SERIAL_INIT_DELAY_MS = STARTUP.serial_init_delay_ms;
constexpr uint32_t WIFI_RADIO_STABILIZATION_MS = STARTUP.wifi_radio_stabilization_ms;
constexpr uint32_t POST_INIT_DELAY_MS = STARTUP.post_init_delay_ms;
constexpr uint32_t COMPONENT_INIT_DELAY_MS = STARTUP.component_init_delay_ms;

// ============================================================================
// DISCOVERY & CHANNEL HOPPING
// ============================================================================
constexpr uint32_t DISCOVERY_RETRY_INTERVAL_MS = DISCOVERY.retry_interval_ms;
constexpr uint32_t DEFERRED_DISCOVERY_POLL_MS = DISCOVERY.deferred_poll_ms;
constexpr uint32_t ANNOUNCEMENT_INTERVAL_MS = DISCOVERY.announcement_interval_ms;
constexpr uint32_t CHANNEL_STABILIZATION_MS = DISCOVERY.channel_stabilization_ms;
constexpr uint32_t PEER_REGISTRATION_DELAY_MS = DISCOVERY.peer_registration_delay_ms;
constexpr uint32_t CHANNEL_SWITCHING_DELAY_MS = DISCOVERY.channel_switching_delay_ms;

constexpr uint32_t PROBE_INTERVAL_MS = DISCOVERY.probe_interval_ms;
constexpr uint32_t TRANSMIT_DURATION_PER_CHANNEL_MS = DISCOVERY.transmit_duration_per_channel_ms;

constexpr uint32_t RESTART_INITIAL_BACKOFF_MS = DISCOVERY.restart_initial_backoff_ms;
constexpr uint32_t RESTART_STABILIZATION_DELAY_MS = DISCOVERY.restart_stabilization_delay_ms;

constexpr uint32_t RECOVERY_RETRY_DELAY_MS = DISCOVERY.recovery_retry_delay_ms;
constexpr uint32_t RECOVERY_TIMEOUT_MS = DISCOVERY.recovery_timeout_ms;

// ============================================================================
// HEARTBEAT & CONNECTION MONITORING
// ============================================================================
constexpr uint32_t HEARTBEAT_INTERVAL_MS = HEARTBEAT.interval_ms;
constexpr uint32_t HEARTBEAT_TIMEOUT_MS = HEARTBEAT.timeout_ms;
constexpr uint32_t TX_HEARTBEAT_TIMEOUT_MS = HEARTBEAT.tx_timeout_ms;
constexpr uint32_t ESPNOW_CONNECTING_TIMEOUT_MS = HEARTBEAT.espnow_connecting_timeout_ms;
constexpr uint32_t HEARTBEAT_ACK_TIMEOUT_MS = HEARTBEAT.ack_timeout_ms;

// ============================================================================
// RECEIVER ESP-NOW TASKS
// ============================================================================
constexpr uint32_t RX_ESPNOW_QUEUE_RECEIVE_TIMEOUT_MS = RECEIVER_ESPNOW.queue_receive_timeout_ms;
constexpr uint32_t RX_ESPNOW_STALE_TIMEOUT_MS = RECEIVER_ESPNOW.stale_timeout_ms;
constexpr uint32_t RX_ESPNOW_CONFIG_UPDATE_GRACE_MS = RECEIVER_ESPNOW.config_update_grace_ms;
constexpr uint32_t RX_ESPNOW_QUEUE_STATS_LOG_INTERVAL_MS = RECEIVER_ESPNOW.queue_stats_log_interval_ms;

// ============================================================================
// DATA TRANSMISSION
// ============================================================================
constexpr uint32_t DATA_TRANSMIT_INTERVAL_MS = DATA_TRANSMISSION.data_transmit_interval_ms;
constexpr uint32_t ESPNOW_SEND_INTERVAL_MS = DATA_TRANSMISSION.espnow_send_interval_ms;
constexpr uint32_t DATA_SEND_TIMEOUT_MS = DATA_TRANSMISSION.send_timeout_ms;

// ============================================================================
// VERSION BEACONS
// ============================================================================
constexpr uint32_t VERSION_BEACON_INTERVAL_MS = VERSION_BEACON.interval_ms;
constexpr uint32_t MIN_BEACON_INTERVAL_MS = VERSION_BEACON.min_interval_ms;

// ============================================================================
// NETWORK - ETHERNET
// ============================================================================
constexpr uint32_t ETHERNET_PHY_RESET_TIMEOUT_MS = ETHERNET.phy_reset_timeout_ms;
constexpr uint32_t ETHERNET_CONFIG_APPLY_TIMEOUT_MS = ETHERNET.config_apply_timeout_ms;
constexpr uint32_t ETHERNET_LINK_ACQUIRING_TIMEOUT_MS = ETHERNET.link_acquiring_timeout_ms;
constexpr uint32_t ETHERNET_IP_ACQUIRING_TIMEOUT_MS = ETHERNET.ip_acquiring_timeout_ms;
constexpr uint32_t ETHERNET_RECOVERY_TIMEOUT_MS = ETHERNET.recovery_timeout_ms;
constexpr uint32_t ETHERNET_INIT_DELAY_MS = ETHERNET.init_delay_ms;
constexpr uint32_t ETHERNET_PHY_RESET_DELAY_MS = ETHERNET.phy_reset_delay_ms;
constexpr uint32_t ETHERNET_PHY_POWER_ASSERT_DELAY_MS = ETHERNET.phy_power_assert_delay_ms;  ///< PHY power-pin assert-low duration (reset pulse width ms)

// ============================================================================
// NETWORK - MQTT
// ============================================================================
constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = MQTT.connect_timeout_ms;
constexpr uint32_t MQTT_INITIAL_RETRY_DELAY_MS = MQTT.initial_retry_delay_ms;
constexpr uint32_t MQTT_MAX_RETRY_DELAY_MS = MQTT.max_retry_delay_ms;
constexpr uint32_t MQTT_PUBLISH_INTERVAL_MS = MQTT.publish_interval_ms;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = MQTT.reconnect_interval_ms;
constexpr uint32_t MQTT_SUBSCRIBE_DELAY_MS = MQTT.subscribe_delay_ms;
constexpr uint32_t MQTT_CONNECTION_STABILIZATION_MS = MQTT.connection_stabilization_ms;
constexpr uint32_t MQTT_TASK_DELAY_MS = MQTT.task_delay_ms;
constexpr uint32_t MQTT_LOOP_DELAY_MS = MQTT.loop_delay_ms;
constexpr uint32_t MQTT_STATS_LOG_INTERVAL_MS = MQTT.stats_log_interval_ms;
constexpr uint32_t MQTT_CELL_PUBLISH_INTERVAL_MS = MQTT.cell_publish_interval_ms;
constexpr uint32_t MQTT_EVENT_PUBLISH_INTERVAL_MS = MQTT.event_publish_interval_ms;
/// Initial delay before the receiver MQTT task attempts its first connection.
/// Allows WiFi and ReceiverNetworkConfig to fully initialise first.
constexpr uint32_t MQTT_TASK_STARTUP_DELAY_MS = MQTT.task_startup_delay_ms;
/// FreeRTOS loop poll interval for the receiver MQTT task (10 times/second).
constexpr uint32_t MQTT_TASK_POLL_MS = MQTT.task_poll_ms;

// ============================================================================
// NETWORK - NTP / OTA
// ============================================================================
constexpr uint32_t NTP_RESYNC_INTERVAL_MS = TIME_SYNC.ntp_resync_interval_ms;
constexpr uint32_t OTA_TIMEOUT_MS = OTA.timeout_ms;
constexpr uint32_t OTA_PRE_REBOOT_DELAY_MS = OTA.pre_reboot_delay_ms;

// ============================================================================
// MESSAGE HANDLING / LOOPS / SYNCHRONIZATION
// ============================================================================
constexpr uint32_t REBOOT_DELAY_MS = LOOPS.reboot_delay_ms;
constexpr uint32_t SETTINGS_UPDATE_DELAY_MS = LOOPS.settings_update_delay_ms;
constexpr uint32_t COMPONENT_CONFIG_DELAY_MS = LOOPS.component_config_delay_ms;

constexpr uint32_t MAIN_LOOP_DELAY_MS = LOOPS.main_loop_delay_ms;
constexpr uint32_t QUEUE_FLUSH_POLL_DELAY_MS = LOOPS.queue_flush_poll_delay_ms;

constexpr uint32_t ETH_STATE_MACHINE_UPDATE_INTERVAL_MS = LOOPS.eth_state_machine_update_interval_ms;
constexpr uint32_t CAN_STATS_LOG_INTERVAL_MS = LOOPS.can_stats_log_interval_ms;
constexpr uint32_t STATE_VALIDATION_INTERVAL_MS = LOOPS.state_validation_interval_ms;
constexpr uint32_t METRICS_REPORT_INTERVAL_MS = LOOPS.metrics_report_interval_ms;
constexpr uint32_t PEER_AUDIT_INTERVAL_MS = LOOPS.peer_audit_interval_ms;

constexpr uint32_t MUTEX_TIMEOUT_MS = LOOPS.mutex_timeout_ms;
constexpr uint32_t CAN_RX_TIMEOUT_MS = LOOPS.can_rx_timeout_ms;

// ============================================================================
// Shared sanity checks
// ============================================================================
static_assert(MQTT_MAX_RETRY_DELAY_MS >= MQTT_INITIAL_RETRY_DELAY_MS,
              "MQTT_MAX_RETRY_DELAY_MS must be >= MQTT_INITIAL_RETRY_DELAY_MS");
static_assert(HEARTBEAT_TIMEOUT_MS > HEARTBEAT_INTERVAL_MS,
              "HEARTBEAT_TIMEOUT_MS must be > HEARTBEAT_INTERVAL_MS");

} // namespace TimingConfig
