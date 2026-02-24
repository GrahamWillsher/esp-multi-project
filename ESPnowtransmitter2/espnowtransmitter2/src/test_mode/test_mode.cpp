#include "test_mode.h"
#include <Arduino.h>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <esp_log.h>

static const char* TAG = "TestMode";

namespace TestMode {

// ============================================================================
// Internal State
// ============================================================================

static bool g_enabled = false;
static TestState g_state;
static TestConfig g_config;
static unsigned long g_last_update_ms = 0;
static const float TARGET_PACK_VOLTAGE = 345.6f;  // 96 cells × 3.6V nominal
static const uint16_t NOMINAL_CELL_VOLTAGE = 3600;  // 3.6V in mV

// ============================================================================
// Forward Declarations
// ============================================================================

static void update_cell_voltages();
static void update_balancing_status();
static void apply_scenario_drift();

// ============================================================================
// Core API Implementation
// ============================================================================

bool initialize(uint8_t num_cells) {
    ESP_LOGI(TAG, "Initializing test mode with %u cells", num_cells);
    
    g_config.num_cells = num_cells;
    g_state.cell_voltages.resize(num_cells, NOMINAL_CELL_VOLTAGE);
    g_state.balancing_active.resize(num_cells, false);
    
    // Initialize with stable discharge scenario
    g_config.scenario = TestConfig::SCENARIO_STABLE;
    g_config.power_rate = -100;  // Discharging at 100W/s
    g_config.soc_drift_rate = 0.5f;
    
    reset();
    
    ESP_LOGI(TAG, "Test mode initialized: %u cells, %s", 
             num_cells, g_enabled ? "enabled" : "disabled");
    return true;
}

bool set_enabled(bool enable) {
    if (g_enabled == enable) {
        return false;  // No state change
    }
    
    g_enabled = enable;
    if (g_enabled) {
        g_last_update_ms = millis();
        ESP_LOGI(TAG, "Test mode ENABLED");
    } else {
        ESP_LOGI(TAG, "Test mode DISABLED");
    }
    
    return true;
}

bool is_enabled() {
    return g_enabled;
}

const TestState& generate_sample() {
    if (!g_enabled) {
        return g_state;
    }
    
    unsigned long now_ms = millis();
    unsigned long elapsed_ms = now_ms - g_last_update_ms;
    g_last_update_ms = now_ms;
    
    if (elapsed_ms == 0) {
        return g_state;  // No time has passed
    }
    
    // Convert to seconds for drift calculations
    float elapsed_sec = elapsed_ms / 1000.0f;
    
    // Apply power drift
    g_state.power += (int32_t)(g_config.power_rate * elapsed_sec);
    
    // Apply SOC drift
    g_state.soc = std::max(0, std::min(100, 
        (int)(g_state.soc + g_config.soc_drift_rate * elapsed_sec)));
    
    // Apply temperature drift
    if (g_config.temperature_change != 0) {
        g_state.bms_temperature += g_config.temperature_change * elapsed_sec;
        g_state.cell_temperature += g_config.temperature_change * elapsed_sec;
    }
    
    // Update derived values
    apply_scenario_drift();
    update_cell_voltages();
    update_balancing_status();
    
    // Update pack voltage from cell voltages
    uint32_t total_voltage = 0;
    for (const auto& cell_voltage : g_state.cell_voltages) {
        total_voltage += cell_voltage;
    }
    g_state.voltage_mv = total_voltage / g_config.num_cells * g_config.num_cells / 1000;
    
    return g_state;
}

const TestState& get_current_state() {
    return g_state;
}

void configure_scenario(const TestConfig& config) {
    g_config = config;
    ESP_LOGI(TAG, "Scenario configured: type=%d, power_rate=%ld W/s", 
             config.scenario, config.power_rate);
}

void set_scenario(TestConfig::ScenarioType scenario) {
    g_config.scenario = scenario;
    
    // Apply scenario-specific settings
    switch (scenario) {
        case TestConfig::SCENARIO_STABLE:
            g_config.power_rate = -100;  // Slow discharge
            g_config.soc_drift_rate = 0.5f;
            g_config.temperature_change = 0;
            break;
            
        case TestConfig::SCENARIO_CHARGING:
            g_config.power_rate = 200;   // Charging
            g_config.soc_drift_rate = 1.0f;
            g_config.temperature_change = 50;  // Slow warming
            break;
            
        case TestConfig::SCENARIO_FAST_DISCHARGE:
            g_config.power_rate = -500;  // Fast discharge (5kW)
            g_config.soc_drift_rate = 3.0f;
            g_config.temperature_change = -100;  // Cooling
            break;
            
        case TestConfig::SCENARIO_HIGH_TEMPERATURE:
            g_config.power_rate = 0;
            g_config.soc_drift_rate = 0.1f;
            g_config.temperature_change = 200;  // Rapid heating
            break;
            
        case TestConfig::SCENARIO_IMBALANCE:
            g_config.power_rate = -50;
            g_config.soc_drift_rate = 0.2f;
            g_config.temperature_change = 0;
            simulate_imbalance(150);  // 150mV spread
            break;
            
        case TestConfig::SCENARIO_FAULT:
            g_config.power_rate = -1000;  // Protective discharge
            g_config.soc_drift_rate = 5.0f;
            g_config.temperature_change = 0;
            break;
    }
    
    ESP_LOGI(TAG, "Scenario set to type %d", scenario);
}

void reset() {
    g_state.soc = 50;
    g_state.power = 0;
    g_state.voltage_mv = (uint32_t)(TARGET_PACK_VOLTAGE * 1000);
    g_state.bms_temperature = 298;     // 25°C
    g_state.cell_temperature = 298;
    
    // Initialize all cells to nominal voltage
    for (auto& voltage : g_state.cell_voltages) {
        voltage = NOMINAL_CELL_VOLTAGE;
    }
    
    // Clear balancing (avoid std::vector<bool> iterator issues)
    for (size_t i = 0; i < g_state.balancing_active.size(); ++i) {
        g_state.balancing_active[i] = false;
    }
    
    g_last_update_ms = millis();
    
    ESP_LOGI(TAG, "Test state reset to initial values");
}

// ============================================================================
// Advanced API Implementation
// ============================================================================

void set_values(uint8_t soc, int32_t power, uint32_t voltage) {
    g_state.soc = std::max(0, std::min(100, (int)soc));
    g_state.power = power;
    g_state.voltage_mv = voltage;
    
    ESP_LOGI(TAG, "Test values set: SOC=%u%%, Power=%ld W, Voltage=%lu mV", 
             soc, power, voltage);
}

uint8_t get_cell_count() {
    return g_config.num_cells;
}

uint16_t get_cell_voltage(uint8_t cell_index) {
    if (cell_index >= g_state.cell_voltages.size()) {
        return 0;
    }
    return g_state.cell_voltages[cell_index];
}

bool is_cell_balancing(uint8_t cell_index) {
    if (cell_index >= g_state.balancing_active.size()) {
        return false;
    }
    return g_state.balancing_active[cell_index];
}

void simulate_imbalance(uint16_t spread_mv) {
    // Create voltage spread across cells
    for (size_t i = 0; i < g_state.cell_voltages.size(); i++) {
        // Create triangular pattern: low in middle, high at ends
        float position = (float)i / (g_state.cell_voltages.size() - 1);  // 0.0 to 1.0
        float deviation = 2.0f * std::abs(position - 0.5f);  // 0.0 to 1.0
        
        int16_t voltage_offset = (int16_t)(spread_mv * deviation) - spread_mv / 2;
        g_state.cell_voltages[i] = NOMINAL_CELL_VOLTAGE + voltage_offset;
    }
    
    update_balancing_status();
    ESP_LOGI(TAG, "Imbalance simulated: %u mV spread", spread_mv);
}

void simulate_fault(uint8_t cell_index, uint16_t voltage_mv) {
    if (cell_index == 0xFF) {
        // Clear fault - reset to nominal
        for (auto& voltage : g_state.cell_voltages) {
            voltage = NOMINAL_CELL_VOLTAGE;
        }
        ESP_LOGI(TAG, "Fault condition cleared");
    } else if (cell_index < g_state.cell_voltages.size()) {
        g_state.cell_voltages[cell_index] = voltage_mv;
        ESP_LOGI(TAG, "Fault injected at cell %u: %u mV", cell_index, voltage_mv);
    }
}

const char* get_diagnostics() {
    static char diag_buffer[256];
    
    snprintf(diag_buffer, sizeof(diag_buffer),
             "TestMode: %s | SOC:%u%% PWR:%ld W Volt:%lu mV | "
             "Cells:%u MinV:%u MaxV:%u Dev:%u | Bal:%u",
             g_enabled ? "ON" : "OFF",
             g_state.soc, g_state.power, g_state.voltage_mv,
             g_config.num_cells,
             g_state.min_cell_voltage, g_state.max_cell_voltage, g_state.cell_deviation,
             std::count(g_state.balancing_active.begin(), g_state.balancing_active.end(), true));
    
    return diag_buffer;
}

// ============================================================================
// Internal Helper Functions
// ============================================================================

static void update_cell_voltages() {
    if (g_state.cell_voltages.empty()) {
        return;
    }
    
    // Cells drift towards nominal voltage (self-balancing)
    for (auto& voltage : g_state.cell_voltages) {
        float error = NOMINAL_CELL_VOLTAGE - voltage;
        voltage += (uint16_t)(error * 0.02f);  // 2% convergence per update
    }
    
    // Add small random variation (±10mV) for realism
    for (auto& voltage : g_state.cell_voltages) {
        int16_t variation = (random() % 20) - 10;
        voltage = std::max(2500, std::min(4200, (int)voltage + variation));
    }
    
    // Calculate min/max/deviation
    g_state.min_cell_voltage = *std::min_element(
        g_state.cell_voltages.begin(), g_state.cell_voltages.end());
    g_state.max_cell_voltage = *std::max_element(
        g_state.cell_voltages.begin(), g_state.cell_voltages.end());
    g_state.cell_deviation = g_state.max_cell_voltage - g_state.min_cell_voltage;
}

static void update_balancing_status() {
    // Activate balancing on cells above nominal voltage
    for (size_t i = 0; i < g_state.cell_voltages.size(); i++) {
        g_state.balancing_active[i] = (g_state.cell_voltages[i] > NOMINAL_CELL_VOLTAGE + 50);
    }
}

static void apply_scenario_drift() {
    // Scenarios can apply additional per-cycle adjustments
    // This is called after power/SOC drift to apply scenario-specific effects
    
    switch (g_config.scenario) {
        case TestConfig::SCENARIO_HIGH_TEMPERATURE:
            // Temperature rising - increase cell voltages slightly
            for (auto& voltage : g_state.cell_voltages) {
                voltage += 2;  // +2mV per sample at high temp
            }
            break;
            
        case TestConfig::SCENARIO_FAULT:
            // Progressively lower one cell (simulate growing fault)
            if (!g_state.cell_voltages.empty()) {
                g_state.cell_voltages[0] = std::max(2500, (int)g_state.cell_voltages[0] - 5);
            }
            break;
            
        default:
            // No scenario-specific drift
            break;
    }
}

}  // namespace TestMode
