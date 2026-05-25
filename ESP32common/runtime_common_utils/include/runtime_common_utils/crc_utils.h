#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RuntimeCrcUtils {

inline uint32_t crc32(const void* data, size_t len) {
    if (!data || len == 0) {
        return 0;
    }

    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    for (size_t index = 0; index < len; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if ((crc & 1u) != 0) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFFu;
}

template<typename T>
inline uint32_t calculate_message_crc32(const T* message) {
    if (!message) {
        return 0;
    }
    return crc32(message, sizeof(T));
}

template<typename T>
inline uint32_t calculate_message_crc32_zeroed(const T* message) {
    static_assert(sizeof(T) >= sizeof(uint32_t), "Message must be large enough to contain trailing CRC32");

    if (!message) {
        return 0;
    }

    T copy = *message;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&copy);
    memset(bytes + sizeof(T) - sizeof(uint32_t), 0, sizeof(uint32_t));
    return crc32(bytes, sizeof(T));
}

template<typename T>
inline bool verify_message_crc32(const T* message) {
    static_assert(sizeof(T) >= sizeof(uint32_t), "Message must be large enough to contain trailing CRC32");

    if (!message) {
        return false;
    }

    uint32_t stored_crc = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(message);
    memcpy(&stored_crc, bytes + sizeof(T) - sizeof(uint32_t), sizeof(stored_crc));
    return calculate_message_crc32_zeroed(message) == stored_crc;
}

}  // namespace RuntimeCrcUtils
