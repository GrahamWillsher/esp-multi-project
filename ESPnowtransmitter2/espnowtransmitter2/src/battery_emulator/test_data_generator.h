#pragma once

/**
 * @file test_data_generator.h
 * @brief Generates realistic dummy battery data for testing without physical battery
 * 
 * Phase 2: Runtime-configurable test data generation with mode support.
 * Replaces compile-time TEST_DATA_GENERATOR flag with dynamic configuration.
 * 
 * Simulates:
 * - SOC cycling between 20-95%
 * - Realistic voltage changes (300-420V range)
 * - Power variation (-5000W to +3000W)
 * - Temperature fluctuations
 * - Cell voltages (configurable cell count)
 * - BMS status transitions
 */

namespace TestDataGenerator {

/**
 * @brief Initialize test data generator
 * Sets up initial realistic values in datalayer based on battery selection
 */
void init();

/**
 * @brief Update test data with realistic variations
 * Call periodically (e.g., every 100ms) to simulate changing battery conditions
 */
void update();

/**
 * @brief Check if test data generator is enabled
 * @return true if enabled via runtime configuration
 */
bool is_enabled();

/**
 * @brief Enable or disable test data generator at runtime
 * @param enabled True to enable, false to disable
 */
void set_enabled(bool enabled);

/**
 * @brief Check if cell generation is enabled
 * @return true if cell data should be generated (FULL_BATTERY_DATA mode)
 */
bool is_cell_generation_enabled();

/**
 * @brief Enable or disable cell voltage generation
 * @param enabled True to generate cells, false to skip
 */
void set_cell_generation_enabled(bool enabled);

/**
 * @brief Reinitialize with current battery configuration
 * Call after battery type changes or configuration updates
 */
void reinitialize();

} // namespace TestDataGenerator

