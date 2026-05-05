#pragma once

#include "rx_connection_handler.h"
#include "rx_heartbeat_manager.h"
#include "rx_runtime_hooks.h"
#include "rx_runtime_routes.h"
#include "rx_state_machine.h"
#include <cstddef>
#include <cstdint>

namespace EspNowReceiver {

struct RuntimeStats {
    uint32_t messages_received = 0;
    uint32_t routed_messages = 0;
    uint32_t unhandled_messages = 0;
    uint32_t last_message_ms = 0;
    uint32_t last_tick_ms = 0;
};

class Runtime {
public:
    static Runtime& instance();

    bool init(RuntimeHooks* hooks = nullptr);
    void reset();
    void tick();
    void set_hooks(RuntimeHooks* hooks);

    bool register_route(uint8_t message_type, RouteRegistry::Handler handler);
    bool handle_message(const uint8_t* sender_mac, const uint8_t* payload, size_t payload_length);

    bool is_connected() const;
    const RuntimeStats& stats() const { return stats_; }
    RouteRegistry& routes() { return routes_; }
    ConnectionHandler& connection_handler() { return ConnectionHandler::instance(); }
    HeartbeatManager& heartbeat_manager() { return heartbeat_manager_; }
    StateMachine& state_machine() { return state_machine_; }

private:
    Runtime() = default;

    void register_default_routes();

    RuntimeHooks* hooks_{nullptr};
    bool initialized_{false};
    RouteRegistry routes_{};
    HeartbeatManager heartbeat_manager_{};
    StateMachine state_machine_{};
    RuntimeStats stats_{};
};

}  // namespace EspNowReceiver