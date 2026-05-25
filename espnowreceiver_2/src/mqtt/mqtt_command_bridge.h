#pragma once

#include <cstdint>

uint8_t get_last_debug_level();
uint8_t get_last_test_data_mode();
bool send_test_data_mode_control(uint8_t mode);
void cache_last_debug_level(uint8_t level);
