#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ═══════════════════════════════════════════════════════════════════════
// smart_delay()
//
// Task-aware delay. Uses vTaskDelay() when the FreeRTOS scheduler is
// running (so the calling task yields to peers), falls back to Arduino
// delay() during early setup() before the scheduler has started.
//
// Use this everywhere instead of bare delay() so splash and bootstrap
// sequences remain correct after Phase B introduces FreeRTOS tasks.
// ═══════════════════════════════════════════════════════════════════════
void smart_delay(uint32_t ms);
