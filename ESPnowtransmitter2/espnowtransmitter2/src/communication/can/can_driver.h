/**
 * @file can_driver.h
 * @brief CAN bus driver using MCP2515 controller
 * 
 * Hardware: Waveshare RS485/CAN HAT (MCP2515 + TJA1050)
 * Connection: SPI interface (HSPI) on Olimex ESP32-POE2
 * 
 * GPIO Configuration:
 * - SCK: GPIO 14 (HSPI clock - no conflict with Ethernet)
 * - MOSI: GPIO 13 (HSPI data out - no conflict with Ethernet)
 * - MISO: GPIO 4 (HSPI data in - safe GPIO, no Ethernet conflicts)
 * - CS: GPIO 15 (Chip Select)
 * - INT: GPIO 32 (MCP2515 interrupt)
 * 
 * IMPORTANT GPIO CONFLICT RESOLUTION:
 * - GPIO 19 CANNOT be used (conflicts with Ethernet EMAC_TXD0)
 * - GPIO 12 is used by Ethernet PHY power
 * - GPIO 4 is safe and available for MISO
 * 
 * Ethernet RMII Interface (Reserved GPIOs):
 * - GPIO 0: EMAC_CLK_OUT (50MHz clock output)
 * - GPIO 12: PHY_POWER (power enable for LAN8720)
 * - GPIO 18: MDIO (management data I/O)
 * - GPIO 19: EMAC_TXD0 (transmit data 0) ⚠️ CONFLICTS with default MISO
 * - GPIO 21: EMAC_TX_EN (transmit enable)
 * - GPIO 22: EMAC_TXD1 (transmit data 1)
 * - GPIO 23: MDC (management data clock)
 * - GPIO 25: EMAC_RXD0 (receive data 0)
 * - GPIO 26: EMAC_RXD1 (receive data 1)
 * - GPIO 27: EMAC_CRS_DV (carrier sense)
 * 
 * CAN SPI Bus (Safe GPIOs):
 * - GPIO 4: MISO (no conflicts)
 * - GPIO 13: MOSI (no conflicts)
 * - GPIO 14: SCK (no conflicts)
 * - GPIO 15: CS (no conflicts)
 * - GPIO 32: INT (no conflicts)
 */

#pragma once

#include <Arduino.h>
#include <mcp2515.h>

// CAN configuration
namespace CANConfig {
    // GPIO pins - HSPI bus (no conflicts with Ethernet RMII)
    constexpr uint8_t SCK_PIN = 14;  // HSPI clock
    constexpr uint8_t MISO_PIN = 4;  // HSPI data in (GPIO 4 - safe, no Ethernet conflicts)
    constexpr uint8_t MOSI_PIN = 13; // HSPI data out
    constexpr uint8_t CS_PIN = 15;   // Chip select
    constexpr uint8_t INT_PIN = 32;  // Interrupt
    
    // CAN bus speed
    constexpr CAN_SPEED SPEED = CAN_500KBPS;
    constexpr CAN_CLOCK CLOCK = MCP_8MHZ;  // Crystal frequency on HAT
    
    // Message processing
    constexpr uint16_t RX_BUFFER_SIZE = 32;
    constexpr uint32_t RX_TIMEOUT_MS = 10;
}

/**
 * @brief CAN bus driver singleton
 */
class CANDriver {
public:
    /**
     * @brief Get singleton instance
     */
    static CANDriver& instance();
    
    /**
     * @brief Initialize CAN driver
     * 
     * MUST be called AFTER Ethernet initialization to avoid GPIO conflicts
     * 
     * @return true if initialized successfully
     */
    bool init();
    
    /**
     * @brief Update CAN driver (process incoming messages)
     * 
     * Call this regularly from main loop or dedicated task
     */
    void update();
    
    /**
     * @brief Send CAN message
     * @param id CAN message ID
     * @param data Message data buffer
     * @param len Data length (0-8 bytes)
     * @return true if sent successfully
     */
    bool send(uint32_t id, const uint8_t* data, uint8_t len);
    
    /**
     * @brief Check if CAN is initialized and ready
     * @return true if ready for communication
     */
    bool is_ready() const { return initialized_; }
    
    /**
     * @brief Get error counter
     * @return Number of errors since last reset
     */
    uint32_t get_error_count() const { return error_count_; }
    
    /**
     * @brief Get received message counter
     * @return Number of messages received since last reset
     */
    uint32_t get_rx_count() const { return rx_count_; }
    
    /**
     * @brief Get transmitted message counter
     * @return Number of messages sent since last reset
     */
    uint32_t get_tx_count() const { return tx_count_; }
    
    /**
     * @brief Reset error and message counters
     */
    void reset_counters();
    
private:
    CANDriver();
    ~CANDriver() = default;
    
    // Prevent copying
    CANDriver(const CANDriver&) = delete;
    CANDriver& operator=(const CANDriver&) = delete;
    
    /**
     * @brief Process received CAN message
     * @param frame Received CAN frame
     */
    void process_message(const can_frame& frame);
    
    /**
     * @brief Handle CAN errors
     */
    void handle_error();
    
    // MCP2515 controller instance
    MCP2515* mcp2515_ = nullptr;
    
    // State
    bool initialized_ = false;
    uint32_t last_error_time_ms_ = 0;
    
    // Statistics
    uint32_t error_count_ = 0;
    uint32_t rx_count_ = 0;
    uint32_t tx_count_ = 0;
    
    // Logging tag
    const char* LOG_TAG = "CAN_DRIVER";
};
