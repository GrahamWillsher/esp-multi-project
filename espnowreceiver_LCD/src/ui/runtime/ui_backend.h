#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>

namespace UI::Runtime::Backend {

bool init(lgfx::LGFX_Device& display);
void run_startup_sequence();
void set_state(float soc_percent, int32_t power_w);
void set_led_state(uint8_t color, uint8_t effect);
void set_network_status(const char* ip, bool wifi_ok);  // call before tasks start; LVGL-safe
void set_link_connected(bool connected);
void tick(uint32_t now_ms);

}  // namespace UI::Runtime::Backend
