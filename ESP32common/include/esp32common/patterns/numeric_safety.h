#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace esp32common::numeric {

template <typename T>
inline bool increment_saturating(T& value, T max_value = std::numeric_limits<T>::max()) {
    static_assert(std::is_integral<T>::value, "increment_saturating requires integral type");
    if (value < max_value) {
        ++value;
        return true;
    }
    return false;
}

template <typename T>
inline bool add_saturating(T& value, T addend, T max_value = std::numeric_limits<T>::max()) {
    static_assert(std::is_integral<T>::value, "add_saturating requires integral type");
    if (addend <= 0) {
        return false;
    }

    const T room = static_cast<T>(max_value - value);
    if (room <= 0) {
        return false;
    }

    if (addend >= room) {
        value = max_value;
        return true;
    }

    value = static_cast<T>(value + addend);
    return true;
}

// Safe due-check for wrapping 32-bit monotonic timers (e.g., millis())
constexpr bool due_u32(uint32_t now_ms, uint32_t deadline_ms) {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

}  // namespace esp32common::numeric
