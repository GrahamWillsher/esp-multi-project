#ifndef TEST_MODE_H
#define TEST_MODE_H

#include <cstdint>
#include <vector>

/**
 * @file test_mode.h
 * @brief Test Mode Management for Transmitter
 * 
 * Provides realistic test data generation for battery monitoring system.
 * Test data simulates realistic BMS behavior for debugging and demonstration.
 * 
 * Features:
 * - Realistic SOC drift (charging/discharging)
 * - Temperature variations
 * - Cell voltage patterns
 * - Balancing simulation
 * - Configurable test scenarios
 */

namespace TestMode {

// Test Mode State Structure
struct TestState {
    // Power state
    uint8_t soc = 50;                  // State of charge (0-100%)
    int32_t power = 0;                 // Power in watts (positive=charging, negative=discharge)
    uint32_t voltage_mv = 0;           // Pack voltage in mV
    
    // Temperature
    uint16_t bms_temperature = 298;    // BMS temperature in Kelvin (298K = 25°C)
    uint16_t cell_temperature = 298;   // Cell temperature in Kelvin
    
    // Cell data
    std::vector<uint16_t> cell_voltages;  // Individual cell voltages in mV
    std::vector<bool> balancing_active;   // Which cells are balancing
    
    // Derived values
    uint16_t max_cell_voltage = 0;
    uint16_t min_cell_voltage = 0;
    uint16_t cell_deviation = 0;
};

// Configuration for test scenarios
struct TestConfig {
    int32_t power_rate = -100;         // Power change per second (W/s) - negative = discharging
    float soc_drift_rate = 0.5f;       // SOC change per second (%)
    int16_t temperature_change = 0;    // Temperature change in K
    uint8_t num_cells = 96;            // Number of cells
    
    // Test scenario type
    enum ScenarioType {
        SCENARIO_STABLE,                // Stable discharge
        SCENARIO_CHARGING,              // Charging scenario
        SCENARIO_FAST_DISCHARGE,        // Rapid discharge
        SCENARIO_HIGH_TEMPERATURE,      // High temp warning
        SCENARIO_IMBALANCE,             // Unbalanced cells
        SCENARIO_FAULT                  // Fault simulation
    };
    ScenarioType scenario = SCENARIO_STABLE;
};

// ============================================================================
// Core API
// ============================================================================

/**
 * @brief Initialize test mode system
 * @param num_cells Number of cells to simulate (default 96)
 * @return true if initialization successful
 */
bool initialize(uint8_t num_cells = 96);

/**
 * @brief Enable or disable test mode
 * @param enable true to enable test mode
 * @return true if state changed
 */
bool set_enabled(bool enable);

/**
 * @brief Check if test mode is currently enabled
 * @return true if test mode is active
 */
bool is_enabled();

/**
 * @brief Generate next test data sample
 * Advances internal state based on configured drift rates.
 * Should be called periodically (every 100-500ms recommended).
 * 
 * @return const TestState& Reference to current test state
 */
const TestState& generate_sample();

/**
 * @brief Get current test state without advancing
 * @return const TestState& Reference to current test state
 */
const TestState& get_current_state();

/**
 * @brief Configure test scenario
 * @param config Configuration to apply
 */
void configure_scenario(const TestConfig& config);

/**
 * @brief Set test scenario by type
 * @param scenario Scenario type to activate
 */
void set_scenario(TestConfig::ScenarioType scenario);

/**
 * @brief Reset test data to initial state
 */
void reset();

// ============================================================================
// Advanced API
// ============================================================================

/**
 * @brief Set specific test value (for fine-tuning)
 * @param soc State of charge (0-100)
 * @param power Power in watts
 * @param voltage Pack voltage in mV
 */
void set_values(uint8_t soc, int32_t power, uint32_t voltage);

/**
 * @brief Get cell count
 * @return Number of cells being simulated
 */
uint8_t get_cell_count();

/**
 * @brief Get specific cell voltage
 * @param cell_index Index of cell (0-based)
 * @return Cell voltage in mV, or 0 if invalid index
 */
uint16_t get_cell_voltage(uint8_t cell_index);

/**
 * @brief Check if specific cell is balancing
 * @param cell_index Index of cell (0-based)
 * @return true if cell is actively balancing
 */
bool is_cell_balancing(uint8_t cell_index);

/**
 * @brief Simulate imbalance condition
 * Creates voltage spread between cells for testing BMS balancing.
 * @param spread_mv Maximum voltage spread in mV
 */
void simulate_imbalance(uint16_t spread_mv = 100);

/**
 * @brief Simulate fault condition
 * Creates low or high voltage condition for testing BMS fault handling.
 * @param cell_index Cell to fault (0-based), or 0xFF for no fault
 * @param voltage_mv Voltage to set for faulted cell
 */
void simulate_fault(uint8_t cell_index = 0xFF, uint16_t voltage_mv = 0);

/**
 * @brief Get diagnostics string for logging
 * @return String with current test state info
 */
const char* get_diagnostics();

} // namespace TestMode

#endif // TEST_MODE_H
