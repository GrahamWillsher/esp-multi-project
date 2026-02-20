#ifndef LED_H_
#define LED_H_

#include "../../devboard/utils/types.h"
#include <driver/gpio.h>

/*
 * LED Handler - Simplified for Olimex ESP32-POE2 (no physical LED)
 * 
 * The Olimex board has no physical LED. This module sends LED status
 * via ESP-NOW to the receiver's simulated LED display.
 * 
 * The receiver handles all flashing/animation logic automatically.
 * We only send the color based on system status.
 */

// LED color enum (matches receiver's expectations)
enum LEDColor {
    LED_RED    = 0,    // Error/Fault state
    LED_GREEN  = 1,    // Normal operation
    LED_ORANGE = 2     // Warning state
};

class LED {
 public:
  LED(gpio_num_t pin, uint8_t maxBrightness) : mode(led_mode_enum::CLASSIC) {
      // Deprecated parameters - kept for compatibility only
      // Physical LED no longer used on Olimex board
  }

  LED(led_mode_enum mode, gpio_num_t pin, uint8_t maxBrightness) : mode(mode) {
      // Deprecated parameters - kept for compatibility only
      // Physical LED no longer used on Olimex board
  }

  void exe(void);

 private:
  led_mode_enum mode;
  
  // Determine LED color based on current system status
  uint8_t get_led_color_for_status(void);
};

bool led_init(void);
void led_exe(void);

#endif  // LED_H_
