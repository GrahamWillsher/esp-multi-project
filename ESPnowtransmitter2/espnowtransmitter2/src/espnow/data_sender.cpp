#include "data_sender.h"
#include "message_handler.h"
#include "enhanced_cache.h"  // Section 11: Dual storage cache (replaces data_cache)
#include "../config/task_config.h"
#include "../config/logging_config.h"
#include "../datalayer/datalayer.h"  // Phase 4a: Real battery data
#include "../battery_emulator/test_data_generator.h"  // Test data for development
#include "../test_mode/test_mode.h"  // Phase 1: Test mode
#include "../network/transmission_selector.h"  // Phase 2: Smart transmission routing
#include <Arduino.h>
#include <connection_manager.h>
#include <espnow_transmitter.h>

// SOC band tracking for LED flash control
// SOC range is 20-80%, divided into thirds for visual feedback
enum class SOCBand : uint8_t {
    LOW_SOC,      // 20-39 SOC (0-33% normalized) -> Red
    MEDIUM_SOC,   // 40-59 SOC (34-66% normalized) -> Orange
    HIGH_SOC      // 60-80 SOC (67-100% normalized) -> Green
};

static SOCBand last_soc_band = SOCBand::MEDIUM_SOC;  // Track previous band to detect changes

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
    
    // NOTE: Test data generator will auto-initialize on first update() call
    // This ensures battery setup() has already run and configured number_of_cells
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(timing::ESPNOW_SEND_INTERVAL_MS);
    
    while (true) {
        vTaskDelayUntil(&last_wake_time, interval_ticks);
        
        // Phase 1: Generate test data if test mode is enabled
        if (TestMode::is_enabled()) {
            TestMode::generate_sample();  // Advance internal state for realistic drift
        }
        // Phase 4a: Update real battery data from datalayer (if CAN is enabled and test mode is off)
        else if (TestDataGenerator::is_enabled()) {
            TestDataGenerator::update();
        }
        
        if (EspnowMessageHandler::instance().is_transmission_active()) {
            LOG_TRACE("DATA_SENDER", "Sending data (transmission active, mode: %s)", 
                     TestMode::is_enabled() ? "TEST" : "LIVE");
            send_test_data_with_led_control();
        } else {
            LOG_TRACE("DATA_SENDER", "Skipping send (transmission inactive)");
        }
    }
}

/**
 * @brief Send battery data (test or live) with SOC-based LED flash control
 * 
 * Phase 1: Determines data source (live vs test) based on test mode flag
 * Phase 4a: Uses real datalayer from CAN bus when in live mode
 * 
 * Section 11 Architecture: ALWAYS cache-first (non-blocking)
 * - Data flows through EnhancedCache regardless of connection state
 * - Background transmission task handles sending from cache
 * - Non-blocking: < 100µs cache write (doesn't block Battery Emulator)
 * 
 * Sends battery data and triggers LED flash on receiver when SOC band changes.
 * 
 * SOC range is 20-80%, mapped to thirds:
 * - Low (20-39 SOC = 0-33% normalized): Red LED
 * - Medium (40-59 SOC = 34-66% normalized): Orange LED  
 * - High (60-80 SOC = 67-100% normalized): Green LED
 * 
 * LED flash command is only sent once when transitioning between bands.
 */
void DataSender::send_test_data_with_led_control() {
    // Phase 1: Select data source (test vs live)
    if (TestMode::is_enabled()) {
        // Use test mode data
        const TestMode::TestState& test_state = TestMode::get_current_state();
        tx_data.soc = test_state.soc;
        tx_data.power = test_state.power;
        LOG_TRACE("DATA_SENDER", "Using TEST data: SOC:%d%%, Power:%dW", 
                 tx_data.soc, tx_data.power);
    } else {
        // Phase 4a: Read real battery data from datalayer
        // Convert from datalayer format (pptt = percent * 100) to simple percentage
        uint16_t soc_pptt = datalayer.battery.status.reported_soc;  // e.g., 8000 = 80.00%
        uint8_t soc_percent = soc_pptt / 100;  // Convert to 0-100 range
        
        int32_t power_w = datalayer.battery.status.active_power_W;
        
        // Populate tx_data structure from datalayer
        tx_data.soc = soc_percent;
        tx_data.power = power_w;
        LOG_TRACE("DATA_SENDER", "Using LIVE data: SOC:%d%%, Power:%dW", 
                 tx_data.soc, tx_data.power);
    }
    
    tx_data.checksum = calculate_checksum(&tx_data);
    
    // Section 11: ALWAYS write to cache first (cache-centric pattern)
    // Background transmission task will handle sending from cache
    if (EnhancedCache::instance().add_transient(tx_data)) {
        LOG_TRACE("DATA_SENDER", "Data cached (SOC:%d%%, Power:%dW)", 
                 tx_data.soc, tx_data.power);
        
        // Phase 2: Report dynamic data to transmission selector (enables dual ESP-NOW/MQTT support)
        // Transmission selector will intelligently route small frequent updates via fastest method
        char timestamp_str[32];
        snprintf(timestamp_str, sizeof(timestamp_str), "%lu", millis());
        auto result = TransmissionSelector::transmit_dynamic_data(tx_data.soc, tx_data.power, timestamp_str);
        if (result.espnow_sent) {
            LOG_TRACE("DATA_SENDER", "Dynamic data sent via %s", result.method);
        }
    } else {
        // Cache write failed (mutex timeout or overflow)
        // Data dropped - doesn't block control code
        LOG_WARN("DATA_SENDER", "Cache write failed (timeout/overflow) - data dropped");
        return;
    }
    
    // Determine current SOC band (20-80 range mapped to thirds)
    SOCBand current_band;
    if (tx_data.soc < 40) {
        current_band = SOCBand::LOW_SOC;      // 20-39: Lower third (RED)
    } else if (tx_data.soc < 60) {
        current_band = SOCBand::MEDIUM_SOC;   // 40-59: Middle third (ORANGE)
    } else {
        current_band = SOCBand::HIGH_SOC;     // 60-80: Upper third (GREEN)
    }
    
    // Send flash LED command only when band changes
    if (current_band != last_soc_band) {
        // Check ESP-NOW health before sending LED command
        if (!is_espnow_healthy()) {
            LOG_DEBUG("DATA_SENDER", "Skipping LED flash - ESP-NOW experiencing delivery failures");
            return;
        }
        
        flash_led_t flash_msg;
        flash_msg.type = msg_flash_led;
        
        // Map band to LED color: 0=red, 1=green, 2=orange
        switch (current_band) {
            case SOCBand::LOW_SOC:
                flash_msg.color = 0;  // Red
                LOG_INFO("DATA_SENDER", "SOC band changed to LOW (20-39%%) - Flash RED (SOC: %d)", tx_data.soc);
                break;
            case SOCBand::MEDIUM_SOC:
                flash_msg.color = 2;  // Orange
                LOG_INFO("DATA_SENDER", "SOC band changed to MEDIUM (40-59%%) - Flash ORANGE (SOC: %d)", tx_data.soc);
                break;
            case SOCBand::HIGH_SOC:
                flash_msg.color = 1;  // Green
                LOG_INFO("DATA_SENDER", "SOC band changed to HIGH (60-80%%) - Flash GREEN (SOC: %d)", tx_data.soc);
                break;
        }
        
        // Send flash LED command to receiver
        const uint8_t* peer_mac = EspNowConnectionManager::instance().get_peer_mac();
        esp_err_t result = esp_now_send(peer_mac, (const uint8_t*)&flash_msg, sizeof(flash_msg));
        if (result == ESP_OK) {
            LOG_DEBUG("DATA_SENDER", "Flash LED command sent: color=%d", flash_msg.color);
            last_soc_band = current_band;  // Update tracked band
        } else {
            LOG_ERROR("DATA_SENDER", "Failed to send flash LED: %s", esp_err_to_name(result));
        }
    }
}
