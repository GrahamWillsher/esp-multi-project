#pragma once

#include <cstdint>

/**
 * @brief Process-wide runtime context for shared telemetry values.
 *
 * Transport-neutral. MQTT/runtime code can use this API for lightweight
 * cross-module telemetry access when needed.
 */
class RuntimeContext {
public:
    static RuntimeContext& instance();

    void set_tx_soc(uint8_t soc);
    uint8_t tx_soc() const { return tx_soc_; }

private:
    RuntimeContext() = default;
    ~RuntimeContext() = default;

    RuntimeContext(const RuntimeContext&) = delete;
    RuntimeContext& operator=(const RuntimeContext&) = delete;

    uint8_t tx_soc_{0};
};
