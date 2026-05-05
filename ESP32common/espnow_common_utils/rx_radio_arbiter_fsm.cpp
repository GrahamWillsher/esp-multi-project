#include "rx_radio_arbiter_fsm.h"

namespace {

bool time_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return deadline_ms != 0U && now_ms >= deadline_ms;
}

}  // namespace

RxRadioArbiterFsm& RxRadioArbiterFsm::instance() {
    static RxRadioArbiterFsm fsm;
    return fsm;
}

void RxRadioArbiterFsm::init() {
    init(Config{});
}

void RxRadioArbiterFsm::init(const Config& config) {
    config_ = config;
    reset(0U);
}

void RxRadioArbiterFsm::reset(uint32_t now_ms) {
    state_ = State::BOOTSTRAP;
    state_entered_ms_ = now_ms;
    reconnect_deadline_ms_ = 0U;
    settle_deadline_ms_ = 0U;

    stats_ = Stats{};
    stats_.last_state = state_;

    apply_policy_for_state(state_, true);
}

bool RxRadioArbiterFsm::on_event(Event event, uint32_t now_ms) {
    policy_.purge_non_control_on_entry = false;
    stats_.total_events++;
    stats_.last_event = event;

    switch (state_) {
        case State::BOOTSTRAP:
            if (event == Event::EV_HEARTBEAT_FRESH) {
                return transition_to(State::STEADY_CONNECTED, event, now_ms);
            }
            if (event == Event::EV_PROBE_WHILE_PREVIOUSLY_CONNECTED ||
                event == Event::EV_HEARTBEAT_STALE) {
                return transition_to(State::RECONNECT_DETECTED, event, now_ms);
            }
            break;

        case State::STEADY_CONNECTED:
            if (event == Event::EV_PROBE_WHILE_PREVIOUSLY_CONNECTED ||
                event == Event::EV_HEARTBEAT_STALE) {
                return transition_to(State::RECONNECT_DETECTED, event, now_ms);
            }
            break;

        case State::RECONNECT_DETECTED:
            if (event == Event::EV_DISCOVERY_ACK_SENT_OK) {
                return transition_to(State::ACK_RECOVERY_WINDOW, event, now_ms);
            }
            if (event == Event::EV_RECONNECT_TIMEOUT) {
                return transition_to(State::DEGRADED_FALLBACK, event, now_ms);
            }
            break;

        case State::ACK_RECOVERY_WINDOW:
            if (event == Event::EV_FIRST_HEARTBEAT_AFTER_RECOVERY) {
                return transition_to(State::POST_RECONNECT_SETTLE, event, now_ms);
            }
            if (event == Event::EV_DISCOVERY_ACK_SEND_FAIL_NO_MEM) {
                if (stats_.ack_no_mem_failures < UINT32_MAX) {
                    stats_.ack_no_mem_failures++;
                }
                if (stats_.ack_no_mem_failures >= config_.max_no_mem_failures_for_fallback) {
                    return transition_to(State::DEGRADED_FALLBACK, event, now_ms);
                }
                return true;
            }
            if (event == Event::EV_DISCOVERY_ACK_SENT_OK) {
                stats_.ack_no_mem_failures = 0;
                return true;
            }
            break;

        case State::POST_RECONNECT_SETTLE:
            if (event == Event::EV_SETTLE_TIMER_EXPIRED) {
                return transition_to(State::STEADY_CONNECTED, event, now_ms);
            }
            // EV_HEARTBEAT_STALE intentionally not handled here: intermittent delivery
            // failures cause heartbeat gaps < 2500 ms that must not reset the settle
            // timer. Genuine link loss during the settle window is caught by
            // EV_PROBE_WHILE_PREVIOUSLY_CONNECTED (TX rebooted again) and by the
            // EspNowConnectionManager 32-second heartbeat timeout path.
            if (event == Event::EV_PROBE_WHILE_PREVIOUSLY_CONNECTED) {
                return transition_to(State::RECONNECT_DETECTED, event, now_ms);
            }
            break;

        case State::DEGRADED_FALLBACK:
            if (event == Event::EV_DISCOVERY_ACK_SENT_OK) {
                return transition_to(State::ACK_RECOVERY_WINDOW, event, now_ms);
            }
            break;
    }

    return false;
}

bool RxRadioArbiterFsm::tick(uint32_t now_ms) {
    if (state_ == State::RECONNECT_DETECTED && time_reached(now_ms, reconnect_deadline_ms_)) {
        return on_event(Event::EV_RECONNECT_TIMEOUT, now_ms);
    }

    if (state_ == State::POST_RECONNECT_SETTLE && time_reached(now_ms, settle_deadline_ms_)) {
        return on_event(Event::EV_SETTLE_TIMER_EXPIRED, now_ms);
    }

    return false;
}

RxRadioArbiterFsm::State RxRadioArbiterFsm::state() const {
    return state_;
}

const RxRadioArbiterFsm::Policy& RxRadioArbiterFsm::policy() const {
    return policy_;
}

const RxRadioArbiterFsm::Config& RxRadioArbiterFsm::config() const {
    return config_;
}

const RxRadioArbiterFsm::Stats& RxRadioArbiterFsm::stats() const {
    return stats_;
}

const char* RxRadioArbiterFsm::to_string(State state) {
    switch (state) {
        case State::BOOTSTRAP:
            return "BOOTSTRAP";
        case State::STEADY_CONNECTED:
            return "STEADY_CONNECTED";
        case State::RECONNECT_DETECTED:
            return "RECONNECT_DETECTED";
        case State::ACK_RECOVERY_WINDOW:
            return "ACK_RECOVERY_WINDOW";
        case State::POST_RECONNECT_SETTLE:
            return "POST_RECONNECT_SETTLE";
        case State::DEGRADED_FALLBACK:
            return "DEGRADED_FALLBACK";
    }
    return "UNKNOWN";
}

const char* RxRadioArbiterFsm::to_string(Event event) {
    switch (event) {
        case Event::EV_HEARTBEAT_FRESH:
            return "EV_HEARTBEAT_FRESH";
        case Event::EV_HEARTBEAT_STALE:
            return "EV_HEARTBEAT_STALE";
        case Event::EV_PROBE_WHILE_PREVIOUSLY_CONNECTED:
            return "EV_PROBE_WHILE_PREVIOUSLY_CONNECTED";
        case Event::EV_DISCOVERY_ACK_SENT_OK:
            return "EV_DISCOVERY_ACK_SENT_OK";
        case Event::EV_DISCOVERY_ACK_SEND_FAIL_NO_MEM:
            return "EV_DISCOVERY_ACK_SEND_FAIL_NO_MEM";
        case Event::EV_FIRST_HEARTBEAT_AFTER_RECOVERY:
            return "EV_FIRST_HEARTBEAT_AFTER_RECOVERY";
        case Event::EV_SETTLE_TIMER_EXPIRED:
            return "EV_SETTLE_TIMER_EXPIRED";
        case Event::EV_RECONNECT_TIMEOUT:
            return "EV_RECONNECT_TIMEOUT";
    }
    return "UNKNOWN";
}

void RxRadioArbiterFsm::enter_state(State new_state, uint32_t now_ms) {
    state_ = new_state;
    state_entered_ms_ = now_ms;
    stats_.last_state = state_;
    stats_.last_transition_ms = now_ms;

    reconnect_deadline_ms_ = 0U;
    settle_deadline_ms_ = 0U;

    if (state_ == State::RECONNECT_DETECTED) {
        reconnect_deadline_ms_ = now_ms + config_.reconnect_timeout_ms;
    } else if (state_ == State::POST_RECONNECT_SETTLE) {
        settle_deadline_ms_ = now_ms + config_.post_reconnect_settle_ms;
    } else if (state_ != State::ACK_RECOVERY_WINDOW) {
        stats_.ack_no_mem_failures = 0;
    }

    apply_policy_for_state(state_, true);
}

void RxRadioArbiterFsm::apply_policy_for_state(State state, bool is_entry) {
    policy_.purge_non_control_on_entry = false;

    switch (state) {
        case State::BOOTSTRAP:
            policy_.mqtt_allowed = false;
            policy_.control_only_mode = true;
            policy_.noncritical_enqueue_allowed = false;
            policy_.ack_retry_profile = AckRetryProfile::RECOVERY;
            policy_.diagnostic_profile = DiagnosticProfile::NORMAL;
            break;

        case State::STEADY_CONNECTED:
            policy_.mqtt_allowed = true;
            policy_.control_only_mode = false;
            policy_.noncritical_enqueue_allowed = true;
            policy_.ack_retry_profile = AckRetryProfile::NORMAL;
            policy_.diagnostic_profile = DiagnosticProfile::NORMAL;
            break;

        case State::RECONNECT_DETECTED:
            policy_.mqtt_allowed = false;
            policy_.control_only_mode = true;
            policy_.noncritical_enqueue_allowed = false;
            policy_.ack_retry_profile = AckRetryProfile::RECOVERY;
            policy_.diagnostic_profile = DiagnosticProfile::VERBOSE_RECONNECT;
            policy_.purge_non_control_on_entry = is_entry;
            break;

        case State::ACK_RECOVERY_WINDOW:
            policy_.mqtt_allowed = false;
            policy_.control_only_mode = true;
            policy_.noncritical_enqueue_allowed = false;
            policy_.ack_retry_profile = AckRetryProfile::RECOVERY;
            policy_.diagnostic_profile = DiagnosticProfile::VERBOSE_RECONNECT;
            break;

        case State::POST_RECONNECT_SETTLE:
            policy_.mqtt_allowed = false;
            policy_.control_only_mode = true;
            policy_.noncritical_enqueue_allowed = false;
            policy_.ack_retry_profile = AckRetryProfile::RECOVERY;
            policy_.diagnostic_profile = DiagnosticProfile::VERBOSE_RECONNECT;
            break;

        case State::DEGRADED_FALLBACK:
            policy_.mqtt_allowed = false;
            policy_.control_only_mode = true;
            policy_.noncritical_enqueue_allowed = false;
            policy_.ack_retry_profile = AckRetryProfile::RECOVERY;
            policy_.diagnostic_profile = DiagnosticProfile::VERBOSE_RECONNECT;
            break;
    }
}

bool RxRadioArbiterFsm::transition_to(State new_state, Event event, uint32_t now_ms) {
    (void)event;
    if (new_state == state_) {
        return false;
    }

    stats_.transition_count++;
    enter_state(new_state, now_ms);
    return true;
}
