#pragma once

#include "rx_runtime_messages.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace EspNowReceiver {

class RouteRegistry {
public:
    using Handler = std::function<void(const RuntimeMessageView& message)>;

    bool register_handler(uint8_t message_type, Handler handler);
    bool unregister_handler(uint8_t message_type);
    bool has_handler(uint8_t message_type) const;
    bool dispatch(const RuntimeMessageView& message) const;
    size_t registered_handler_count() const { return handler_count_; }
    void clear();

private:
    std::array<Handler, 256> handlers_{};
    size_t handler_count_{0};
};

}  // namespace EspNowReceiver