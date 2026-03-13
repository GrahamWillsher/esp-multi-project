#pragma once

#include <Arduino.h>

// Test data generation functions

// Generate test data (increments SOC and power)
void generate_test_data();

// FreeRTOS task for test data generation
void task_generate_test_data(void *parameter);
