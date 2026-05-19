#ifndef MQTT_TOPICS_COMMON_H
#define MQTT_TOPICS_COMMON_H

/**
 * @file mqtt_topics_common.h
 * @brief Centralized MQTT topic namespace constants
 * 
 * This header defines all MQTT topic patterns and constants for the battery-emulator
 * MQTT-only transport layer. The namespace is versioned to support future evolution:
 * 
 *   batt-emu/mqtt-v1/
 *     ├─ tx/          (transmitter publishes)
 *     │  ├─ state/    (live telemetry + static config)
 *     │  ├─ meta/     (version, schema, runtime info)
 *     │  └─ ack/      (command acknowledgments)
 *     └─ rx/          (receiver publishes)
 *        ├─ cmd/      (receiver commands to transmitter)
 *        ├─ meta/     (receiver version/identity)
 *        └─ state/    (receiver transient state like pending updates)
 * 
 * All topic strings are defined as constexpr to prevent drift and enable
 * compile-time ACL/review.
 */

#include <string_view>
#include <array>

namespace mqtt {
namespace topics {

// ============================================================================
// NAMESPACE ROOT
// ============================================================================

/// Primary namespace root: battery-emulator MQTT v1
inline constexpr std::string_view NAMESPACE_ROOT = "batt-emu/mqtt-v1";

// ============================================================================
// TRANSMITTER PUBLISH TOPICS (TX → RX)
// ============================================================================

namespace tx {

// --- State/Telemetry Topics ---

/// Transmitter heartbeat (QoS0, non-retained)
/// Payload: { ts_ms, status, mqtt_connected, ethernet_connected }
inline constexpr std::string_view STATE_HEARTBEAT = "batt-emu/mqtt-v1/tx/state/heartbeat";

/// Live battery telemetry (QoS0, non-retained, periodic)
/// Payload: { ts_ms, soc, voltage, current, temperature, ... }
inline constexpr std::string_view STATE_BATTERY_LIVE = "batt-emu/mqtt-v1/tx/state/battery_live";

/// Static battery configuration (QoS1, retained)
/// Payload: full battery model snapshot (chemistry, capacity, cells, etc.)
inline constexpr std::string_view STATE_STATIC_BATTERY = "batt-emu/mqtt-v1/tx/state/static/battery";

/// Static power profile configuration (QoS1, retained)
/// Payload: { rated_power, profiles[], mode, ... }
inline constexpr std::string_view STATE_STATIC_POWER = "batt-emu/mqtt-v1/tx/state/static/power";

/// Static inverter configuration (QoS1, retained)
inline constexpr std::string_view STATE_STATIC_INVERTER = "batt-emu/mqtt-v1/tx/state/static/inverter";

/// Static network configuration (QoS1, retained)
/// Payload: { hostname, mac, ip_mode, dns, ... }
inline constexpr std::string_view STATE_STATIC_NETWORK = "batt-emu/mqtt-v1/tx/state/static/network";

/// Static MQTT configuration (QoS1, retained)
/// Payload: { broker_host, port, user (redacted in logs), qos_policy, ... }
inline constexpr std::string_view STATE_STATIC_MQTT = "batt-emu/mqtt-v1/tx/state/static/mqtt";

/// Static LED state/configuration (QoS1, retained)
/// Payload: { current_state, brightness, mode, ... }
inline constexpr std::string_view STATE_STATIC_LED = "batt-emu/mqtt-v1/tx/state/static/led";

/// Battery type catalog (QoS1, retained)
/// Payload: array of { id, name, chemistry, capacity, ... }
inline constexpr std::string_view STATE_STATIC_CATALOG_BATTERY = "batt-emu/mqtt-v1/tx/state/static/catalog_battery";

/// Inverter type catalog (QoS1, retained)
inline constexpr std::string_view STATE_STATIC_CATALOG_INVERTER = "batt-emu/mqtt-v1/tx/state/static/catalog_inverter";

/// Event-log summary (QoS1, non-retained)
/// Payload: { total_count, warning_count, error_count, last_ts, ... }
inline constexpr std::string_view STATE_SUMMARY_EVENT_LOGS = "batt-emu/mqtt-v1/tx/state/summary/event_logs";

/// Event-log data chunk (QoS1, non-retained)
/// Payload: { chunk_id, part_num, total_parts, data_base64, ... }
inline constexpr std::string_view STATE_EVENT_LOGS_CHUNK = "batt-emu/mqtt-v1/tx/state/event_logs/chunk";

/// Cell voltage/temperature data chunk (QoS1, non-retained)
/// Payload: { chunk_id, part_num, total_parts, data_base64, ... }
inline constexpr std::string_view STATE_CELL_DATA_CHUNK = "batt-emu/mqtt-v1/tx/state/cell_data/chunk";

// --- Metadata Topics ---

/// Transmitter firmware version and compatibility info (QoS1, retained)
/// Payload: { firmware_version, build_date, proto_version, capabilities, ... }
inline constexpr std::string_view META_VERSION = "batt-emu/mqtt-v1/tx/meta/version";

/// Per-model schema versions (QoS1, retained)
/// Payload: { battery: N, power: N, network: N, mqtt: N, led: N, ... }
inline constexpr std::string_view META_SCHEMA_VERSIONS = "batt-emu/mqtt-v1/tx/meta/schema_versions";

/// Transmitter runtime state (QoS1, retained)
/// Payload: { uptime_ms, mqtt_connected, ethernet_connected, task_heap_free, ... }
inline constexpr std::string_view META_RUNTIME = "batt-emu/mqtt-v1/tx/meta/runtime";

// --- ACK/Result Topics ---

/// Generic ACK for battery configuration update (QoS1, non-retained)
/// Payload: { request_id, status, message, applied_version, ... }
inline constexpr std::string_view ACK_BATTERY = "batt-emu/mqtt-v1/tx/ack/battery";

/// ACK for power configuration update
inline constexpr std::string_view ACK_POWER = "batt-emu/mqtt-v1/tx/ack/power";

/// ACK for network configuration update
inline constexpr std::string_view ACK_NETWORK = "batt-emu/mqtt-v1/tx/ack/network";

/// ACK for MQTT configuration update
inline constexpr std::string_view ACK_MQTT = "batt-emu/mqtt-v1/tx/ack/mqtt";

/// ACK for inverter configuration update
inline constexpr std::string_view ACK_INVERTER = "batt-emu/mqtt-v1/tx/ack/inverter";

/// ACK for CAN configuration update
inline constexpr std::string_view ACK_CAN = "batt-emu/mqtt-v1/tx/ack/can";

/// ACK for contactor control
inline constexpr std::string_view ACK_CONTACTOR = "batt-emu/mqtt-v1/tx/ack/contactor";

/// ACK for control/command (reboot, debug_level, etc.)
inline constexpr std::string_view ACK_CONTROL = "batt-emu/mqtt-v1/tx/ack/control";

/// ACK for component application
inline constexpr std::string_view ACK_COMPONENT = "batt-emu/mqtt-v1/tx/ack/component";

/// ACK for event-log clear operation
inline constexpr std::string_view ACK_EVENT_LOGS_CLEAR = "batt-emu/mqtt-v1/tx/ack/event_logs_clear";

/// ACK for data refresh request
inline constexpr std::string_view ACK_REFRESH = "batt-emu/mqtt-v1/tx/ack/refresh";

/// Wildcard for all transmitter ACKs (subscribe pattern)
inline constexpr std::string_view ACK_WILDCARD = "batt-emu/mqtt-v1/tx/ack/#";

/// Wildcard for all transmitter state topics (subscribe pattern)
inline constexpr std::string_view STATE_WILDCARD = "batt-emu/mqtt-v1/tx/state/#";

/// Wildcard for all transmitter metadata topics (subscribe pattern)
inline constexpr std::string_view META_WILDCARD = "batt-emu/mqtt-v1/tx/meta/#";

} // namespace tx

// ============================================================================
// RECEIVER PUBLISH TOPICS (RX → TX)
// ============================================================================

namespace rx {

// --- Command Topics ---

/// Battery configuration update command (QoS1)
/// Payload: { request_id, base_version, fields: { ... }, ts_ms }
inline constexpr std::string_view CMD_UPDATE_BATTERY = "batt-emu/mqtt-v1/rx/cmd/update/battery";

/// Power configuration update command
inline constexpr std::string_view CMD_UPDATE_POWER = "batt-emu/mqtt-v1/rx/cmd/update/power";

/// Network configuration update command
inline constexpr std::string_view CMD_UPDATE_NETWORK = "batt-emu/mqtt-v1/rx/cmd/update/network";

/// MQTT configuration update command
inline constexpr std::string_view CMD_UPDATE_MQTT = "batt-emu/mqtt-v1/rx/cmd/update/mqtt";

/// Inverter configuration update command
inline constexpr std::string_view CMD_UPDATE_INVERTER = "batt-emu/mqtt-v1/rx/cmd/update/inverter";

/// CAN configuration update command
inline constexpr std::string_view CMD_UPDATE_CAN = "batt-emu/mqtt-v1/rx/cmd/update/can";

/// Contactor control command
inline constexpr std::string_view CMD_UPDATE_CONTACTOR = "batt-emu/mqtt-v1/rx/cmd/update/contactor";

/// Generic update pattern (subscribe for all models)
inline constexpr std::string_view CMD_UPDATE_WILDCARD = "batt-emu/mqtt-v1/rx/cmd/update/#";

/// Control/Action commands (reboot, debug_level, etc.)
/// Payload: { request_id, action, params, ts_ms }
inline constexpr std::string_view CMD_CONTROL_DEBUG_LEVEL = "batt-emu/mqtt-v1/rx/cmd/control/debug_level";
inline constexpr std::string_view CMD_CONTROL_TEST_DATA_MODE = "batt-emu/mqtt-v1/rx/cmd/control/test_data_mode";
inline constexpr std::string_view CMD_CONTROL_REBOOT = "batt-emu/mqtt-v1/rx/cmd/control/reboot";
inline constexpr std::string_view CMD_CONTROL_OTA_START = "batt-emu/mqtt-v1/rx/cmd/control/ota_start";
inline constexpr std::string_view CMD_CONTROL_COMPONENT_APPLY = "batt-emu/mqtt-v1/rx/cmd/control/component_apply";
inline constexpr std::string_view CMD_CONTROL_EVENT_LOGS_CLEAR = "batt-emu/mqtt-v1/rx/cmd/control/event_logs_clear";

/// Generic control pattern (subscribe for all actions)
inline constexpr std::string_view CMD_CONTROL_WILDCARD = "batt-emu/mqtt-v1/rx/cmd/control/#";

/// Stream control (subscribe/unsubscribe to event-log streaming)
/// Payload: { request_id, stream, action: "subscribe"|"unsubscribe", ts_ms }
inline constexpr std::string_view CMD_STREAM_EVENT_LOGS = "batt-emu/mqtt-v1/rx/cmd/stream/event_logs";

/// Generic stream pattern (subscribe for all streams)
inline constexpr std::string_view CMD_STREAM_WILDCARD = "batt-emu/mqtt-v1/rx/cmd/stream/#";

/// Demand refresh for cache-miss recovery (QoS1)
/// Payload: { request_id, model, ts_ms }
inline constexpr std::string_view CMD_REFRESH_BATTERY = "batt-emu/mqtt-v1/rx/cmd/refresh/battery";
inline constexpr std::string_view CMD_REFRESH_POWER = "batt-emu/mqtt-v1/rx/cmd/refresh/power";
inline constexpr std::string_view CMD_REFRESH_NETWORK = "batt-emu/mqtt-v1/rx/cmd/refresh/network";
inline constexpr std::string_view CMD_REFRESH_MQTT = "batt-emu/mqtt-v1/rx/cmd/refresh/mqtt";
inline constexpr std::string_view CMD_REFRESH_LED = "batt-emu/mqtt-v1/rx/cmd/refresh/led";
inline constexpr std::string_view CMD_REFRESH_CATALOG_BATTERY = "batt-emu/mqtt-v1/rx/cmd/refresh/catalog_battery";
inline constexpr std::string_view CMD_REFRESH_CATALOG_INVERTER = "batt-emu/mqtt-v1/rx/cmd/refresh/catalog_inverter";

/// Generic refresh pattern (subscribe for all refresh requests)
inline constexpr std::string_view CMD_REFRESH_WILDCARD = "batt-emu/mqtt-v1/rx/cmd/refresh/#";

/// Generic receiver command pattern (subscribe for all commands)
inline constexpr std::string_view CMD_WILDCARD = "batt-emu/mqtt-v1/rx/cmd/#";

// --- Metadata Topics ---

/// Receiver firmware version and identity (QoS1, optional retained on connect)
/// Payload: { version, build_date, receiver_model, capabilities, ... }
inline constexpr std::string_view META_VERSION = "batt-emu/mqtt-v1/rx/meta/version";

// --- State Topics ---

/// Receiver online/offline presence (LWT-backed, optional)
inline constexpr std::string_view STATE_PRESENCE = "batt-emu/mqtt-v1/rx/state/presence";

/// Pending update buffer (Pattern B for large updates)
/// Payload: temporary update data before being published as command
inline constexpr std::string_view STATE_PENDING_PATTERN = "batt-emu/mqtt-v1/rx/state/pending/{model}/{request_id}";

} // namespace rx

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/// Get wildcard subscriptions recommended for receiver
inline constexpr std::array<std::string_view, 4> receiver_subscriptions() noexcept {
  return {
    tx::STATE_WILDCARD,
    tx::META_WILDCARD,
    tx::ACK_WILDCARD,
    // Note: rx subscribes to tx/* for state updates
  };
}

/// Get wildcard subscriptions recommended for transmitter
inline constexpr std::array<std::string_view, 1> transmitter_subscriptions() noexcept {
  return {
    rx::CMD_WILDCARD,  // All receiver commands
  };
}

} // namespace topics
} // namespace mqtt

#endif // MQTT_TOPICS_COMMON_H
