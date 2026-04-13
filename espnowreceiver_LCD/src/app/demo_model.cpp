#include "app/demo_model.h"

void DemoModel::update(uint32_t /*now_ms*/) {
    // ===== SOC: slow linear ramp 20..95..20% (matches transmitter2 pattern) =====
    if (soc_increasing_) {
        soc_target_ += 0.02f;  // ~0.02% per update tick
        if (soc_target_ >= 95.0f) {
            soc_target_ = 95.0f;
            soc_increasing_ = false;
        }
    } else {
        soc_target_ -= 0.03f;  // discharge slightly faster
        if (soc_target_ <= 20.0f) {
            soc_target_ = 20.0f;
            soc_increasing_ = true;
        }
    }
    state_.soc_percent = soc_target_;

    // ===== Power: asymmetric sine -5 kW (charging) .. +3 kW (discharging) =====
    power_cycle_ += 0.02f;
    const float norm = sinf(power_cycle_);  // -1..+1
    if (norm < 0.0f) {
        state_.power_w = static_cast<int32_t>(norm * 5000.0f);  // charging up to -5000 W
    } else {
        state_.power_w = static_cast<int32_t>(norm * 3000.0f);  // discharging up to +3000 W
    }
}
