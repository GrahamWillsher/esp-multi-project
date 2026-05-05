#include "rx_runtime_routes.h"

namespace EspNowReceiver {

bool RouteRegistry::register_handler(uint8_t message_type, Handler handler) {
    if (!handler) {
        return false;
    }

    if (!handlers_[message_type]) {
        ++handler_count_;
    }

    handlers_[message_type] = std::move(handler);
    return true;
}

bool RouteRegistry::unregister_handler(uint8_t message_type) {
    if (!handlers_[message_type]) {
        return false;
    }

    handlers_[message_type] = Handler{};
    --handler_count_;
    return true;
}

bool RouteRegistry::has_handler(uint8_t message_type) const {
    return static_cast<bool>(handlers_[message_type]);
}

bool RouteRegistry::dispatch(const RuntimeMessageView& message) const {
    const Handler& handler = handlers_[message.message_type];
    if (!handler) {
        return false;
    }

    handler(message);
    return true;
}

void RouteRegistry::clear() {
    for (Handler& handler : handlers_) {
        handler = Handler{};
    }
    handler_count_ = 0;
}

}  // namespace EspNowReceiver