#ifndef LED_H_
#define LED_H_

#include <esp_err.h>

/*
 * LED Handler - Event-driven ESP-NOW LED publisher
 * 
 * The Olimex board has no physical LED. This module publishes status LED
 * packets to the receiver, which owns rendering and animation.
 */
esp_err_t led_publish_current_state(bool force = false, const uint8_t* receiver_mac = nullptr);

#endif  // LED_H_
