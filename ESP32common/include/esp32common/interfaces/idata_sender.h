#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Abstract transport sender contract for DI-friendly components.
 */
class IDataSender {
public:
    virtual ~IDataSender() = default;

    /**
     * @brief Send raw bytes.
     * @param data Buffer to send
     * @param length Number of bytes in buffer
     * @return true when queued/sent successfully
     */
    virtual bool send_data(const uint8_t* data, size_t length) = 0;
};
