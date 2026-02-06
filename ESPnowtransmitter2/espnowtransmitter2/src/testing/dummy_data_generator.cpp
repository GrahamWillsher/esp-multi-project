/**
 * Dummy Data Generator for Phase 1-3 Testing
 * 
 * TEMPORARY: Generates realistic battery/charger/inverter data
 * for web UI and ESP-NOW testing before real hardware.
 * 
 * WILL BE REMOVED in Phase 4.
 */

#include "dummy_data_generator.h"
#include <espnow_common.h>
#include <espnow_transmitter.h>
#include "../config/logging_config.h"

namespace DummyData {
    // Task handle
    static TaskHandle_t task_handle = NULL;
    
    // Simulated state variables
    static uint16_t soc = 8000;  // Start at 80.00%
    static int32_t power = 0;     // Start at 0W
    static uint32_t uptime = 0;   // Uptime counter
    
    /**
     * @brief Calculate checksum for message
     */
    static uint16_t calculate_checksum(const void* data, size_t len) {
        const uint8_t* bytes = (const uint8_t*)data;
        uint16_t sum = 0;
        for (size_t i = 0; i < len; i++) {
            sum += bytes[i];
        }
        return sum;
    }
    
    /**
     * @brief Send dummy battery status message
     */
    static void send_battery_status() {
        battery_status_msg_t msg;
        msg.type = msg_battery_status;
        
        // Simulate SOC changing slowly
        static int8_t soc_delta = -1;  // Starts discharging
        soc += soc_delta;
        
        // Bounce at limits
        if (soc <= 2000) { soc_delta = 1; soc = 2000; }  // 20% min
        if (soc >= 9500) { soc_delta = -1; soc = 9500; } // 95% max
        
        msg.soc_percent_100 = soc;
        msg.voltage_mV = 48000 + random(-500, 500);  // 48V ± 0.5V
        msg.current_mA = power * 1000 / (msg.voltage_mV);  // I = P/V
        msg.temperature_dC = 250 + random(-10, 10);  // 25°C ± 1°C
        msg.power_W = power;
        msg.max_charge_power_W = 3000;
        msg.max_discharge_power_W = 5000;
        
        // Set BMS status based on SOC
        if (soc < 2000) {
            msg.bms_status = BMS_FAULT;
        } else if (soc < 3000) {
            msg.bms_status = BMS_WARNING;
        } else {
            msg.bms_status = BMS_OK;
        }
        
        msg.checksum = calculate_checksum(&msg, sizeof(msg) - 2);
        
        esp_now_send(get_broadcast_address(), (uint8_t*)&msg, sizeof(msg));
        LOG_TRACE("[DUMMY] Battery: SOC=%d.%02d%%, V=%dmV, I=%dmA, P=%dW",
                 soc/100, soc%100, msg.voltage_mV, msg.current_mA, msg.power_W);
    }
    
    /**
     * @brief Send dummy battery info message (static data)
     */
    static void send_battery_info() {
        battery_info_msg_t msg;
        msg.type = msg_battery_info;
        msg.total_capacity_Wh = 30000;          // 30kWh battery
        msg.reported_capacity_Wh = 28500;       // 95% of total
        msg.max_design_voltage_dV = 5040;       // 504V
        msg.min_design_voltage_dV = 4200;       // 420V
        msg.max_cell_voltage_mV = 4200;         // 4.2V per cell
        msg.min_cell_voltage_mV = 3000;         // 3.0V per cell
        msg.max_cell_deviation_mV = 50;         // 50mV max deviation
        msg.number_of_cells = 120;              // 120 cells in series
        msg.chemistry = 2;                      // LFP
        msg.checksum = calculate_checksum(&msg, sizeof(msg) - 2);
        
        esp_now_send(get_broadcast_address(), (uint8_t*)&msg, sizeof(msg));
        LOG_DEBUG("[DUMMY] Battery info sent (30kWh, 120 cells, LFP)");
    }
    
    /**
     * @brief Send dummy charger status message
     */
    static void send_charger_status() {
        charger_status_msg_t msg;
        msg.type = msg_charger_status;
        msg.hv_voltage_dV = 4800 + random(-50, 50);    // 480V ± 5V
        msg.hv_current_dA = (power > 0) ? (power * 10 / 480) : 0;  // Only when charging
        msg.lv_voltage_dV = 140 + random(-2, 2);       // 14V ± 0.2V
        msg.lv_current_dA = 50 + random(-5, 5);        // 5A ± 0.5A
        msg.ac_voltage_V = 230 + random(-5, 5);        // 230V ± 5V
        msg.ac_current_dA = (power > 0) ? (power * 10 / 230) : 0;
        msg.power_W = (power > 0) ? power : 0;
        msg.charger_status = (power > 0) ? 1 : 0;      // 1=charging, 0=off
        msg.checksum = calculate_checksum(&msg, sizeof(msg) - 2);
        
        esp_now_send(get_broadcast_address(), (uint8_t*)&msg, sizeof(msg));
        LOG_TRACE("[DUMMY] Charger: %s, HV=%dV/%dA, P=%dW",
                 msg.charger_status ? "CHARGING" : "OFF",
                 msg.hv_voltage_dV/10, msg.hv_current_dA/10, msg.power_W);
    }
    
    /**
     * @brief Send dummy inverter status message
     */
    static void send_inverter_status() {
        inverter_status_msg_t msg;
        msg.type = msg_inverter_status;
        msg.ac_voltage_V = 230 + random(-5, 5);        // 230V ± 5V
        msg.ac_frequency_dHz = 500 + random(-1, 1);    // 50.0Hz ± 0.1Hz
        msg.ac_current_dA = (power < 0) ? (-power * 10 / 230) : 0;
        msg.power_W = (power < 0) ? -power : 0;
        msg.inverter_status = (power < 0) ? 1 : 0;     // 1=on, 0=off
        msg.checksum = calculate_checksum(&msg, sizeof(msg) - 2);
        
        esp_now_send(get_broadcast_address(), (uint8_t*)&msg, sizeof(msg));
        LOG_TRACE("[DUMMY] Inverter: %s, AC=%dV/%dA@%dHz, P=%dW",
                 msg.inverter_status ? "ON" : "OFF",
                 msg.ac_voltage_V, msg.ac_current_dA/10, msg.ac_frequency_dHz/10, msg.power_W);
    }
    
    /**
     * @brief Send dummy system status message
     */
    static void send_system_status() {
        system_status_msg_t msg;
        msg.type = msg_system_status;
        msg.contactor_state = (soc > 3000) ? 0x03 : 0x00;  // Bits 0,1 = positive+negative contactors
        msg.error_flags = (soc < 2000) ? 0x01 : 0x00;      // Bit 0 = low SOC error
        msg.warning_flags = (soc < 3000) ? 0x01 : 0x00;    // Bit 0 = low SOC warning
        msg.uptime_seconds = uptime;
        msg.checksum = calculate_checksum(&msg, sizeof(msg) - 2);
        
        esp_now_send(get_broadcast_address(), (uint8_t*)&msg, sizeof(msg));
        LOG_TRACE("[DUMMY] System: contactors=0x%02X, errors=0x%02X, warnings=0x%02X, uptime=%us",
                 msg.contactor_state, msg.error_flags, msg.warning_flags, msg.uptime_seconds);
    }
    
    /**
     * @brief Dummy data generator task
     */
    static void task(void* parameter) {
        LOG_INFO("[DUMMY] Data generator started (TEMPORARY - will be removed in Phase 4)");
        
        // Send battery info once at startup
        vTaskDelay(pdMS_TO_TICKS(2000));  // Wait for receiver to be ready
        send_battery_info();
        
        TickType_t last_wake = xTaskGetTickCount();
        const TickType_t interval = pdMS_TO_TICKS(200);  // 200ms = 5Hz
        uint8_t cycle = 0;
        
        while (true) {
            // Simulate power changing (oscillates between -2000W and +1500W)
            static float power_phase = 0.0;
            power_phase += 0.05;  // Slow oscillation
            power = (int32_t)(sin(power_phase) * 1750.0);  // -1750W to +1750W
            
            // Send status messages (staggered to avoid bursts)
            send_battery_status();
            vTaskDelay(pdMS_TO_TICKS(20));
            
            send_charger_status();
            vTaskDelay(pdMS_TO_TICKS(20));
            
            send_inverter_status();
            vTaskDelay(pdMS_TO_TICKS(20));
            
            send_system_status();
            
            // Increment uptime every 5 cycles (1 second)
            cycle++;
            if (cycle >= 5) {
                uptime++;
                cycle = 0;
            }
            
            // Wait for next cycle (not strict timing)
            vTaskDelayUntil(&last_wake, interval);
        }
    }
    
    void start(uint8_t priority, uint8_t core) {
        if (task_handle != NULL) {
            LOG_WARN("[DUMMY] Data generator already running");
            return;
        }
        
        xTaskCreatePinnedToCore(
            task,
            "DummyData",
            4096,           // Stack size
            NULL,
            priority,
            &task_handle,
            core
        );
        
        LOG_INFO("[DUMMY] Data generator task created (Priority %d, Core %d)", priority, core);
    }
    
    void stop() {
        if (task_handle != NULL) {
            vTaskDelete(task_handle);
            task_handle = NULL;
            LOG_INFO("[DUMMY] Data generator stopped");
        }
    }
    
    bool is_running() {
        return (task_handle != NULL);
    }
}
