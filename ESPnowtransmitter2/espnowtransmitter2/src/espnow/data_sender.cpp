#include "data_sender.h"
#include "message_handler.h"
#include "tx_state_machine.h"
#include "enhanced_cache.h"  // Section 11: Dual storage cache (replaces data_cache)
#include "../config/task_config.h"
#include "../config/logging_config.h"
#include <esp32common/config/timing_config.h>
#include "../datalayer/datalayer.h"  // Phase 4a: Real battery data
#include "../battery_emulator/test_data_generator.h"  // Test data for development
#include "../test_data/test_data_config.h"  // Phase 2: Runtime test data configuration
#include "../network/transmission_selector.h"  // Phase 2: Smart transmission routing
#include <Arduino.h>
#include <espnow_transmitter.h>

DataSender& DataSender::instance() {
    static DataSender instance;
    return instance;
}

void DataSender::start() {
    xTaskCreate(
        task_impl,
        "task_data",
        task_config::STACK_SIZE_DATA_SENDER,
        NULL,
        task_config::PRIORITY_NORMAL,
        NULL
    );
    LOG_DEBUG("DATA_SENDER", "Data transmission task started");
}

void DataSender::task_impl(void* parameter) {
    LOG_DEBUG("DATA_SENDER", "Data sender task running");
    
    // NOTE: Test data generator auto-initializes on first update() call
    // This ensures battery setup() has already run and configured number_of_cells
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(TimingConfig::ESPNOW_SEND_INTERVAL_MS);
    auto& state_machine = TxStateMachine::instance();
    
    while (true) {
        vTaskDelayUntil(&last_wake_time, interval_ticks);
        
        // Phase 2: Update test data if enabled (runtime configuration)
        const bool test_mode_enabled = TestDataGenerator::is_enabled();
        if (test_mode_enabled) {
            TestDataGenerator::update();
        }
        
        if (state_machine.is_transmission_active()) {
            const char* mode_str = test_mode_enabled ? "TEST" : "LIVE";
            LOG_TRACE("DATA_SENDER", "Sending data (transmission active, mode: %s)", mode_str);
            send_battery_data();
        } else {
            static uint32_t last_inactive_log_ms = 0;
            uint32_t now = millis();
            if (now - last_inactive_log_ms > 5000) {
                last_inactive_log_ms = now;
                LOG_WARN("DATA_SENDER", "Transmission inactive - no ESP-NOW data being sent");
            }
        }
    }
}

/**
 * @brief Send battery data (test or live)
 * 
 * Phase 1: Determines data source (live vs test) based on test mode flag
 * Phase 4a: Uses real datalayer from CAN bus when in live mode
 * 
 * Section 11 Architecture: ALWAYS cache-first (non-blocking)
 * - Data flows through EnhancedCache regardless of connection state
 * - Background transmission task handles sending from cache
 * - Non-blocking: < 100µs cache write (doesn't block Battery Emulator)
 * 
 * LED updates are published separately by the event/status pipeline.
 */
void DataSender::send_battery_data() {
    auto& cache = EnhancedCache::instance();

    // Phase 2: Read battery data from datalayer (live or test depending on configuration)
    // Convert from datalayer format (pptt = percent * 100) to simple percentage
    uint16_t soc_pptt = datalayer.battery.status.reported_soc;  // e.g., 8000 = 80.00%
    uint8_t soc_percent = soc_pptt / 100;  // Convert to 0-100 range
    
    int32_t power_w = datalayer.battery.status.active_power_W;
    
    // Populate tx_data structure from datalayer
    tx_data.type = msg_data;  // Set message type for receiver routing
    tx_data.soc = soc_percent;
    tx_data.power = power_w;
    
    const char* mode_str = TestDataGenerator::is_enabled() ? "TEST" : "LIVE";
    LOG_TRACE("DATA_SENDER", "Using %s data: SOC:%d%%, Power:%dW", mode_str,
             tx_data.soc, tx_data.power);
    
    tx_data.checksum = calculate_checksum(&tx_data);
    
    // Section 11: ALWAYS write to cache first (cache-centric pattern)
    // Background transmission task will handle sending from cache
    if (cache.add_transient(tx_data)) {
        LOG_TRACE("DATA_SENDER", "Data cached (SOC:%d%%, Power:%dW)", 
                 tx_data.soc, tx_data.power);
        
        // Phase 2: Record route selection for dynamic data (selector is advisory/planning)
        char timestamp_str[32];
        snprintf(timestamp_str, sizeof(timestamp_str), "%lu", millis());
        auto result = TransmissionSelector::transmit_dynamic_data(tx_data.soc, tx_data.power, timestamp_str);
        if (result.espnow_sent) {
            LOG_TRACE("DATA_SENDER", "Dynamic data route selected: %s", result.method);
        }
    } else {
        // Cache write failed (mutex timeout or overflow)
        // Data dropped - doesn't block control code
        LOG_WARN("DATA_SENDER", "Cache write failed (timeout/overflow) - data dropped");
        return;
    }
}
