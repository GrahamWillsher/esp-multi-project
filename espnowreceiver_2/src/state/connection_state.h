#pragma once

#include <Arduino.h>

// Connection state tracking for timeout detection
struct ConnectionState {
    bool is_connected;
    uint32_t last_rx_time_ms;
};
