#pragma once

#include <cstddef>
#include <cstdint>

namespace config::event_logs {

// Orphaned-subscription cleanup TTL (primary safety net)
constexpr uint64_t SUBSCRIPTION_TTL_MS = 60000ULL;

// Maximum events per MQTT batch publish
constexpr size_t MAX_BATCH_SIZE = 100U;

// Receiver wait budget when confirming transmitter-side clear
constexpr uint32_t CLEAR_CONFIRM_TIMEOUT_MS = 1500U;

}  // namespace config::event_logs
