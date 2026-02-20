#include "test_data_generator.h"
#include "../datalayer/datalayer.h"
#include "../config/logging_config.h"
#include <Arduino.h>

// Enable test data generator via compile flag (comment out for production)
#define TEST_DATA_GENERATOR_ENABLED

namespace TestDataGenerator {

static bool initialized = false;
static uint32_t last_update_ms = 0;
static const uint32_t UPDATE_INTERVAL_MS = 100;

// Simulation state
static float soc_target = 65.0;
static bool soc_increasing = true;
static float power_cycle = 0.0;
static uint32_t cycle_count = 0;

void init() {
#ifdef TEST_DATA_GENERATOR_ENABLED
    if (initialized) return;
    
    LOG_INFO("TEST_DATA", "Initializing test data generator (NO REAL CAN BUS)");
    
    // Initialize battery info (static)
    datalayer.battery.info.total_capacity_Wh = 75000;  // 75 kWh battery
    datalayer.battery.info.max_design_voltage_dV = 4200;  // 420V nominal
    datalayer.battery.info.min_design_voltage_dV = 3000;  // 300V minimum
    datalayer.battery.info.max_cell_voltage_mV = 3650;
    datalayer.battery.info.min_cell_voltage_mV = 2800;
    datalayer.battery.info.number_of_cells = 108;  // 108S for ~390V nominal
    datalayer.battery.info.chemistry = battery_chemistry_enum::NMC;
    
    // Initialize battery status (dynamic)
    datalayer.battery.status.remaining_capacity_Wh = 48750;  // 65% SOC
    datalayer.battery.status.real_soc = 6500;  // 65.00% (pptt format)
    datalayer.battery.status.reported_soc = 6500;
    datalayer.battery.status.voltage_dV = 3900;  // 390V
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
    LOG_INFO("TEST_DATA", "✓ Test data initialized: 75kWh, 108S NMC, SOC=65%%");
#endif
}

void update() {
#ifdef TEST_DATA_GENERATOR_ENABLED
    if (!initialized) {
        init();
        return;
    }
    
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
    float avg_cell_v = voltage_v / 108.0;
    float cell_spread_mv = 30.0;  // 30mV spread between min/max
    datalayer.battery.status.cell_max_voltage_mV = (uint16_t)((avg_cell_v + cell_spread_mv/2000.0) * 1000);
    datalayer.battery.status.cell_min_voltage_mV = (uint16_t)((avg_cell_v - cell_spread_mv/2000.0) * 1000);
    
    // ===== Update remaining capacity =====
    datalayer.battery.status.remaining_capacity_Wh = 
        (uint32_t)((soc_target / 100.0) * datalayer.battery.info.total_capacity_Wh);
    
    // ===== Log every 10 seconds =====
    if (cycle_count % 100 == 0) {
        LOG_DEBUG("TEST_DATA", "SOC=%.1f%%, V=%.1fV, I=%.1fA, P=%dW, T=%.1f-%.1f°C",
                 soc_target, voltage_v, current_a, power_w,
                 temp_min_c, temp_max_c);
    }
#endif
}

bool is_enabled() {
#ifdef TEST_DATA_GENERATOR_ENABLED
    return true;
#else
    return false;
#endif
}

} // namespace TestDataGenerator
