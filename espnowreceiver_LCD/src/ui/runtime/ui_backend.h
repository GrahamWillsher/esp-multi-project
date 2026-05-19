#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <cstdint>

namespace UI::Runtime::Backend {

enum class PowerBarRendererMode : uint8_t {
	Original = 0,
	Soft = 1,
	Linear = 2,
	Hybrid = 3,
	OriginalRounded = 4,
};

bool init(lgfx::LGFX_Device& display);
void run_startup_sequence();
void set_state(float soc_percent, int32_t power_w);
void set_power_bar_mode(PowerBarRendererMode mode);
void set_led_state(uint8_t color, uint8_t effect);
void set_network_status(const char* status_text, bool wifi_ok);  // call from LVGL task/locked context
void set_link_connected(bool connected);
void tick(uint32_t now_ms);

}  // namespace UI::Runtime::Backend
