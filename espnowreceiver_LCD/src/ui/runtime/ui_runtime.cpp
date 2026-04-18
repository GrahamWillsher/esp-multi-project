#include "ui/runtime/ui_runtime.h"

#include "ui/runtime/ui_backend.h"

namespace UI::Runtime {

bool init(lgfx::LGFX_Device& display) {
    return Backend::init(display);
}

void run_startup_sequence() {
    Backend::run_startup_sequence();
}

void set_state(float soc_percent, int32_t power_w) {
    Backend::set_state(soc_percent, power_w);
}

void set_led_state(uint8_t color, uint8_t effect) {
    Backend::set_led_state(color, effect);
}

void set_link_connected(bool connected) {
    Backend::set_link_connected(connected);
}

void tick(uint32_t now_ms) {
    Backend::tick(now_ms);
}

}  // namespace UI::Runtime
