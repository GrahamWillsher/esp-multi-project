#pragma once
#include <cstdint>

namespace TxControlHandlers {
void save_debug_level(uint8_t level);
uint8_t load_debug_level();

} // namespace TxControlHandlers