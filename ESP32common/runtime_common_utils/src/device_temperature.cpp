#include <runtime_common_utils/device_temperature.h>

#include <math.h>
#include <limits.h>

namespace {
DeviceTemperature::Reading g_reading;
bool g_initialized = false;
uint32_t g_last_sample_ms = 0;

int16_t clamp_to_int16(long value) {
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return static_cast<int16_t>(value);
}
}  // namespace

namespace DeviceTemperature {

void init() {
    if (g_initialized) {
        return;
    }

    g_reading = {};
    g_last_sample_ms = 0;
    g_initialized = true;
}

void sample_now() {
    init();

    const uint32_t now = millis();
    const float celsius = temperatureRead();

    g_last_sample_ms = now;
    g_reading.sample_time_ms = now;

    if (isnan(celsius) || celsius < -100.0f || celsius > 200.0f) {
        g_reading.valid = false;
        g_reading.centi_celsius = 0;
        return;
    }

    g_reading.valid = true;
    g_reading.centi_celsius = clamp_to_int16(lroundf(celsius * 100.0f));
}

void tick(uint32_t interval_ms) {
    init();

    const uint32_t now = millis();
    if (g_last_sample_ms == 0 || now - g_last_sample_ms >= interval_ms) {
        sample_now();
    }
}

Reading get_latest() {
    init();
    return g_reading;
}

float to_celsius(int16_t centi_celsius) {
    return static_cast<float>(centi_celsius) / 100.0f;
}

}  // namespace DeviceTemperature