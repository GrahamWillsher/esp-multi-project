#pragma once

#include <Arduino.h>
#include <cmath>

struct DemoState {
    float   soc_percent = 65.0f;
    int32_t power_w     = 0;
};

class DemoModel {
public:
    void update(uint32_t now_ms);
    const DemoState& state() const { return state_; }
private:
    DemoState state_;
    float soc_target_     = 65.0f;
    bool  soc_increasing_ = true;
    float power_cycle_    = 0.0f;
};
