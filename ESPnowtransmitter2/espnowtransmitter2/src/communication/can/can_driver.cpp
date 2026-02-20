/**
 * @file can_driver.cpp
 * @brief CAN bus driver implementation
 */

#include "can_driver.h"
#include "../../datalayer/datalayer.h"
#include "../../battery/battery_manager.h"
#include "../../config/logging_config.h"
#include <SPI.h>

CANDriver& CANDriver::instance() {
    static CANDriver instance;
    return instance;
}

CANDriver::CANDriver() {
    // Constructor - MCP2515 will be initialized in init()
}

bool CANDriver::init() {
    if (initialized_) {
        LOG_WARN(LOG_TAG, "Already initialized");
        return true;
    }
    
    LOG_INFO(LOG_TAG, "Initializing CAN driver...");
    LOG_INFO(LOG_TAG, "  SCK pin: GPIO %d (HSPI)", CANConfig::SCK_PIN);
    LOG_INFO(LOG_TAG, "  MISO pin: GPIO %d (HSPI)", CANConfig::MISO_PIN);
    LOG_INFO(LOG_TAG, "  MOSI pin: GPIO %d (HSPI)", CANConfig::MOSI_PIN);
    LOG_INFO(LOG_TAG, "  CS pin: GPIO %d", CANConfig::CS_PIN);
    LOG_INFO(LOG_TAG, "  INT pin: GPIO %d", CANConfig::INT_PIN);
    LOG_INFO(LOG_TAG, "  Speed: 500 kbps");
    LOG_INFO(LOG_TAG, "  Clock: 8 MHz");
    
    // Initialize HSPI with explicit pins (no Ethernet conflicts)
    SPI.begin(CANConfig::SCK_PIN, CANConfig::MISO_PIN, CANConfig::MOSI_PIN, CANConfig::CS_PIN);
    
    // Create MCP2515 instance
    mcp2515_ = new MCP2515(CANConfig::CS_PIN);
    
    // Reset MCP2515
    mcp2515_->reset();
    delay(10);  // Wait for reset to complete
    
    // Configure CAN speed
    MCP2515::ERROR result = mcp2515_->setBitrate(CANConfig::SPEED, CANConfig::CLOCK);
    if (result != MCP2515::ERROR_OK) {
        LOG_ERROR(LOG_TAG, "Failed to set CAN bitrate: %d", result);
        delete mcp2515_;
        mcp2515_ = nullptr;
        return false;
    }
    
    // Set normal mode (not loopback, not listen-only)
    result = mcp2515_->setNormalMode();
    if (result != MCP2515::ERROR_OK) {
        LOG_ERROR(LOG_TAG, "Failed to set normal mode: %d", result);
        delete mcp2515_;
        mcp2515_ = nullptr;
        return false;
    }
    
    // Configure interrupt pin
    pinMode(CANConfig::INT_PIN, INPUT_PULLUP);
    
    initialized_ = true;
    LOG_INFO(LOG_TAG, "✓ CAN driver initialized successfully");
    
    return true;
}

void CANDriver::update() {
    if (!initialized_ || !mcp2515_) {
        return;
    }
    
    // Check if messages are available
    // MCP2515 INT pin goes LOW when message received
    if (digitalRead(CANConfig::INT_PIN) == HIGH) {
        return;  // No messages pending
    }
    
    // Process all pending messages
    can_frame frame;
    while (mcp2515_->readMessage(&frame) == MCP2515::ERROR_OK) {
        rx_count_++;
        process_message(frame);
        
        // Update CAN alive counter
        datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
        datalayer.last_can_message_timestamp = millis();
    }
}

void CANDriver::process_message(const can_frame& frame) {
    // Log first message for debugging
    static bool first_message_logged = false;
    if (!first_message_logged) {
        LOG_INFO(LOG_TAG, "First CAN message received: ID=0x%03X, DLC=%d", 
                 frame.can_id, frame.can_dlc);
        first_message_logged = true;
    }
    
    // Route to BMS (Battery Emulator) for parsing
    BatteryManager::instance().process_can_message(frame.can_id, frame.data, frame.can_dlc);
}

bool CANDriver::send(uint32_t id, const uint8_t* data, uint8_t len) {
    if (!initialized_ || !mcp2515_) {
        return false;
    }
    
    if (len > 8) {
        LOG_ERROR(LOG_TAG, "Invalid CAN message length: %d (max 8)", len);
        return false;
    }
    
    can_frame frame;
    frame.can_id = id;
    frame.can_dlc = len;
    memcpy(frame.data, data, len);
    
    MCP2515::ERROR result = mcp2515_->sendMessage(&frame);
    if (result == MCP2515::ERROR_OK) {
        tx_count_++;
        return true;
    } else {
        error_count_++;
        handle_error();
        return false;
    }
}

void CANDriver::handle_error() {
    uint32_t now = millis();
    
    // Rate-limit error logging (max once per second)
    if (now - last_error_time_ms_ > 1000) {
        LOG_ERROR(LOG_TAG, "CAN error detected (total: %u)", error_count_);
        last_error_time_ms_ = now;
    }
}

void CANDriver::reset_counters() {
    error_count_ = 0;
    rx_count_ = 0;
    tx_count_ = 0;
    LOG_INFO(LOG_TAG, "Counters reset");
}
