#ifndef MQTT_TOPICS_TRANSMITTER_H
#define MQTT_TOPICS_TRANSMITTER_H

#include <stdint.h>
#include <string>

/**
 * @brief MQTT topic contracts for transmitter device
 * 
 * Defines all topics the transmitter publishes and subscribes to,
 * with QoS and retention policies per the MQTT-only architecture.
 * 
 * Topic namespace: batt-emu/mqtt-v1/{rx,tx}/*
 */
namespace MqttTopicsTransmitter {

// ============================================================================
// TRANSMITTER PUBLICATIONS (OUTBOUND)
// ============================================================================

/**
 * Transmitter state/telemetry topics (transmitter publishes)
 * QoS: 0 (heartbeat live data, lossy acceptable)
 * Retain: false
 */
namespace TxState {
  constexpr const char* HEARTBEAT          = "batt-emu/mqtt-v1/tx/state/heartbeat";
  constexpr const char* BATTERY_LIVE       = "batt-emu/mqtt-v1/tx/state/battery_live";
  constexpr const char* SUMMARY_EVENT_LOGS = "batt-emu/mqtt-v1/tx/state/summary/event_logs";
  constexpr const char* EVENT_LOGS_CHUNK   = "batt-emu/mqtt-v1/tx/state/event_logs/chunk";
  constexpr const char* CELL_DATA_CHUNK    = "batt-emu/mqtt-v1/tx/state/cell_data/chunk";
}

namespace TxRuntime {
  constexpr const char* LED    = "batt-emu/mqtt-v1/tx/state/runtime/led";
  constexpr const char* SYSTEM = "batt-emu/mqtt-v1/tx/state/runtime/system";
  constexpr const char* CHARGER = "batt-emu/mqtt-v1/tx/state/runtime/charger";
  constexpr const char* INVERTER = "batt-emu/mqtt-v1/tx/state/runtime/inverter";
}

/**
 * Transmitter static/config topics (transmitter publishes, retained)
 * QoS: 1
 * Retain: true
 * Replayed to receiver on subscription/reconnect
 */
namespace TxStatic {
  constexpr const char* BATTERY      = "batt-emu/mqtt-v1/tx/state/static/battery";
  constexpr const char* POWER        = "batt-emu/mqtt-v1/tx/state/static/power";
  constexpr const char* INVERTER     = "batt-emu/mqtt-v1/tx/state/static/inverter";
  constexpr const char* NETWORK      = "batt-emu/mqtt-v1/tx/state/static/network";
  constexpr const char* MQTT         = "batt-emu/mqtt-v1/tx/state/static/mqtt";
  constexpr const char* LED          = "batt-emu/mqtt-v1/tx/state/static/led";
  constexpr const char* CATALOG_BATTERY = "batt-emu/mqtt-v1/tx/state/static/catalog_battery";
  constexpr const char* CATALOG_INVERTER = "batt-emu/mqtt-v1/tx/state/static/catalog_inverter";
}

/**
 * Transmitter metadata topics (transmitter publishes, retained)
 * QoS: 1
 * Retain: true
 * Used for receiver discovery and version/compatibility checking
 */
namespace TxMeta {
  constexpr const char* VERSION         = "batt-emu/mqtt-v1/tx/meta/version";
  constexpr const char* SCHEMA_VERSIONS = "batt-emu/mqtt-v1/tx/meta/schema_versions";
  constexpr const char* RUNTIME         = "batt-emu/mqtt-v1/tx/meta/runtime";
}

/**
 * Transmitter ACK/result topics (transmitter publishes)
 * QoS: 1 (command responses must not be lost)
 * Retain: false
 * Carries request_id for correlation with originating command
 */
namespace TxAck {
  constexpr const char* PREFIX         = "batt-emu/mqtt-v1/tx/ack";
  constexpr const char* SETTINGS       = "batt-emu/mqtt-v1/tx/ack/settings";
  constexpr const char* NETWORK        = "batt-emu/mqtt-v1/tx/ack/network";
  constexpr const char* MQTT_CONFIG    = "batt-emu/mqtt-v1/tx/ack/mqtt";
  constexpr const char* CONTROL        = "batt-emu/mqtt-v1/tx/ack/control";
  constexpr const char* EVENT_LOGS     = "batt-emu/mqtt-v1/tx/ack/event_logs_clear";
}

// ============================================================================
// TRANSMITTER SUBSCRIPTIONS (INBOUND COMMANDS)
// ============================================================================

/**
 * Receiver command topics (transmitter subscribes)
 * QoS: 1
 * Retain: false
 * Pattern: batt-emu/mqtt-v1/rx/cmd/{category}/{action}
 */
namespace RxCmd {
  constexpr const char* PREFIX = "batt-emu/mqtt-v1/rx/cmd";
  
  // Update commands (set values)
  constexpr const char* UPDATE_BATTERY    = "batt-emu/mqtt-v1/rx/cmd/update/battery";
  constexpr const char* UPDATE_POWER      = "batt-emu/mqtt-v1/rx/cmd/update/power";
  constexpr const char* UPDATE_INVERTER   = "batt-emu/mqtt-v1/rx/cmd/update/inverter";
  constexpr const char* UPDATE_CAN        = "batt-emu/mqtt-v1/rx/cmd/update/can";
  constexpr const char* UPDATE_CONTACTOR  = "batt-emu/mqtt-v1/rx/cmd/update/contactor";
  constexpr const char* UPDATE_NETWORK    = "batt-emu/mqtt-v1/rx/cmd/update/network";
  constexpr const char* UPDATE_MQTT       = "batt-emu/mqtt-v1/rx/cmd/update/mqtt";
  
  // Control commands (actions)
  constexpr const char* CONTROL_DEBUG_LEVEL       = "batt-emu/mqtt-v1/rx/cmd/control/debug_level";
  constexpr const char* CONTROL_TEST_DATA_MODE    = "batt-emu/mqtt-v1/rx/cmd/control/test_data_mode";
  constexpr const char* CONTROL_REBOOT            = "batt-emu/mqtt-v1/rx/cmd/control/reboot";
  constexpr const char* CONTROL_OTA_START         = "batt-emu/mqtt-v1/rx/cmd/control/ota_start";
  constexpr const char* CONTROL_COMPONENT_APPLY   = "batt-emu/mqtt-v1/rx/cmd/control/component_apply";
  constexpr const char* CONTROL_EVENT_LOGS_CLEAR  = "batt-emu/mqtt-v1/rx/cmd/control/event_logs_clear";
  
  // Stream control
  constexpr const char* STREAM_EVENT_LOGS = "batt-emu/mqtt-v1/rx/cmd/stream/event_logs";
  
  // Refresh requests
  constexpr const char* REFRESH_BATTERY           = "batt-emu/mqtt-v1/rx/cmd/refresh/battery";
  constexpr const char* REFRESH_POWER             = "batt-emu/mqtt-v1/rx/cmd/refresh/power";
  constexpr const char* REFRESH_NETWORK           = "batt-emu/mqtt-v1/rx/cmd/refresh/network";
  constexpr const char* REFRESH_MQTT              = "batt-emu/mqtt-v1/rx/cmd/refresh/mqtt";
  constexpr const char* REFRESH_CATALOG_BATTERY   = "batt-emu/mqtt-v1/rx/cmd/refresh/catalog_battery";
  constexpr const char* REFRESH_CATALOG_INVERTER  = "batt-emu/mqtt-v1/rx/cmd/refresh/catalog_inverter";
  constexpr const char* REFRESH_LED               = "batt-emu/mqtt-v1/rx/cmd/refresh/led";
}

/**
 * Receiver metadata topics (transmitter may subscribe for presence detection)
 * QoS: 1
 * Retain: true
 */
namespace RxMeta {
  constexpr const char* VERSION   = "batt-emu/mqtt-v1/rx/meta/version";
  constexpr const char* PRESENCE  = "batt-emu/mqtt-v1/rx/state/presence";
}

// ============================================================================
// COMMAND MESSAGE SCHEMA KEYS (used in JSON payloads)
// ============================================================================

namespace CommandKeys {
  // Common fields in all commands
  constexpr const char* REQUEST_ID   = "request_id";
  constexpr const char* TIMESTAMP_MS = "ts_ms";
  constexpr const char* SCHEMA_VERSION = "schema_version";
  constexpr const char* ORIGIN       = "origin";
  
  // Settings command fields
  constexpr const char* CATEGORY = "category";
  constexpr const char* FIELD    = "field";
  constexpr const char* VALUE    = "value";
  constexpr const char* BASE_VERSION = "base_version";
  
  // Network config fields
  constexpr const char* USE_STATIC_IP = "use_static_ip";
  constexpr const char* IP_ADDRESS    = "ip_address";
  constexpr const char* GATEWAY       = "gateway";
  constexpr const char* SUBNET_MASK   = "subnet_mask";
  constexpr const char* DNS_PRIMARY   = "dns_primary";
  constexpr const char* DNS_SECONDARY = "dns_secondary";
  
  // MQTT config fields
  constexpr const char* MQTT_ENABLED  = "mqtt_enabled";
  constexpr const char* MQTT_SERVER   = "mqtt_server";
  constexpr const char* MQTT_PORT     = "mqtt_port";
  constexpr const char* MQTT_USERNAME = "mqtt_username";
  constexpr const char* MQTT_PASSWORD = "mqtt_password";
  constexpr const char* MQTT_CLIENT_ID = "mqtt_client_id";
  
  // Control command fields
  constexpr const char* DEBUG_LEVEL = "debug_level";
  constexpr const char* TEST_MODE_ENABLED = "test_mode_enabled";
  constexpr const char* CONFIRM = "confirm";
  constexpr const char* APPLY_MASK = "apply_mask";
  constexpr const char* COMPONENT_TYPES = "component_types";
}

// ============================================================================
// ACK MESSAGE SCHEMA KEYS (used in ACK responses)
// ============================================================================

namespace AckKeys {
  constexpr const char* REQUEST_ID = "request_id";
  constexpr const char* SUCCESS    = "success";
  constexpr const char* CODE       = "code";
  constexpr const char* MESSAGE    = "message";
  constexpr const char* TIMESTAMP_MS = "ts_ms";
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * @brief Get subscription pattern for all receiver command topics
 * @return Wildcard pattern (e.g., "batt-emu/mqtt-v1/rx/cmd/#")
 */
const char* GetRxCmdSubscriptionPattern();

/**
 * @brief Extract command type and action from command topic
 * @param cmd_topic Full topic path
 * @param out_category Output category (e.g., "update", "control", "refresh")
 * @param out_action Output action (e.g., "battery", "network", "reboot")
 * @return true if topic matches command pattern
 */
bool ExtractCommandTopicParts(const std::string& cmd_topic,
                              std::string& out_category,
                              std::string& out_action);

/**
 * @brief Determine which ACK topic to publish based on command category
 * @param category Command category from ExtractCommandTopicParts
 * @return ACK topic to publish response to
 */
const char* GetAckTopicForCategory(const std::string& category);

} // namespace MqttTopicsTransmitter

#endif // MQTT_TOPICS_TRANSMITTER_H
