#pragma once

// Force standard FILE definition for ESP-IDF headers used in C++ compilation units.
#include <cstdio>
#include <stdio.h>

// Compatibility shim for WiFi config macro mismatch across framework versions.
#ifndef CONFIG_ESP_WIFI_DYNAMIC_RX_MGMT_BUF
  #ifdef CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM
    #define CONFIG_ESP_WIFI_DYNAMIC_RX_MGMT_BUF CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM
  #else
    #define CONFIG_ESP_WIFI_DYNAMIC_RX_MGMT_BUF 0
  #endif
#endif

#ifndef CONFIG_ESP_WIFI_TX_BUFFER_TYPE
  #ifdef CONFIG_ESP32_WIFI_TX_BUFFER_TYPE
    #define CONFIG_ESP_WIFI_TX_BUFFER_TYPE CONFIG_ESP32_WIFI_TX_BUFFER_TYPE
  #else
    #define CONFIG_ESP_WIFI_TX_BUFFER_TYPE 0
  #endif
#endif
