# Hardware Abstraction Layer (HAL) - Transmitter (ESP32-POE2 + CAN)

## Overview

The **transmitter** uses a **Waveshare RS485/CAN HAT (B)** for battery system communication via CAN bus, combined with Ethernet for network connectivity.

**Hardware Stack**:
- **MCU**: Olimex ESP32-POE2 (WROVER variant with PSRAM)
- **CAN Interface**: Waveshare RS485/CAN HAT (B)
- **Network**: Built-in Ethernet (Integrated PHY)
- **Connection**: SPI bus from ESP32-POE2 to Waveshare HAT

---

## Hardware Specifications

### Olimex ESP32-POE2 (Transmitter MCU)
- **Processor**: Dual-core ESP32 (240 MHz, Xtensa LX6)
- **RAM**: 520 KB SRAM + 4 MB PSRAM (WROVER variant)
- **Flash**: 4 MB (partitioned for OTA: 1.5 MB app + 1.5 MB OTA + config)
- **Networking**: Built-in Ethernet (LAN8720 PHY via RMII interface)
- **GPIO Pins Available**: 30 GPIO pins (some shared with Ethernet/SPI)
- **Power**: PoE (Power over Ethernet) via integrated jack + USB backup
- **Operating Temp**: -20°C to +60°C
- **Form Factor**: 32-pin DIP-like module on breakout board

### Waveshare RS485/CAN HAT (B)
- **CAN Controller**: MCP2515 (SPI interface, 10 Mbps peak)
- **CAN Transceiver**: TJA1050 (3.3V, differential bus signaling)
- **RS485 IC**: SP3485 (half-duplex, 3.3V)
- **Crystal**: 8 MHz oscillator (external for MCP2515 timing)
- **Features**: 
  - CAN 2.0B compatible (500 kbps standard)
  - CAN FD support (with firmware)
  - Daisy-chain support (multiple HATs via GPIO expansion)
  - Isolated power option (variant available)
- **Connector**: DB-9 female (CAN), 3.5mm screw terminal (RS485)

---

## GPIO Pin Mapping

### SPI Bus (CAN Interface - MCP2515)

| Function | GPIO Pin | Alternative | Notes |
|----------|----------|-------------|-------|
| SPI_CLK (SCLK) | GPIO 18 | ETH_MDIO | CAN clock signal, 10 MHz |
| SPI_MOSI (SDI) | GPIO 23 | ETH_MDC | CAN data in, Ethernet MDC (multiplexed) |
| SPI_MISO (SDO) | GPIO 19 | — | CAN data out (input to ESP32) |
| SPI_CS (NSS) | GPIO 5 | — | Chip select for MCP2515 active-low |

**Multiplexing Note**: GPIO 18 and 23 are shared between Ethernet (MDIO/MDC) and CAN (SPI_CLK/MOSI). Initialization order is critical — configure Ethernet PHY BEFORE enabling CAN SPI.

### CAN Interrupt & Control

| Function | GPIO Pin | Type | Notes |
|----------|----------|------|-------|
| MCP2515 Interrupt (INT) | GPIO 32 | Input | Active-low interrupt, wakeup-capable |
| Crystal Enable (Optional) | GPIO 33 | Output | Crystal oscillator control (if needed) |

### RS485 Serial (Half-Duplex UART1)

| Function | GPIO Pin | Alternative | Notes |
|----------|----------|-------------|-------|
| RS485_RX (RO) | GPIO 16 | UART1_RX | Receiver output (data in to ESP32) |
| RS485_TX (DI) | GPIO 17 | UART1_TX | Driver input (data out from ESP32) |
| RS485_DE (Enable) | GPIO 25 | — | Driver enable (HIGH=TX, LOW=RX) |

**Status**: RS485 optional; CAN bus is primary for battery communication.

### Ethernet (RMII Interface - LAN8720 PHY)

| Function | GPIO Pin | Notes |
|----------|----------|-------|
| ETH_MDC | GPIO 23 | Shared with SPI_MOSI (see multiplexing) |
| ETH_MDIO | GPIO 18 | Shared with SPI_CLK (see multiplexing) |
| ETH_TX_EN | GPIO 21 | Transmit enable |
| ETH_TXD0 | GPIO 26 | Transmit data 0 (RMII) |
| ETH_TXD1 | GPIO 27 | Transmit data 1 (RMII) |
| ETH_RX_EN | GPIO 28 | Receive enable (CRS_DV on RMII) |
| ETH_RXD0 | GPIO 29 | Receive data 0 (RMII) |
| ETH_RXD1 | GPIO 30 | Receive data 1 (RMII) |

**RMII Note**: Reduced Media Independent Interface (RMII) uses fewer pins than MII (8 pins vs 16), suitable for embedded systems. Requires 50 MHz reference clock (provided by LAN8720).

### Power & Ground

| Signal | Pin | Current | Notes |
|--------|-----|---------|-------|
| +3.3V | 3.3V rail | 500 mA | Waveshare HAT power, MCU I/O voltage |
| +5V | 5V rail | 1 A | Optional, for isolated power variant |
| GND | GND rail | Multiple pins | Common ground, must be solid |

---

## Hardware Configuration Code

### `src/config/hardware_config.h`

```cpp
#pragma once

namespace hardware {
    // ===== ETHERNET CONFIGURATION (Olimex ESP32-POE2) =====
    // Built-in Ethernet, no SPI required (parallel RMII interface)
    constexpr int PHY_ADDR = 0;              // PHY address on MDIO bus
    constexpr int ETH_POWER_PIN = 12;        // Power enable (active-high)
    constexpr int ETH_MDC_PIN = 23;          // MDIO clock (also SPI_MOSI!)
    constexpr int ETH_MDIO_PIN = 18;         // MDIO data (also SPI_CLK!)
    
    // ===== CAN INTERFACE (Waveshare RS485/CAN HAT B via SPI) =====
    // MCP2515 SPI Configuration
    constexpr int CAN_SPI_CLK = 18;          // GPIO18 = SCLK (shares ETH_MDIO)
    constexpr int CAN_SPI_MOSI = 23;         // GPIO23 = MOSI (shares ETH_MDC)
    constexpr int CAN_SPI_MISO = 19;         // GPIO19 = MISO (dedicated)
    constexpr int CAN_SPI_CS = 5;            // GPIO5  = NSS (dedicated)
    constexpr int CAN_INT = 32;              // GPIO32 = MCP2515 Interrupt
    constexpr int CAN_SPI_FREQ = 10000000;   // 10 MHz SPI clock
    
    // CAN Bus Configuration
    constexpr uint32_t CAN_BAUDRATE_500K = 500000;   // Standard CAN speed
    constexpr uint32_t CAN_BAUDRATE_250K = 250000;   // Alternative speed (lower EMI)
    
    // ===== RS485 CONFIGURATION (Optional Serial) =====
    // Half-duplex RS485 on UART1 (optional, not primary)
    constexpr int RS485_RX = 16;             // GPIO16 = RO (receive output)
    constexpr int RS485_TX = 17;             // GPIO17 = DI (driver input)
    constexpr int RS485_DE = 25;             // GPIO25 = DE (driver enable)
    constexpr int RS485_BAUD = 115200;       // Serial baud rate
    
    // ===== FLASH / PSRAM CONFIGURATION =====
    // Auto-detected by esp-idf, but documented for clarity
    constexpr int FLASH_SIZE_MB = 4;
    constexpr int PSRAM_SIZE_MB = 4;
}
```

### `src/communication/can/can_config.h`

```cpp
#pragma once

#include "../config/hardware_config.h"

namespace can {
    // MCP2515 Bit Timing Configuration (500 kbps @ 8 MHz crystal)
    // Allows 16 time quanta per bit for robust timing
    struct BitTimingConfig {
        uint8_t sjw;      // Synchronization Jump Width
        uint8_t ps1;      // Propagation Segment 1
        uint8_t ps2;      // Propagation Segment 2
    };
    
    // Standard CAN speeds
    constexpr BitTimingConfig TIMING_500K = {1, 6, 7};   // 500 kbps
    constexpr BitTimingConfig TIMING_250K = {1, 13, 14}; // 250 kbps
    
    // Message IDs for Battery Equipment (CAN 2.0B, 11-bit ID)
    enum MessageID : uint16_t {
        BMS_STATUS      = 0x305,   // Battery Management System data
        BMS_CELL_DATA   = 0x306,   // Cell voltage details
        CHARGER_STATUS  = 0x623,   // Charger status and feedback
        INVERTER_STATUS = 0x351,   // Inverter output status
        GATEWAY_CMD     = 0x100,   // Commands TO battery equipment
    };
    
    // Filter configuration (accept relevant messages)
    constexpr uint16_t FILTER_MASK = 0x7FF;  // 11-bit mask
    constexpr uint16_t FILTER_0 = 0x300;     // Accept 0x300-0x3FF (BMS)
    constexpr uint16_t FILTER_1 = 0x620;     // Accept 0x620-0x62F (Charger)
    
    // Error handling
    constexpr uint32_t CAN_RX_TIMEOUT_MS = 100;  // No message = stale
    constexpr uint32_t CAN_TX_TIMEOUT_MS = 50;   // Transmit timeout
}
```

---

## Initialization Sequence

**Critical**: GPIO 18/23 are shared between Ethernet and CAN. Initialization order matters.

```cpp
// src/main.cpp - Initialization order

void setup() {
    // Step 1: Initialize Ethernet FIRST (claims GPIO 18/23)
    if (!initialize_ethernet()) {
        LOG_ERROR("ETH", "Ethernet init failed!");
        // Halt or retry
    }
    
    // Step 2: Initialize CAN (now safe to use GPIO 18/23 for SPI)
    if (!initialize_can()) {
        LOG_ERROR("CAN", "CAN init failed!");
        // Retry or continue with limited functionality
    }
    
    // Step 3: Start main control loop
    xTaskCreate(battery_control_loop, "BatteryControl", 4096, nullptr, 5, nullptr);
}

// Ethernet initialization (simplified)
bool initialize_ethernet() {
    // Configure PHY (LAN8720 via RMII interface)
    // Ethernet driver handles GPIO multiplexing automatically
    esp_eth_config_t eth_config = ETH_CONFIG_DEFAULT();
    eth_config.phy_addr = hardware::PHY_ADDR;  // Usually 0
    eth_config.clock_mode = ETH_CLOCK_GPIO0_IN;  // RMII clock input
    
    esp_eth_handle_t eth_handle;
    
    if (esp_eth_driver_install(&eth_config, &eth_handle) != ESP_OK) {
        return false;
    }
    
    // Start Ethernet (RMII interface)
    return esp_eth_start(eth_handle) == ESP_OK;
}

// CAN initialization
bool initialize_can() {
    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = hardware::CAN_SPI_MOSI,
        .miso_io_num = hardware::CAN_SPI_MISO,
        .sclk_io_num = hardware::CAN_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    
    if (spi_bus_initialize(VSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        return false;
    }
    
    // Configure MCP2515 device
    spi_device_interface_config_t dev_cfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,
        .clock_speed_hz = hardware::CAN_SPI_FREQ,
        .spics_io_num = hardware::CAN_SPI_CS,
        .queue_size = 7,
    };
    
    // Initialize MCP2515 (requires library integration)
    // ... MCP2515 setup code ...
    
    return true;
}
```

---

## Testing & Diagnostics

### SPI Communication Test

```cpp
#include <MCP2515.h>

void test_spi_communication() {
    // Test MCP2515 responds to SPI reads
    MCP2515 mcp(hardware::CAN_SPI_CS);
    
    if (mcp.reset()) {
        LOG_INFO("SPI", "✓ MCP2515 detected on SPI bus");
    } else {
        LOG_ERROR("SPI", "✗ MCP2515 NOT detected - check connections!");
        LOG_ERROR("SPI", "  - Verify GPIO %d (CS) connection", hardware::CAN_SPI_CS);
        LOG_ERROR("SPI", "  - Verify GPIO %d (CLK) not shorted to ETH", hardware::CAN_SPI_CLK);
        LOG_ERROR("SPI", "  - Verify GPIO %d (MOSI) not shorted to ETH", hardware::CAN_SPI_MOSI);
        LOG_ERROR("SPI", "  - Verify 3.3V and GND connections");
    }
}
```

### CAN Bus Activity Test

```cpp
void test_can_bus_activity() {
    // Check if CAN messages are being received
    can_frame frame;
    int message_count = 0;
    
    for (int i = 0; i < 100; i++) {
        if (mcp2515.readMessage(&frame)) {
            message_count++;
            LOG_INFO("CAN", "Msg %d: ID=0x%03X DLC=%d Data[0]=0x%02X",
                     message_count, frame.can_id, frame.can_dlc, frame.data[0]);
        }
        delay(10);
    }
    
    if (message_count == 0) {
        LOG_WARN("CAN", "✗ No CAN messages received in 1 second!");
        LOG_WARN("CAN", "  - Check CAN bus termination (120Ω resistors at ends)");
        LOG_WARN("CAN", "  - Check CAN power (12V or 24V depending on equipment)");
        LOG_WARN("CAN", "  - Verify CAN transceiver IC (TJA1050) powered");
        LOG_WARN("CAN", "  - Check CAN bus wiring (twisted pair)");
    } else {
        LOG_INFO("CAN", "✓ CAN bus active: received %d messages", message_count);
    }
}
```

### Interrupt Test

```cpp
void test_can_interrupt() {
    pinMode(hardware::CAN_INT, INPUT);
    
    // Simulate message reception by toggling INT pin
    volatile bool interrupt_fired = false;
    
    attachInterrupt(hardware::CAN_INT, [](){ interrupt_fired = true; }, FALLING);
    
    // Force INT low for 10µs
    digitalWrite(hardware::CAN_INT, LOW);
    delayMicroseconds(10);
    digitalWrite(hardware::CAN_INT, HIGH);
    
    delay(10);  // Allow ISR to fire
    
    if (interrupt_fired) {
        LOG_INFO("INT", "✓ Interrupt pin test passed");
    } else {
        LOG_ERROR("INT", "✗ Interrupt pin not responding - check GPIO %d", hardware::CAN_INT);
    }
}
```

---

## Troubleshooting

### SPI Clock Conflicts
**Problem**: CAN initialization fails immediately
**Cause**: GPIO 18/23 shared with Ethernet, both trying to use SPI_CLK/MOSI
**Solution**: 
- Ensure Ethernet initialized first (claims pins)
- Use dedicated GPIO for CAN (GPIO 19, 5 are safe)
- Check ESP-IDF Ethernet driver configuration

### CAN Messages Not Received
**Problem**: No messages on CAN bus, MCP2515 responding
**Cause**: CAN transceiver not powered or bus not terminated
**Solution**:
- Verify Waveshare HAT +3.3V power
- Check CAN bus termination: 120Ω resistor between CAN_H and CAN_L at each end
- Verify CAN equipment powered (12V or 24V DC)
- Check differential voltage on CAN bus (nominal ±2V)

### Intermittent SPI Errors
**Problem**: Occasional CAN timeouts or garbled data
**Cause**: SPI clock too fast or noise on shared GPIO
**Solution**:
- Reduce SPI clock from 10 MHz to 5 MHz: `constexpr int CAN_SPI_FREQ = 5000000;`
- Add shielding to SPI wires
- Increase CAN_RX_TIMEOUT_MS for slower buses

### Ethernet Not Working
**Problem**: Ethernet fails, CAN works fine
**Cause**: GPIO 18/23 multiplexing conflict
**Solution**:
- Verify Ethernet initialization before CAN
- Check PHY address (usually 0, can be 1)
- Verify Ethernet connector wired correctly

---

## Future Enhancements

### 1. Multiple CAN Buses (Daisy-chain)
- Add GPIO4, GPIO2 as additional CS pins
- Support 3x independent CAN buses
- Isolate BMS/Charger/Inverter traffic

### 2. CAN FD (Flexible Data Rate)
- Waveshare HAT (B) supports CAN FD
- Enable 64-byte frames instead of 8-byte
- Requires firmware on MCP2515

### 3. Isolated CAN
- Use isolated RS485/CAN HAT variant
- Protects against ground loops
- For multi-source power systems

### 4. Redundant CAN Bus
- Secondary CAN bus for safety-critical signals
- Dual channels for BMS status
- Automatic failover on error

---

## References

- **Olimex ESP32-POE2**: https://www.olimex.com/Products/IoT/ESP32/ESP32-POE2/
- **Waveshare RS485/CAN HAT (B)**: https://www.waveshare.com/wiki/RS485_CAN_HAT_(B)
- **MCP2515 Datasheet**: http://ww1.microchip.com/downloads/en/DeviceDoc/20001801J.pdf
- **TJA1050 Datasheet**: https://www.nxp.com/docs/en/data-sheet/TJA1050.pdf
- **CAN 2.0 Specification**: ISO 11898-1

---

**Document Version**: 1.0  
**Created**: February 16, 2026  
**Hardware Tested**: Olimex ESP32-POE2 + Waveshare RS485/CAN HAT (B)  
**Scope**: Transmitter only
