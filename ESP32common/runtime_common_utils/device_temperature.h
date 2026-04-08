#pragma once

#include <Arduino.h>

namespace DeviceTemperature {

struct Reading {
    bool valid = false;
    int16_t centi_celsius = 0;
    uint32_t sample_time_ms = 0;
};

void init();
void sample_now();
void tick(uint32_t interval_ms);
Reading get_latest();
float to_celsius(int16_t centi_celsius);

}  // namespace DeviceTemperature