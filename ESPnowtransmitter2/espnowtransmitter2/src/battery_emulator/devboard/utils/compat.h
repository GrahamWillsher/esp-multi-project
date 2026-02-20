#pragma once

// Compatibility helpers for Arduino-ESP32 core 2.x
#if !defined(ARDUINO_ESP32_MAJOR) || (ARDUINO_ESP32_MAJOR < 3)
  #ifndef ledcAttachChannel
    #define ledcAttachChannel(pin, freq, res, channel) \
      (ledcSetup((channel), (freq), (res)), ledcAttachPin((pin), (channel)))
  #endif
#endif

// Map legacy BMS fault name used in older code
#ifndef BMS_FAULT
  #define BMS_FAULT BMS_FAULT_EMULATOR
#endif
