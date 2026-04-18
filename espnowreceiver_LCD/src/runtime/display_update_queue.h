#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

namespace DisplayUpdateQueue {

struct snapshot_t {
    float soc_percent = 0.0f;
    int32_t power_w = 0;
};

bool init(size_t depth = 8);
bool enqueue(const snapshot_t& snapshot);
bool try_dequeue(snapshot_t& out_snapshot);

}  // namespace DisplayUpdateQueue
