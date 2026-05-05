#include "rx_runtime.h"

#include "rx_runtime_messages.h"

namespace EspNowReceiver {

Runtime& Runtime::instance() {
    static Runtime runtime;
    return runtime;
}

bool Runtime::init(RuntimeHooks* hooks) {
    hooks_ = hooks;
    stats_ = RuntimeStats{};
    routes_.clear();
    heartbeat_manager_.init();
    state_machine_.init();
    connection_handler().init();
    connection_handler().set_runtime_hooks(hooks_);
    register_default_routes();
    initialized_ = true;

    if (hooks_ != nullptr) {
        hooks_->on_runtime_initialized();
    }

    return true;
}

void Runtime::reset() {
    routes_.clear();
    heartbeat_manager_.reset();
    state_machine_.reset();
    connection_handler().reset();
    stats_ = RuntimeStats{};
    register_default_routes();

    if (hooks_ != nullptr) {
        hooks_->on_runtime_reset();
    }
}

void Runtime::tick() {
    if (!initialized_) {
        return;
    }

    const uint32_t now_ms = millis();
    heartbeat_manager_.tick();
    connection_handler().tick(heartbeat_manager_.time_since_last(now_ms));
    state_machine_.check_stale(15000);
    stats_.last_tick_ms = now_ms;

    if (hooks_ != nullptr) {
        hooks_->on_runtime_tick(now_ms);
    }
}

void Runtime::set_hooks(RuntimeHooks* hooks) {
    hooks_ = hooks;
    connection_handler().set_runtime_hooks(hooks);
}

bool Runtime::register_route(uint8_t message_type, RouteRegistry::Handler handler) {
    return routes_.register_handler(message_type, std::move(handler));
}

bool Runtime::handle_message(const uint8_t* sender_mac, const uint8_t* payload, size_t payload_length) {
    if (!initialized_ || payload == nullptr || payload_length == 0) {
        return false;
    }

    const uint8_t message_type = payload[0];
    const uint32_t now_ms = millis();
    RuntimeMessageView message{message_type, payload, payload_length, sender_mac};

    ++stats_.messages_received;
    stats_.last_message_ms = now_ms;
    state_machine_.on_message_processing(message_type, stats_.messages_received);

    switch (message_type) {
        case msg_probe:
            connection_handler().on_probe_received(sender_mac);
            break;

        case msg_ack:
            connection_handler().on_link_activity(sender_mac);
            break;

        case msg_heartbeat:
            connection_handler().on_link_activity(sender_mac);
            if (payload_length >= sizeof(heartbeat_t)) {
                heartbeat_manager_.on_heartbeat(reinterpret_cast<const heartbeat_t*>(payload), sender_mac);
            }
            break;

        case msg_heartbeat_ack:
            connection_handler().on_link_activity(sender_mac);
            heartbeat_manager_.record_ack_sent();
            break;

        default:
            if (participates_in_generic_activity(message_type)) {
                connection_handler().on_link_activity(sender_mac);
            }
            break;
    }

    if (participates_in_generic_activity(message_type)) {
        state_machine_.on_activity();
    }

    if (routes_.dispatch(message)) {
        ++stats_.routed_messages;
    } else {
        ++stats_.unhandled_messages;
    }

    state_machine_.on_message_valid();

    if (hooks_ != nullptr) {
        hooks_->on_message_processed(message_type, sender_mac, payload_length);
    }

    return true;
}

bool Runtime::is_connected() const {
    const StateMachine::ConnectionState state = state_machine_.connection_state();
    return state == StateMachine::ConnectionState::CONNECTED ||
           state == StateMachine::ConnectionState::ACTIVE;
}

void Runtime::register_default_routes() {
    routes_.register_handler(msg_probe, [](const RuntimeMessageView&) {});
    routes_.register_handler(msg_ack, [](const RuntimeMessageView&) {});
    routes_.register_handler(msg_heartbeat, [](const RuntimeMessageView&) {});
    routes_.register_handler(msg_heartbeat_ack, [](const RuntimeMessageView&) {});
}

}  // namespace EspNowReceiver