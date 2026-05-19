#include "radio_pressure_state.h"

#include "rx_radio_arbiter_fsm.h"
#include "espnow_tx_scheduler.h"

// Build-flag-tuneable threshold.  A value of 5 means: after 5 consecutive
// fully-retried ESP_ERR_ESPNOW_NO_MEM failures (no send callback ever fired)
// the system is classified CRITICAL.  Lower this if you want earlier gating;
// raise it if transient LMAC contention is expected in your RF environment.
#ifndef RADIO_PRESSURE_CRITICAL_NO_MEM_THRESHOLD
#define RADIO_PRESSURE_CRITICAL_NO_MEM_THRESHOLD 5U
#endif

namespace {
constexpr uint32_t kCriticalNoMemThreshold = RADIO_PRESSURE_CRITICAL_NO_MEM_THRESHOLD;
}  // namespace

RadioPressureState get_radio_pressure_state() {
    const RxRadioArbiterFsm::State fsm_state = RxRadioArbiterFsm::instance().state();
    const uint32_t no_mem = EspnowTxScheduler::get_consecutive_no_mem_count();

    // CRITICAL: the FSM has fallen to its lowest recovery level, or the
    // LMAC TX buffer has been repeatedly exhausted with no recovery.
    if (fsm_state == RxRadioArbiterFsm::State::DEGRADED_FALLBACK ||
        no_mem >= kCriticalNoMemThreshold) {
        return RadioPressureState::CRITICAL;
    }

    // NORMAL: fully settled connection with no active NO_MEM pressure.
    if (fsm_state == RxRadioArbiterFsm::State::STEADY_CONNECTED && no_mem == 0) {
        return RadioPressureState::NORMAL;
    }

    // CONSTRAINED: any other state (BOOTSTRAP, RECONNECT_DETECTED,
    // ACK_RECOVERY_WINDOW, POST_RECONNECT_SETTLE) or low-level NO_MEM.
    return RadioPressureState::CONSTRAINED;
}

const char* radio_pressure_state_to_string(RadioPressureState state) {
    switch (state) {
        case RadioPressureState::NORMAL:      return "normal";
        case RadioPressureState::CONSTRAINED: return "constrained";
        case RadioPressureState::CRITICAL:    return "critical";
        default:                              return "unknown";
    }
}
