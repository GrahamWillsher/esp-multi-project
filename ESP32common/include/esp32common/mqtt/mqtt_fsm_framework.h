#ifndef MQTT_FSM_FRAMEWORK_H
#define MQTT_FSM_FRAMEWORK_H

/**
 * @file mqtt_fsm_framework.h
 * @brief Base FSM framework for MQTT connection state management
 * 
 * Provides a reusable FSM base class that transmitter and receiver can
 * instantiate as their own state machine. This allows shared state transition
 * logic while keeping per-device state separate.
 * 
 * The FSM gates:
 * - MQTT publish/subscribe based on state
 * - Non-minimal telemetry transmission (enabled only in BROKER_UP)
 * - Command/ACK correlation (enabled only in BROKER_UP)
 * - Network watchdog and reconnection backoff
 */

#include "mqtt_fsm_state.h"
#include <cstdint>
#include <functional>
#include <chrono>

namespace mqtt {
namespace fsm {

/**
 * @class MqttFsmBase
 * @brief Base state machine for MQTT connection lifecycle
 * 
 * Subclasses (one per device type: transmitter, receiver) override
 * virtual methods to provide device-specific actions on state transitions.
 * 
 * Thread-safety: Not thread-safe; call all methods from a single task context.
 */
class MqttFsmBase {
public:
  using Clock = std::chrono::system_clock;
  using TimePoint = Clock::time_point;
  using Duration = Clock::duration;

  /**
   * @brief Construct FSM in DISABLED state
   */
  MqttFsmBase() noexcept
    : current_state_(MqttFsmState::DISABLED),
      last_state_(MqttFsmState::DISABLED),
      last_transition_time_(Clock::now()),
      backoff_deadline_(TimePoint::min()) {}

  virtual ~MqttFsmBase() = default;

  // ========================================================================
  // STATE TRANSITIONS (call from FSM task)
  // ========================================================================

  /**
   * @brief Notify FSM that network is now available
   * 
   * Triggers transition: DISABLED/NET_DOWN → BROKER_CONNECTING (if enabled)
   * or remains NET_DOWN if waiting for broker availability.
   */
  void on_network_up() noexcept;

  /**
   * @brief Notify FSM that network is down
   * 
   * Triggers transition: any → NET_DOWN
   */
  void on_network_down() noexcept;

  /**
   * @brief Notify FSM that broker connection succeeded
   * 
   * Triggers transition: BROKER_CONNECTING → BROKER_UP
   */
  void on_broker_connected() noexcept;

  /**
   * @brief Notify FSM that broker connection failed
   * 
   * Triggers transition: BROKER_CONNECTING → NET_DOWN or DEGRADED
   * (depending on context and backoff state)
   */
  void on_broker_connection_failed() noexcept;

  /**
   * @brief Notify FSM that broker disconnected (connection lost)
   * 
   * Triggers transition: BROKER_UP → DEGRADED (with backoff)
   */
  void on_broker_disconnected() noexcept;

  /**
   * @brief Enable MQTT FSM operation
   * 
   * Transitions from DISABLED to NET_DOWN (waiting for network).
   */
  void enable() noexcept;

  /**
   * @brief Disable MQTT FSM operation
   * 
   * Transitions any state → DISABLED (shutdown).
   */
  void disable() noexcept;

  // ========================================================================
  // STATE QUERY
  // ========================================================================

  /**
   * @brief Get current FSM state
   */
  MqttFsmState current_state() const noexcept { return current_state_; }

  /**
   * @brief Get previous FSM state (for debugging/logging)
   */
  MqttFsmState last_state() const noexcept { return last_state_; }

  /**
   * @brief Check if non-minimal telemetry should be transmitted
   * 
   * @return true if state == BROKER_UP, false otherwise
   */
  bool should_publish_full_telemetry() const noexcept {
    return allows_full_telemetry(current_state_);
  }

  /**
   * @brief Check if commands/ACKs should be processed
   * 
   * @return true if state == BROKER_UP, false otherwise
   */
  bool should_process_commands() const noexcept {
    return allows_commands(current_state_);
  }

  /**
   * @brief Get time since last state transition
   */
  Duration time_in_current_state() const noexcept {
    return Clock::now() - last_transition_time_;
  }

  /**
   * @brief Check if in BROKER_UP state
   */
  bool is_broker_connected() const noexcept {
    return current_state_ == MqttFsmState::BROKER_UP;
  }

  /**
   * @brief Check if in a healthy network state (not NET_DOWN or DISABLED)
   */
  bool is_network_healthy() const noexcept {
    return network_available(current_state_);
  }

  // ========================================================================
  // VIRTUAL HOOKS (override in subclasses)
  // ========================================================================

  /**
   * @brief Called when FSM enters a new state
   * 
   * Subclasses should override to perform device-specific actions
   * (e.g., start/stop tasks, update UI, log metrics).
   * 
   * @param old_state Previous state
   * @param new_state New state
   */
  virtual void on_state_transition(MqttFsmState old_state, MqttFsmState new_state) noexcept {
    (void)old_state;
    (void)new_state;
  }

  /**
   * @brief Called when FSM determines it should reconnect to broker
   * 
   * Subclasses should override to trigger MQTT client reconnection attempt.
   * FSM will track success/failure via on_broker_connected() / on_broker_connection_failed().
   */
  virtual void on_should_reconnect() noexcept {}

  /**
   * @brief Called when a state transition was blocked or deferred
   * 
   * Useful for diagnostics/logging.
   * 
   * @param requested_state State transition that was deferred
   * @param reason Human-readable reason (e.g., "backoff_in_progress")
   */
  virtual void on_transition_deferred(MqttFsmState requested_state, std::string_view reason) noexcept {
    (void)requested_state;
    (void)reason;
  }

protected:
  /**
   * @brief Update current state and invoke transition hook
   * 
   * Protected helper for transition methods.
   */
  void set_state_(MqttFsmState new_state) noexcept;

private:
  MqttFsmState current_state_;
  MqttFsmState last_state_;
  TimePoint last_transition_time_;
  TimePoint backoff_deadline_;
};

} // namespace fsm
} // namespace mqtt

#endif // MQTT_FSM_FRAMEWORK_H
