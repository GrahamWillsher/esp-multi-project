/*
 * Helper Functions
 * Common utility functions used across modules
 */

#pragma once

#include "common.h"

// Pre-calculate color gradient
void pre_calculate_color_gradient(uint16_t start_color, uint16_t end_color, int steps, uint16_t* output);

// Get power bar color based on value
uint16_t get_power_color(int32_t power, int max_power);

// Calculate checksum for ESP-NOW payload
uint16_t calculate_checksum(const espnow_payload_t *payload);
