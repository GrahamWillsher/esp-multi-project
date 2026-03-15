#pragma once

/**
 * @brief Abstract connection-lifecycle contract for DI-friendly components.
 */
class IConnectionManager {
public:
    virtual ~IConnectionManager() = default;

    /**
     * @return true when transport is connected and ready
     */
    virtual bool is_connected() const = 0;

    /**
     * @brief Initiate connection process.
     */
    virtual void connect() = 0;

    /**
     * @brief Initiate graceful disconnect process.
     */
    virtual void disconnect() = 0;
};
