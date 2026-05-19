#pragma once

#include <cstdint>

/**
 * @file radio_pressure_state.h
 * @brief Radio coexistence pressure level for HTTP / MQTT policy gating.
 *
 * Derived from RxRadioArbiterFsm::state() and
 * EspnowTxScheduler::get_consecutive_no_mem_count().
 *
 * Consumers call get_radio_pressure_state() rather than querying the
 * FSM or scheduler directly.  This keeps the derivation logic in one
 * place and makes policy gates easy to test independently.
 *
 * Threshold tuning:
 *   Build flag: -D RADIO_PRESSURE_CRITICAL_NO_MEM_THRESHOLD=<n>
 *   Default   : 5 consecutive fully-retried NO_MEM failures.
 */

enum class RadioPressureState : uint8_t {
    NORMAL      = 0,  ///< STEADY_CONNECTED; no elevated NO_MEM count.
    CONSTRAINED = 1,  ///< RECONNECT_DETECTED / ACK_RECOVERY_WINDOW / POST_RECONNECT_SETTLE.
    CRITICAL    = 2,  ///< DEGRADED_FALLBACK or consecutive_no_mem_count >= threshold.
};

/**
 * @brief Returns the current radio coexistence pressure level.
 *
 * - NORMAL:      full HTTP and MQTT behavior permitted.
 * - CONSTRAINED: reduce non-essential TX; quiet_mode_active() returns true.
 * - CRITICAL:    block mutating HTTP routes; quiet_mode_active() returns true.
 */
RadioPressureState get_radio_pressure_state();

/// Short string label suitable for response headers and log lines.
const char* radio_pressure_state_to_string(RadioPressureState state);
