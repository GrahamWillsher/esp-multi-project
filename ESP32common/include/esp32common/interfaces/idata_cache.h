#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Abstract latest-data cache contract for DI-friendly components.
 */
class IDataCache {
public:
    virtual ~IDataCache() = default;

    /**
     * @brief Update cache with latest raw payload.
     * @return true on success
     */
    virtual bool update(const uint8_t* data, size_t length) = 0;

    /**
     * @brief Copy latest cached payload into caller buffer.
     * @param out Destination buffer
     * @param capacity Destination buffer size
     * @return Number of bytes copied
     */
    virtual size_t copy_latest(uint8_t* out, size_t capacity) const = 0;
};
