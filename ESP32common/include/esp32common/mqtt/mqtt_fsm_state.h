#ifndef MQTT_FSM_STATE_H
#define MQTT_FSM_STATE_H

/**
 * @file mqtt_fsm_state.h
 * @brief MQTT connection finite state machine state definitions
 * 
 * Defines the state enum and utility functions for MQTT FSM gating.
 * Each device (transmitter and receiver) maintains its own FSM instance,
 * but they all share this common state definition and framework.
 */

#include <cstdint>
#include <string_view>

namespace mqtt {
namespace fsm {

/**
 * @enum MqttFsmState
 * @brief MQTT connection and publication state machine states
 * 
 * Progression typical states:
 *   DISABLED → NET_DOWN → BROKER_CONNECTING → BROKER_UP
 *   ↑                                               ↓
 *   └─────────────── DEGRADED ←──────────────────┘
 * 
 * Non-minimal MQTT data is gated by state >= BROKER_UP.
 * Minimal data (heartbeat, retained snapshots) proceeds regardless of state.
 */
enum class MqttFsmState : uint8_t {
  /**
   * MQTT is disabled at runtime (feature flag off or explicit shutdown).
   * No MQTT publishing or subscribing occurs.
   * Local MQTT task may still run but does nothing.
   */
  DISABLED = 0,

  /**
   * Network (Ethernet or WiFi) is not available or not ready.
   * MQTT broker connection is not attempted.
   */
  NET_DOWN = 1,

  /**
   * Network is up, but broker connection is in progress.
   * Subscribe/publish operations wait; retained topics may be served
   * from local cache if previous session succeeded.
   */
  BROKER_CONNECTING = 2,

  /**
   * Broker is connected and subscriptions are active.
   * Normal pub/sub operations, ACK correlation, demand refresh all active.
   * Non-minimal telemetry is gated by this state or higher.
   */
  BROKER_UP = 3,

  /**
   * Broker was previously UP but has become temporarily unreachable.
   * Backoff timer is active; connection retry in progress.
   * Non-minimal telemetry is NOT published; local snapshot retained.
   * Command handler still accepts new requests (will be retried on reconnect).
   */
  DEGRADED = 4,
};

/// String representation for logging/debug
inline std::string_view to_string(MqttFsmState state) noexcept {
  switch (state) {
    case MqttFsmState::DISABLED:           return "DISABLED";
    case MqttFsmState::NET_DOWN:           return "NET_DOWN";
    case MqttFsmState::BROKER_CONNECTING:  return "BROKER_CONNECTING";
    case MqttFsmState::BROKER_UP:          return "BROKER_UP";
    case MqttFsmState::DEGRADED:           return "DEGRADED";
    default:                               return "UNKNOWN";
  }
}

/**
 * @brief Check if a state allows normal (non-minimal) MQTT telemetry transmission
 * 
 * Only BROKER_UP state permits non-minimal telemetry (detailed live data).
 * All other states retain snapshot-only mode.
 * 
 * @param state Current FSM state
 * @return true if state permits non-minimal telemetry, false otherwise
 */
inline bool allows_full_telemetry(MqttFsmState state) noexcept {
  return state == MqttFsmState::BROKER_UP;
}

/**
 * @brief Check if a state permits command/ACK correlation operations
 * 
 * Command subscriptions and ACK replies occur in BROKER_UP state only.
 * During DEGRADED, new commands are queued but not processed.
 * 
 * @param state Current FSM state
 * @return true if state permits command operations
 */
inline bool allows_commands(MqttFsmState state) noexcept {
  return state == MqttFsmState::BROKER_UP;
}

/**
 * @brief Check if network is available (precondition for broker connection)
 * 
 * Network must be UP before BROKER_CONNECTING can be attempted.
 * 
 * @param state Current FSM state
 * @return true if network layer is assumed to be available
 */
inline bool network_available(MqttFsmState state) noexcept {
  return state != MqttFsmState::NET_DOWN && state != MqttFsmState::DISABLED;
}

} // namespace fsm
} // namespace mqtt

#endif // MQTT_FSM_STATE_H
