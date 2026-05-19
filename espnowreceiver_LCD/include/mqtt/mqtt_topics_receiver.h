#ifndef MQTT_TOPICS_RECEIVER_H
#define MQTT_TOPICS_RECEIVER_H

#include <stdint.h>
#include <string>

/**
 * @brief MQTT topic contracts for receiver device
 * 
 * Defines all topics the receiver publishes and subscribes to,
 * with QoS and retention policies per the MQTT-only architecture.
 * 
 * Topic namespace: batt-emu/mqtt-v1/{rx,tx}/*
 */
namespace MqttTopicsReceiver {

// ============================================================================
// TRANSMITTER → RECEIVER (SUBSCRIPTIONS)
// ============================================================================

/**
 * Transmitter state/telemetry topics (receiver subscribes)
 * QoS: 0 (heartbeat live data, lossy is acceptable)
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
 * Transmitter static/config topics (receiver subscribes, retained)
 * QoS: 1 (must-have)
 * Retain: true
 * These are replayed on receiver reconnect/broker reconnect
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
 * Transmitter metadata topics (receiver subscribes, retained)
 * QoS: 1
 * Retain: true
 * Compatibility and runtime information
 */
namespace TxMeta {
  constexpr const char* VERSION         = "batt-emu/mqtt-v1/tx/meta/version";
  constexpr const char* SCHEMA_VERSIONS = "batt-emu/mqtt-v1/tx/meta/schema_versions";
  constexpr const char* RUNTIME         = "batt-emu/mqtt-v1/tx/meta/runtime";
}

/**
 * Transmitter ACK/result topics (receiver subscribes)
 * QoS: 1 (must-have for command/response correlation)
 * Retain: false
 * Used for request_id correlation on command responses
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
// RECEIVER → TRANSMITTER (PUBLICATIONS)
// ============================================================================

/**
 * Receiver metadata topics (receiver publishes)
 * QoS: 1
 * Retain: true (allows transmitter to verify receiver is still active on reconnect)
 */
namespace RxMeta {
  constexpr const char* VERSION   = "batt-emu/mqtt-v1/rx/meta/version";
  constexpr const char* PRESENCE  = "batt-emu/mqtt-v1/rx/state/presence";
}

/**
 * Receiver command topics (receiver publishes to ask transmitter to change config)
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
  
  // Stream control (subscription/unsubscription)
  constexpr const char* STREAM_EVENT_LOGS = "batt-emu/mqtt-v1/rx/cmd/stream/event_logs";
  
  // Refresh requests (ask transmitter to republish)
  constexpr const char* REFRESH_BATTERY           = "batt-emu/mqtt-v1/rx/cmd/refresh/battery";
  constexpr const char* REFRESH_POWER             = "batt-emu/mqtt-v1/rx/cmd/refresh/power";
  constexpr const char* REFRESH_NETWORK           = "batt-emu/mqtt-v1/rx/cmd/refresh/network";
  constexpr const char* REFRESH_MQTT              = "batt-emu/mqtt-v1/rx/cmd/refresh/mqtt";
  constexpr const char* REFRESH_CATALOG_BATTERY   = "batt-emu/mqtt-v1/rx/cmd/refresh/catalog_battery";
  constexpr const char* REFRESH_CATALOG_INVERTER  = "batt-emu/mqtt-v1/rx/cmd/refresh/catalog_inverter";
  constexpr const char* REFRESH_LED               = "batt-emu/mqtt-v1/rx/cmd/refresh/led";
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * @brief Extract request_id and model from ACK topic pattern
 * @param ack_topic Full topic path (e.g., "batt-emu/mqtt-v1/tx/ack/settings")
 * @param out_model Output model name (e.g., "settings", "network", "control")
 * @return true if topic matches ACK pattern and model extracted
 */
bool ExtractAckModel(const std::string& ack_topic, std::string& out_model);

/**
 * @brief Get subscription pattern for all transmitter topics
 * @return Wildcard pattern for broker subscription
 */
const char* GetTxSubscriptionPattern();

/**
 * @brief Get subscription pattern for all transmitter ACK topics
 * @return Wildcard pattern for broker subscription (e.g., "batt-emu/mqtt-v1/tx/ack/#")
 */
const char* GetTxAckSubscriptionPattern();

} // namespace MqttTopicsReceiver

#endif // MQTT_TOPICS_RECEIVER_H
