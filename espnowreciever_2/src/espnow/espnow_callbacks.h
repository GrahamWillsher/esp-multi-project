#pragma once

#include <Arduino.h>
#include <esp_now.h>

// ESP-NOW callback functions

// Callback when data is received
void on_data_recv(const uint8_t *mac, const uint8_t *data, int len);

// Callback when data is sent
void on_espnow_sent(const uint8_t *mac, esp_now_send_status_t status);
