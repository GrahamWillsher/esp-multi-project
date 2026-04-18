#pragma once

#include <Arduino.h>

namespace ESPNowRuntime {

// Initialize the ESP-NOW radio only.
bool init_radio();

// Create queue-backed runtime state and register message routes.
bool prepare_runtime();

// Initialize connection/state machinery, callbacks, and discovery.
bool init_state();

// Worker task entrypoint (consumes inbound ESP-NOW messages and pushes display snapshots).
void task_worker(void* parameter);

// Link state inferred from recent telemetry reception.
bool is_connected();

}  // namespace ESPNowRuntime
