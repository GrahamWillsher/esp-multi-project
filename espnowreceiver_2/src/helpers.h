/*
 * Helper Functions
 * Common utility functions used across modules
 */

#pragma once

#include "common.h"

// Pre-calculate color gradient
void pre_calculate_color_gradient(uint16_t start_color, uint16_t end_color, int steps, uint16_t* output);
