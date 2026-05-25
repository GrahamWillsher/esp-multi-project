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

#include <array>

namespace mqtt {
namespace topics {

// ============================================================================
// NAMESPACE ROOT
// ============================================================================

/// Primary namespace root: battery-emulator MQTT v1
static constexpr const char* NAMESPACE_ROOT = "batt-emu/mqtt-v1";

// ============================================================================
// TRANSMITTER PUBLISH TOPICS (TX → RX)
// ============================================================================

namespace tx {

// --- State/Telemetry Topics ---

/// Transmitter heartbeat (QoS0, non-retained)
/// Payload: { ts_ms, status, mqtt_connected, ethernet_connected }
static constexpr const char* STATE_HEARTBEAT = "batt-emu/mqtt-v1/tx/state/heartbeat";

/// Live battery telemetry (QoS0, non-retained, periodic)
/// Payload: { ts_ms, soc, voltage, current, temperature, ... }
static constexpr const char* STATE_BATTERY_LIVE = "batt-emu/mqtt-v1/tx/state/battery_live";

/// Static battery configuration (QoS1, retained)
/// Payload: full battery model snapshot (chemistry, capacity, cells, etc.)
static constexpr const char* STATE_STATIC_BATTERY = "batt-emu/mqtt-v1/tx/state/static/battery";

/// Static power profile configuration (QoS1, retained)
/// Payload: { rated_power, profiles[], mode, ... }
static constexpr const char* STATE_STATIC_POWER = "batt-emu/mqtt-v1/tx/state/static/power";

/// Static inverter configuration (QoS1, retained)
static constexpr const char* STATE_STATIC_INVERTER = "batt-emu/mqtt-v1/tx/state/static/inverter";

/// Static network configuration (QoS1, retained)
/// Payload: { hostname, mac, ip_mode, dns, ... }
static constexpr const char* STATE_STATIC_NETWORK = "batt-emu/mqtt-v1/tx/state/static/network";

/// Static MQTT configuration (QoS1, retained)
/// Payload: { broker_host, port, user (redacted in logs), qos_policy, ... }
static constexpr const char* STATE_STATIC_MQTT = "batt-emu/mqtt-v1/tx/state/static/mqtt";

/// Static LED state/configuration (QoS1, retained)
/// Payload: { current_state, brightness, mode, ... }
static constexpr const char* STATE_STATIC_LED = "batt-emu/mqtt-v1/tx/state/static/led";

/// Static unified settings snapshot (QoS1, retained)
/// Payload: { battery, battery_emulator, power, can, contactor }
static constexpr const char* STATE_STATIC_SETTINGS = "batt-emu/mqtt-v1/tx/state/static/settings";

/// Battery type catalog (QoS1, retained)
/// Payload: array of { id, name, chemistry, capacity, ... }
static constexpr const char* STATE_STATIC_CATALOG_BATTERY = "batt-emu/mqtt-v1/tx/state/static/catalog_battery";

/// Inverter type catalog (QoS1, retained)
static constexpr const char* STATE_STATIC_CATALOG_INVERTER = "batt-emu/mqtt-v1/tx/state/static/catalog_inverter";

/// Event-log summary (QoS1, non-retained)
/// Payload: { total_count, warning_count, error_count, last_ts, ... }
static constexpr const char* STATE_SUMMARY_EVENT_LOGS = "batt-emu/mqtt-v1/tx/state/summary/event_logs";

/// Event-log data chunk (QoS1, non-retained)
/// Payload: { chunk_id, part_num, total_parts, data_base64, ... }
static constexpr const char* STATE_EVENT_LOGS_CHUNK = "batt-emu/mqtt-v1/tx/state/event_logs/chunk";

/// Cell voltage/temperature data chunk (QoS1, non-retained)
/// Payload: { chunk_id, part_num, total_parts, data_base64, ... }
static constexpr const char* STATE_CELL_DATA_CHUNK = "batt-emu/mqtt-v1/tx/state/cell_data/chunk";

/// Live LED runtime state (QoS0, non-retained)
static constexpr const char* STATE_RUNTIME_LED = "batt-emu/mqtt-v1/tx/state/runtime/led";

/// Live system runtime state (QoS0, non-retained)
static constexpr const char* STATE_RUNTIME_SYSTEM = "batt-emu/mqtt-v1/tx/state/runtime/system";

/// Live charger runtime state (QoS0, non-retained)
static constexpr const char* STATE_RUNTIME_CHARGER = "batt-emu/mqtt-v1/tx/state/runtime/charger";

/// Live inverter runtime state (QoS0, non-retained)
static constexpr const char* STATE_RUNTIME_INVERTER = "batt-emu/mqtt-v1/tx/state/runtime/inverter";

// --- Metadata Topics ---

/// Transmitter firmware version and compatibility info (QoS1, retained)
/// Payload: { firmware_version, build_date, proto_version, capabilities, ... }
static constexpr const char* META_VERSION = "batt-emu/mqtt-v1/tx/meta/version";

/// Per-model schema versions (QoS1, retained)
/// Payload: { battery: N, power: N, network: N, mqtt: N, led: N, ... }
static constexpr const char* META_SCHEMA_VERSIONS = "batt-emu/mqtt-v1/tx/meta/schema_versions";

/// Transmitter runtime state (QoS1, retained)
/// Payload: { uptime_ms, mqtt_connected, ethernet_connected, task_heap_free, ... }
static constexpr const char* META_RUNTIME = "batt-emu/mqtt-v1/tx/meta/runtime";

// --- ACK/Result Topics ---

/// Generic ACK for battery configuration update (QoS1, non-retained)
/// Payload: { request_id, status, message, applied_version, ... }
static constexpr const char* ACK_BATTERY = "batt-emu/mqtt-v1/tx/ack/battery";

/// ACK for power configuration update
static constexpr const char* ACK_POWER = "batt-emu/mqtt-v1/tx/ack/power";

/// ACK for network configuration update
static constexpr const char* ACK_NETWORK = "batt-emu/mqtt-v1/tx/ack/network";

/// ACK for MQTT configuration update
static constexpr const char* ACK_MQTT = "batt-emu/mqtt-v1/tx/ack/mqtt";

/// ACK for inverter configuration update
static constexpr const char* ACK_INVERTER = "batt-emu/mqtt-v1/tx/ack/inverter";

/// ACK for CAN configuration update
static constexpr const char* ACK_CAN = "batt-emu/mqtt-v1/tx/ack/can";

/// ACK for contactor control
static constexpr const char* ACK_CONTACTOR = "batt-emu/mqtt-v1/tx/ack/contactor";

/// ACK for control/command (reboot, debug_level, etc.)
static constexpr const char* ACK_CONTROL = "batt-emu/mqtt-v1/tx/ack/control";

/// ACK for component application
static constexpr const char* ACK_COMPONENT = "batt-emu/mqtt-v1/tx/ack/component";

/// ACK for event-log clear operation
static constexpr const char* ACK_EVENT_LOGS_CLEAR = "batt-emu/mqtt-v1/tx/ack/event_logs_clear";

/// ACK for data refresh request
static constexpr const char* ACK_REFRESH = "batt-emu/mqtt-v1/tx/ack/refresh";

/// Wildcard for all transmitter ACKs (subscribe pattern)
static constexpr const char* ACK_WILDCARD = "batt-emu/mqtt-v1/tx/ack/#";

/// Wildcard for all transmitter state topics (subscribe pattern)
static constexpr const char* STATE_WILDCARD = "batt-emu/mqtt-v1/tx/state/#";

/// Wildcard for all transmitter metadata topics (subscribe pattern)
static constexpr const char* META_WILDCARD = "batt-emu/mqtt-v1/tx/meta/#";

} // namespace tx

// ============================================================================
// RECEIVER PUBLISH TOPICS (RX → TX)
// ============================================================================

namespace rx {

// --- Command Topics ---

/// Battery configuration update command (QoS1)
/// Payload: { request_id, base_version, fields: { ... }, ts_ms }
static constexpr const char* CMD_UPDATE_BATTERY = "batt-emu/mqtt-v1/rx/cmd/update/battery";

/// Power configuration update command
static constexpr const char* CMD_UPDATE_POWER = "batt-emu/mqtt-v1/rx/cmd/update/power";

/// Network configuration update command
static constexpr const char* CMD_UPDATE_NETWORK = "batt-emu/mqtt-v1/rx/cmd/update/network";

/// MQTT configuration update command
static constexpr const char* CMD_UPDATE_MQTT = "batt-emu/mqtt-v1/rx/cmd/update/mqtt";

/// Inverter configuration update command
static constexpr const char* CMD_UPDATE_INVERTER = "batt-emu/mqtt-v1/rx/cmd/update/inverter";

/// CAN configuration update command
static constexpr const char* CMD_UPDATE_CAN = "batt-emu/mqtt-v1/rx/cmd/update/can";

/// Contactor control command
static constexpr const char* CMD_UPDATE_CONTACTOR = "batt-emu/mqtt-v1/rx/cmd/update/contactor";

/// Generic update pattern (subscribe for all models)
static constexpr const char* CMD_UPDATE_WILDCARD = "batt-emu/mqtt-v1/rx/cmd/update/#";

/// Control/Action commands (reboot, debug_level, etc.)
/// Payload: { request_id, action, params, ts_ms }
static constexpr const char* CMD_CONTROL_DEBUG_LEVEL = "batt-emu/mqtt-v1/rx/cmd/control/debug_level";
static constexpr const char* CMD_CONTROL_TEST_DATA_MODE = "batt-emu/mqtt-v1/rx/cmd/control/test_data_mode";
static constexpr const char* CMD_CONTROL_REBOOT = "batt-emu/mqtt-v1/rx/cmd/control/reboot";
static constexpr const char* CMD_CONTROL_OTA_START = "batt-emu/mqtt-v1/rx/cmd/control/ota_start";
static constexpr const char* CMD_CONTROL_COMPONENT_APPLY = "batt-emu/mqtt-v1/rx/cmd/control/component_apply";
static constexpr const char* CMD_CONTROL_EVENT_LOGS_CLEAR = "batt-emu/mqtt-v1/rx/cmd/control/event_logs_clear";

/// Generic control pattern (subscribe for all actions)
static constexpr const char* CMD_CONTROL_WILDCARD = "batt-emu/mqtt-v1/rx/cmd/control/#";

/// Stream control (subscribe/unsubscribe to event-log streaming)
/// Payload: { request_id, stream, action: "subscribe"|"unsubscribe", ts_ms }
static constexpr const char* CMD_STREAM_EVENT_LOGS = "batt-emu/mqtt-v1/rx/cmd/stream/event_logs";

/// Stream control for cell-data snapshots/chunks
static constexpr const char* CMD_STREAM_CELL_DATA = "batt-emu/mqtt-v1/rx/cmd/stream/cell_data";

/// Generic stream pattern (subscribe for all streams)
static constexpr const char* CMD_STREAM_WILDCARD = "batt-emu/mqtt-v1/rx/cmd/stream/#";

/// Demand refresh for cache-miss recovery (QoS1)
/// Payload: { request_id, model, ts_ms }
static constexpr const char* CMD_REFRESH_BATTERY = "batt-emu/mqtt-v1/rx/cmd/refresh/battery";
static constexpr const char* CMD_REFRESH_POWER = "batt-emu/mqtt-v1/rx/cmd/refresh/power";
static constexpr const char* CMD_REFRESH_NETWORK = "batt-emu/mqtt-v1/rx/cmd/refresh/network";
static constexpr const char* CMD_REFRESH_MQTT = "batt-emu/mqtt-v1/rx/cmd/refresh/mqtt";
static constexpr const char* CMD_REFRESH_SETTINGS = "batt-emu/mqtt-v1/rx/cmd/refresh/settings";
static constexpr const char* CMD_REFRESH_LED = "batt-emu/mqtt-v1/rx/cmd/refresh/led";
static constexpr const char* CMD_REFRESH_CATALOG_BATTERY = "batt-emu/mqtt-v1/rx/cmd/refresh/catalog_battery";
static constexpr const char* CMD_REFRESH_CATALOG_INVERTER = "batt-emu/mqtt-v1/rx/cmd/refresh/catalog_inverter";

/// Generic refresh pattern (subscribe for all refresh requests)
static constexpr const char* CMD_REFRESH_WILDCARD = "batt-emu/mqtt-v1/rx/cmd/refresh/#";

/// Generic receiver command pattern (subscribe for all commands)
static constexpr const char* CMD_WILDCARD = "batt-emu/mqtt-v1/rx/cmd/#";

// --- Metadata Topics ---

/// Receiver firmware version and identity (QoS1, optional retained on connect)
/// Payload: { version, build_date, receiver_model, capabilities, ... }
static constexpr const char* META_VERSION = "batt-emu/mqtt-v1/rx/meta/version";

// --- State Topics ---

/// Receiver online/offline presence (LWT-backed, optional)
static constexpr const char* STATE_PRESENCE = "batt-emu/mqtt-v1/rx/state/presence";

/// Pending update buffer (Pattern B for large updates)
/// Payload: temporary update data before being published as command
static constexpr const char* STATE_PENDING_PATTERN = "batt-emu/mqtt-v1/rx/state/pending/{model}/{request_id}";

} // namespace rx

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/// Get wildcard subscriptions recommended for receiver
inline const std::array<const char*, 3>& receiver_subscriptions() noexcept {
  static const std::array<const char*, 3> subscriptions = {{
    tx::STATE_WILDCARD,
    tx::META_WILDCARD,
    tx::ACK_WILDCARD,
  }};
  return subscriptions;
}

/// Get wildcard subscriptions recommended for transmitter
inline const std::array<const char*, 1>& transmitter_subscriptions() noexcept {
  static const std::array<const char*, 1> subscriptions = {{
    rx::CMD_WILDCARD,
  }};
  return subscriptions;
}

} // namespace topics
} // namespace mqtt

#endif // MQTT_TOPICS_COMMON_H
