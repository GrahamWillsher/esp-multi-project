/**
 * @file hal_stub.h
 * @brief Stub HAL header to avoid ESP-IDF dependencies
 * 
 * Battery Emulator's HAL layer requires full ESP-IDF setup. 
 * This stub prevents those dependencies while allowing BMS parsing to work.
 */

#ifndef _HAL_STUB_H
#define _HAL_STUB_H

// Stub ESP-IDF includes that might be needed
#include <cstdint>
#include <vector>

// Stub GPIO number enum (used by HAL)
typedef enum {
  GPIO_NUM_0 = 0,
  GPIO_NUM_1 = 1,
  GPIO_NUM_2 = 2,
  GPIO_NUM_3 = 3,
  GPIO_NUM_4 = 4,
  GPIO_NUM_5 = 5,
  GPIO_NUM_MAX = 48
} gpio_num_t;

// Minimal Esp32Hal stub (doesn't require ESP-IDF)
class Esp32Hal {
 public:
  static Esp32Hal& instance();
  
  // GPIO allocation stubs
  gpio_num_t allocate_gpio(int hint_gpio_num) { return GPIO_NUM_0; }
  
 private:
  Esp32Hal() = default;
};

#endif
