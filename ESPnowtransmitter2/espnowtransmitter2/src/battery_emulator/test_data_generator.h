#pragma once

/**
 * @file test_data_generator.h
 * @brief Generates realistic dummy battery data for testing without physical battery
 * 
 * TESTING ONLY: This module populates the datalayer with simulated battery data
 * when no CAN bus connection is available. Enable via TEST_DATA_GENERATOR define.
 * 
 * Simulates:
 * - SOC cycling between 20-95%
 * - Realistic voltage changes (300-420V range)
 * - Power variation (-5000W to +3000W)
 * - Temperature fluctuations
 * - Cell voltages
 * - BMS status transitions
 */

namespace TestDataGenerator {

/**
 * @brief Initialize test data generator
 * Sets up initial realistic values in datalayer
 */
void init();

/**
 * @brief Update test data with realistic variations
 * Call periodically (e.g., every 100ms) to simulate changing battery conditions
 */
void update();

/**
 * @brief Check if test data generator is enabled
 * @return true if enabled via compile-time flag
 */
bool is_enabled();

} // namespace TestDataGenerator
