#pragma once

#include <Arduino.h>

namespace DisplayUpdateQueue {

struct Snapshot {
    uint8_t soc;
    int32_t power_w;
    bool soc_changed;
    bool power_changed;
};

void init();
bool enqueue(uint8_t soc, int32_t power_w, bool soc_changed, bool power_changed);
void task_renderer(void* parameter);

}  // namespace DisplayUpdateQueue
