#pragma once

#include <Arduino.h>

// ESP-NOW task functions

// ESP-NOW worker task - processes queued messages
void task_espnow_worker(void *parameter);

// Periodic announcement task - helps establish bidirectional connection
void task_periodic_announcement(void *parameter);

// Message route initialization (called once in setup() before worker task starts)
void setup_message_routes();
