#include "test_data_generator.h"
#include "../datalayer/datalayer.h"
#include "../config/logging_config.h"
#include "../test_data/test_data_config.h"
#include <Arduino.h>

namespace TestDataGenerator {

static bool initialized = false;
static bool enabled = false;  // Runtime control (replaces compile-time flag)
static bool cell_generation_enabled = true;  // Control cell voltage generation
static uint32_t last_update_ms = 0;
static const uint32_t UPDATE_INTERVAL_MS = 100;

// Simulation state
static float soc_target = 65.0;
static bool soc_increasing = true;
static float power_cycle = 0.0;
static uint32_t cycle_count = 0;

void init() {
    if (initialized) return;
    
    LOG_INFO("TEST_DATA", "========== INIT() CALLED ==========");
    LOG_INFO("TEST_DATA", "number_of_cells BEFORE init: %u", datalayer.battery.info.number_of_cells);
    LOG_INFO("TEST_DATA", "Initializing test data generator (NO REAL CAN BUS)");
    
    // Preserve battery info values set by battery setup() function
    // Only set values that haven't been configured yet (== 0)
    
    if (datalayer.battery.info.total_capacity_Wh == 0) {
        datalayer.battery.info.total_capacity_Wh = 75000;  // 75 kWh default
    }
    if (datalayer.battery.info.max_design_voltage_dV == 0) {
        datalayer.battery.info.max_design_voltage_dV = 4200;  // 420V nominal
    }
    if (datalayer.battery.info.min_design_voltage_dV == 0) {
        datalayer.battery.info.min_design_voltage_dV = 3000;  // 300V minimum
    }
    if (datalayer.battery.info.max_cell_voltage_mV == 0) {
        datalayer.battery.info.max_cell_voltage_mV = 3650;
    }
    if (datalayer.battery.info.min_cell_voltage_mV == 0) {
        datalayer.battery.info.min_cell_voltage_mV = 2800;
    }
    
    // CRITICAL: Respect battery's configured cell count (Nissan Leaf = 96, etc.)
    // Only use default if battery hasn't set it yet
    if (datalayer.battery.info.number_of_cells == 0) {
        datalayer.battery.info.number_of_cells = 108;  // 108S default for generic battery
        LOG_WARN("TEST_DATA", "No battery cell count configured, using default: 108 cells");
    } else {
        LOG_INFO("TEST_DATA", "Using battery's configured cell count: %u cells", 
                 datalayer.battery.info.number_of_cells);
    }
    
    LOG_INFO("TEST_DATA", "number_of_cells AFTER init: %u", datalayer.battery.info.number_of_cells);
    
    if (datalayer.battery.info.chemistry == battery_chemistry_enum::Autodetect) {
        datalayer.battery.info.chemistry = battery_chemistry_enum::NMC;
    }
    
    if (datalayer.battery.info.chemistry == battery_chemistry_enum::Autodetect) {
        datalayer.battery.info.chemistry = battery_chemistry_enum::NMC;
    }
    
    // Initialize battery status (dynamic) based on actual cell count
    uint16_t cell_count = datalayer.battery.info.number_of_cells;
    uint16_t nominal_voltage_dV = (cell_count * 36);  // 3.6V per cell average
    
    datalayer.battery.status.remaining_capacity_Wh = datalayer.battery.info.total_capacity_Wh * 0.65;  // 65% SOC
    datalayer.battery.status.real_soc = 6500;  // 65.00% (pptt format)
    datalayer.battery.status.reported_soc = 6500;
    datalayer.battery.status.voltage_dV = nominal_voltage_dV;  // Based on cell count
    datalayer.battery.status.current_dA = 0;  // 0A initially
    datalayer.battery.status.active_power_W = 0;
    datalayer.battery.status.temperature_min_dC = 180;  // 18°C
    datalayer.battery.status.temperature_max_dC = 220;  // 22°C
    datalayer.battery.status.cell_max_voltage_mV = 3610;
    datalayer.battery.status.cell_min_voltage_mV = 3580;
    datalayer.battery.status.max_charge_power_W = 11000;  // 11kW max charge
    datalayer.battery.status.max_discharge_power_W = 15000;  // 15kW max discharge
    
    // BMS is active since we're generating data
    datalayer.battery.status.real_bms_status = BatteryEmulator_real_bms_status_enum::BMS_ACTIVE;
    datalayer.battery.status.bms_status = ACTIVE;
    
    initialized = true;
    LOG_INFO("TEST_DATA", "✓ Test data initialized: %uWh, %uS, SOC=65%%",
             datalayer.battery.info.total_capacity_Wh,
             datalayer.battery.info.number_of_cells);
}

void update() {
    // CRITICAL: Always initialize on first call, regardless of enabled state
    // This ensures correct cell count is captured from battery configuration
    if (!initialized) {
        init();
        if (!enabled) return;  // If disabled after init, don't update
        // If enabled and just initialized, fall through to update
    }
    
    if (!enabled) return;  // Don't update if disabled
    
    uint32_t now = millis();
    if (now - last_update_ms < UPDATE_INTERVAL_MS) {
        return;  // Update every 100ms
    }
    last_update_ms = now;
    cycle_count++;
    
    // ===== SOC Simulation (slow cycling 20-95%) =====
    if (soc_increasing) {
        soc_target += 0.02;  // ~0.02% per update = ~12% per minute
        if (soc_target >= 95.0) {
            soc_target = 95.0;
            soc_increasing = false;
        }
    } else {
        soc_target -= 0.03;  // Discharge slightly faster
        if (soc_target <= 20.0) {
            soc_target = 20.0;
            soc_increasing = true;
        }
    }
    
    datalayer.battery.status.real_soc = (uint16_t)(soc_target * 100);
    datalayer.battery.status.reported_soc = datalayer.battery.status.real_soc;
    
    // ===== Voltage Simulation (follows SOC: 300-420V) =====
    // Linear approximation: 300V @ 0% → 420V @ 100%
    float voltage_v = 300.0 + (soc_target * 1.2);
    datalayer.battery.status.voltage_dV = (uint16_t)(voltage_v * 10);
    
    // ===== Power Simulation (sine wave: -5kW to +3kW) =====
    power_cycle += 0.02;  // Slow oscillation
    float power_normalized = sin(power_cycle);  // -1 to +1
    
    // Map to charging (-5kW) to discharging (+3kW) range
    int32_t power_w;
    if (power_normalized < 0) {
        power_w = (int32_t)(power_normalized * 5000);  // Charging up to -5000W
    } else {
        power_w = (int32_t)(power_normalized * 3000);  // Discharging up to +3000W
    }
    datalayer.battery.status.active_power_W = power_w;
    
    // Current calculation: I = P / V
    float current_a = (voltage_v > 0) ? (power_w / voltage_v) : 0;
    datalayer.battery.status.current_dA = (int16_t)(current_a * 10);
    
    // ===== Temperature Simulation (18-28°C with small variations) =====
    float temp_min_c = 18.0 + (sin(power_cycle * 0.3) * 2.0);  // 16-20°C
    float temp_max_c = 22.0 + (sin(power_cycle * 0.5) * 3.0);  // 19-25°C
    datalayer.battery.status.temperature_min_dC = (int16_t)(temp_min_c * 10);
    datalayer.battery.status.temperature_max_dC = (int16_t)(temp_max_c * 10);
    
    // ===== Cell Voltage Simulation =====
    // Average cell voltage = total_voltage / cell_count
    uint16_t cell_count = datalayer.battery.info.number_of_cells;
    float avg_cell_v = voltage_v / (float)cell_count;
    float cell_spread_mv = 30.0;  // 30mV spread between min/max
    datalayer.battery.status.cell_max_voltage_mV = (uint16_t)((avg_cell_v + cell_spread_mv/2000.0) * 1000);
    datalayer.battery.status.cell_min_voltage_mV = (uint16_t)((avg_cell_v - cell_spread_mv/2000.0) * 1000);
    
    // Populate individual cell voltages (required for MQTT JSON serialization)
    // Each cell gets the average voltage +/- small random deviation
    uint16_t avg_cell_mv = (uint16_t)(avg_cell_v * 1000);
    for (uint16_t i = 0; i < cell_count && i < MAX_AMOUNT_CELLS; i++) {
        // Each cell gets slightly different voltage for realism
        int16_t deviation_mv = (int16_t)((i % 10) - 5) * 5;  // -25 to +25 mV variation
        datalayer.battery.status.cell_voltages_mV[i] = avg_cell_mv + deviation_mv;
        datalayer.battery.status.cell_balancing_status[i] = false;  // No balancing in test mode
    }
    
    // ===== Update remaining capacity =====
    datalayer.battery.status.remaining_capacity_Wh = 
        (uint32_t)((soc_target / 100.0) * datalayer.battery.info.total_capacity_Wh);
    
    // ===== Log every 10 seconds =====
    if (cycle_count % 100 == 0) {
        LOG_DEBUG("TEST_DATA", "SOC=%.1f%%, V=%.1fV, I=%.1fA, P=%dW, T=%.1f-%.1f°C",
                 soc_target, voltage_v, current_a, power_w,
                 temp_min_c, temp_max_c);
    }
}

bool is_enabled() {
    return enabled;
}

void set_enabled(bool new_enabled) {
    enabled = new_enabled;
    LOG_INFO("TEST_DATA", "Test data generator %s", enabled ? "ENABLED" : "DISABLED");
}

bool is_cell_generation_enabled() {
    return cell_generation_enabled;
}

void set_cell_generation_enabled(bool new_enabled) {
    cell_generation_enabled = new_enabled;
    LOG_INFO("TEST_DATA", "Cell generation %s", cell_generation_enabled ? "ENABLED" : "DISABLED");
}

void reinitialize() {
    initialized = false;
    init();
    LOG_INFO("TEST_DATA", "Test data generator reinitialized");
}

} // namespace TestDataGenerator
