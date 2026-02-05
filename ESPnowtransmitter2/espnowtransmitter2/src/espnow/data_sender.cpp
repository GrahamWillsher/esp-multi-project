#include "data_sender.h"
#include "message_handler.h"
#include "../config/task_config.h"
#include "../config/logging_config.h"
#include <Arduino.h>
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
    LOG_DEBUG("Data transmission task started");
}

void DataSender::task_impl(void* parameter) {
    LOG_DEBUG("Data sender task running");
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(timing::ESPNOW_SEND_INTERVAL_MS);
    
    while (true) {
        vTaskDelayUntil(&last_wake_time, interval_ticks);
        
        if (EspnowMessageHandler::instance().is_transmission_active()) {
            LOG_TRACE("Sending test data (transmission active)");
            send_test_data_with_led_control();
        } else {
            LOG_TRACE("Skipping send (transmission inactive)");
        }
    }
}

/**
 * @brief Send test data with SOC-based LED flash control
 * 
 * Sends battery data and triggers LED flash on receiver when SOC band changes.
 * SOC range is 20-80%, mapped to thirds:
 * - Low (20-39 SOC = 0-33% normalized): Red LED
 * - Medium (40-59 SOC = 34-66% normalized): Orange LED  
 * - High (60-80 SOC = 67-100% normalized): Green LED
 * 
 * LED flash command is only sent once when transitioning between bands.
 */
void DataSender::send_test_data_with_led_control() {
    // Generate test data (using library's send_test_data logic)
    send_test_data();
    
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
        flash_led_t flash_msg;
        flash_msg.type = msg_flash_led;
        
        // Map band to LED color: 0=red, 1=green, 2=orange
        switch (current_band) {
            case SOCBand::LOW_SOC:
                flash_msg.color = 0;  // Red
                LOG_INFO("SOC band changed to LOW (20-39%%) - Flash RED (SOC: %d)", tx_data.soc);
                break;
            case SOCBand::MEDIUM_SOC:
                flash_msg.color = 2;  // Orange
                LOG_INFO("SOC band changed to MEDIUM (40-59%%) - Flash ORANGE (SOC: %d)", tx_data.soc);
                break;
            case SOCBand::HIGH_SOC:
                flash_msg.color = 1;  // Green
                LOG_INFO("SOC band changed to HIGH (60-80%%) - Flash GREEN (SOC: %d)", tx_data.soc);
                break;
        }
        
        // Send flash LED command to receiver
        esp_err_t result = esp_now_send(receiver_mac, (const uint8_t*)&flash_msg, sizeof(flash_msg));
        if (result == ESP_OK) {
            LOG_DEBUG("Flash LED command sent: color=%d", flash_msg.color);
            last_soc_band = current_band;  // Update tracked band
        } else {
            LOG_ERROR("Failed to send flash LED: %s", esp_err_to_name(result));
        }
    }
}
