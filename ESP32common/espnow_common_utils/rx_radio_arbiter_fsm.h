#pragma once

#include <cstdint>

class RxRadioArbiterFsm {
public:
    enum class State : uint8_t {
        BOOTSTRAP = 0,
        STEADY_CONNECTED,
        RECONNECT_DETECTED,
        ACK_RECOVERY_WINDOW,
        POST_RECONNECT_SETTLE,
        DEGRADED_FALLBACK,
    };

    enum class Event : uint8_t {
        EV_HEARTBEAT_FRESH = 0,
        EV_HEARTBEAT_STALE,
        EV_PROBE_WHILE_PREVIOUSLY_CONNECTED,
        EV_DISCOVERY_ACK_SENT_OK,
        EV_DISCOVERY_ACK_SEND_FAIL_NO_MEM,
        EV_FIRST_HEARTBEAT_AFTER_RECOVERY,
        EV_SETTLE_TIMER_EXPIRED,
        EV_RECONNECT_TIMEOUT,
    };

    enum class AckRetryProfile : uint8_t {
        NORMAL = 0,
        RECOVERY,
    };

    enum class DiagnosticProfile : uint8_t {
        NORMAL = 0,
        VERBOSE_RECONNECT,
    };

    struct Policy {
        bool mqtt_allowed = false;
        bool control_only_mode = true;
        bool purge_non_control_on_entry = false;
        bool noncritical_enqueue_allowed = false;
        AckRetryProfile ack_retry_profile = AckRetryProfile::RECOVERY;
        DiagnosticProfile diagnostic_profile = DiagnosticProfile::NORMAL;
    };

    struct Config {
        uint32_t reconnect_timeout_ms = 5000U;
        uint32_t post_reconnect_settle_ms = 8000U;
        uint8_t max_no_mem_failures_for_fallback = 3U;
    };

    struct Stats {
        uint32_t total_events = 0;
        uint32_t transition_count = 0;
        uint32_t ack_no_mem_failures = 0;
        uint32_t last_transition_ms = 0;
        State last_state = State::BOOTSTRAP;
        Event last_event = Event::EV_HEARTBEAT_FRESH;
    };

    static RxRadioArbiterFsm& instance();

    void init();
    void init(const Config& config);
    void reset(uint32_t now_ms = 0U);

    bool on_event(Event event, uint32_t now_ms);
    bool tick(uint32_t now_ms);

    State state() const;
    const Policy& policy() const;
    const Config& config() const;
    const Stats& stats() const;

    static const char* to_string(State state);
    static const char* to_string(Event event);

private:
    RxRadioArbiterFsm() = default;

    void enter_state(State new_state, uint32_t now_ms);
    void apply_policy_for_state(State state, bool is_entry);
    bool transition_to(State new_state, Event event, uint32_t now_ms);

    Config config_{};
    State state_{State::BOOTSTRAP};
    Policy policy_{};
    Stats stats_{};

    uint32_t state_entered_ms_{0};
    uint32_t reconnect_deadline_ms_{0};
    uint32_t settle_deadline_ms_{0};
};
