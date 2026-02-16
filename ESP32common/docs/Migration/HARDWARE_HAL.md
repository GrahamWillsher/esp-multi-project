# Hardware Abstraction Layer (HAL) - Overview & Architecture

## Quick Reference

This project uses **three boards**, each with distinct hardware and responsibilities:

| Board | Role | Hardware | Network | CAN |
|-------|------|----------|---------|-----|
| **Transmitter** | Battery control & data aggregation | Olimex ESP32-POE2 + Waveshare CAN HAT | Ethernet + WiFi | ✓ MCP2515 |
| **Receiver** | Display & monitoring hub | LilyGo T-Display-S3 | WiFi only | ✗ No |
| **Common** | Shared libraries & configs | N/A (software only) | N/A | N/A |

---

## For Detailed Hardware Information

- **Transmitter specifics**: [HARDWARE_HAL_TRANSMITTER.md](HARDWARE_HAL_TRANSMITTER.md)
- **Receiver specifics**: [HARDWARE_HAL_RECEIVER.md](HARDWARE_HAL_RECEIVER.md)

This document provides the overview and cross-board context.

---

## Hardware Specifications

### Olimex ESP32-POE2 (Transmitter)
- **Processor**: Dual-core ESP32 (240 MHz)
- **RAM**: 520 KB SRAM + 4 MB PSRAM (WROVER)
- **Flash**: 4 MB (partitioned for OTA)
- **Networking**: Built-in Ethernet (LAN8720 PHY via RMII interface)
- **Pins Available**: 30 GPIO pins (some shared with Ethernet)

### Waveshare RS485/CAN HAT (B)
- **CAN Controller**: MCP2515 (SPI interface)
- **CAN Transceiver**: TJA1050
- **RS485 IC**: SP3485 (half-duplex)
- **Crystal**: 8 MHz oscillator
- **Features**: 
  - CAN 2.0B/CAN FD support
  - Daisy-chain support (multiple HATs)
  - Isolated power option

---

## GPIO Pin Mapping

### SPI Bus (CAN Interface - MCP2515)
| Function | GPIO Pin | Notes |
|----------|----------|-------|
| SPI_CLK (SCLK) | GPIO 18 | CAN clock signal |
| SPI_MOSI (SDI) | GPIO 23 | CAN data in (Master Out Slave In) |
| SPI_MISO (SDO) | GPIO 19 | CAN data out (Master In Slave Out) |
| SPI_CS (NSS) | GPIO 5 | Chip select for MCP2515 |

**Note**: GPIO 23 is shared with Ethernet MDIO. Configuration handled by `hardware_config.h`.

### CAN Interrupt & Control
| Function | GPIO Pin | Notes |
|----------|----------|-------|
| MCP2515 Interrupt | GPIO 32 | INT pin from Waveshare HAT |
| Crystal Enable | GPIO 33 (Optional) | Crystal oscillator enable (if needed) |

### RS485 Serial (Half-Duplex)
| Function | GPIO Pin | Notes |
|----------|----------|-------|
| RS485_RX (RO) | GPIO 16 | Receiver output (data in) |
| RS485_TX (DI) | GPIO 17 | Driver input (data out) |
| RS485_DE (enable) | GPIO 25 | Driver enable (high=TX, low=RX) |

**Note**: RS485 is optional; CAN bus is primary for battery communication.

### Power & Ground
| Signal | Pin | Notes |
|--------|-----|-------|
| +3.3V | 3.3V rail | Waveshare HAT power |
| +5V | 5V rail | Optional (if using isolated power) |
| GND | GND rail | Common ground (multiple pins) |

---

## Hardware Configuration Code

### `src/config/hardware_config.h`
```cpp
#pragma once

namespace hardware {
    // ===== ETHERNET CONFIGURATION (Olimex ESP32-POE2, RMII Interface) =====
    constexpr int PHY_ADDR = 0;
    constexpr int ETH_POWER_PIN = 12;
    constexpr int ETH_MDC_PIN = 23;        // RMII management: Shared with SPI_MOSI
    constexpr int ETH_MDIO_PIN = 18;       // RMII management: Shared with SPI_SCLK
    
    // ===== CAN INTERFACE (Waveshare RS485/CAN HAT B via SPI) =====
    // MCP2515 SPI Configuration
    constexpr int CAN_SPI_CLK = 18;        // GPIO18 = SCLK (also ETH_MDIO)
    constexpr int CAN_SPI_MOSI = 23;       // GPIO23 = MOSI (also ETH_MDC)
    constexpr int CAN_SPI_MISO = 19;       // GPIO19 = MISO
    constexpr int CAN_SPI_CS = 5;          // GPIO5  = NSS (Chip Select)
    constexpr int CAN_INT = 32;            // GPIO32 = MCP2515 Interrupt
    constexpr int CAN_SPI_FREQ = 10000000; // 10 MHz SPI clock
    
    // CAN Bus Configuration
    constexpr uint32_t CAN_BAUDRATE_500K = 500000;  // Standard CAN speed
    constexpr uint32_t CAN_BAUDRATE_250K = 250000;  // Alternative speed
    
    // ===== RS485 CONFIGURATION (Optional Serial) =====
    // Half-duplex RS485 on UART1
    constexpr int RS485_RX = 16;           // GPIO16 = RO (receive)
    constexpr int RS485_TX = 17;           // GPIO17 = DI (transmit)
    constexpr int RS485_DE = 25;           // GPIO25 = DE (driver enable)
    constexpr int RS485_BAUD = 115200;    // Serial baud rate
    
} // namespace hardware

// SPI Pin Mapping Note:
// The same pins (GPIO18, GPIO23) are used for both:
// 1. Ethernet MDIO/MDC (PHY communication)
// 2. SPI CLK/MOSI (CAN interface)
//
// This is possible because:
// - Ethernet PHY (LAN8720) only uses MDIO/MDC during initialization
// - After initialization, these pins can be reused for SPI
// - SPI CAN communication is managed independently
//
// CRITICAL: Ethernet must be initialized FIRST before SPI CAN bus
```

### Hardware Configuration in `platformio.ini`
```ini
[env:olimex_esp32_poe2]
platform = espressif32@6.5.0
board = esp32-poe2
framework = arduino

; Board configuration flags
build_flags =
    ; ... existing flags ...
    
    ; Hardware identifiers
    -D HW_OLIMEX_ESP32_POE2
    -D HW_WAVESHARE_RS485_CAN_HAT_B
    
    ; Pin definitions (optional - already in hardware_config.h)
    -D CAN_CS_PIN=5
    -D CAN_INT_PIN=32

; Critical board settings
board_build.f_cpu = 240000000L      ; CPU frequency
board_build.f_flash = 80000000L     ; Flash frequency
board_build.flash_mode = qio         ; Quad I/O for speed
board_build.partitions = partitions_4mb_ota.csv
board_build.arduino.memory_type = qio_qspi  ; PSRAM settings
```

---

## Initialization Sequence

### 1. Ethernet (First Priority - must initialize before SPI CAN)
```cpp
// src/network/ethernet_manager.cpp

void EthernetManager::begin() {
    // Initialize Ethernet PHY (LAN8720) via RMII interface
    // Uses GPIO 18 (MDIO), GPIO 23 (MDC) for management during init
    // After init: these management pins can be shared with SPI
    
    ETH.begin(
        phy_addr = hardware::PHY_ADDR,
        power_pin = hardware::ETH_POWER_PIN,
        mdc_pin = hardware::ETH_MDC_PIN,      // Management: RMII MDC
        mdio_pin = hardware::ETH_MDIO_PIN,    // Management: RMII MDIO
        eth_type = ETH_PHY_LAN8720,
        eth_clk_mode = ETH_CLOCK_GPIO0_IN     // RMII 50MHz clock input
    );
}
```

### 2. SPI Bus (After Ethernet initialization)
```cpp
// src/communication/can/can_driver.cpp

void CANDriver::begin() {
    // Initialize SPI bus (reusing Ethernet pins after init)
    SPI.begin(
        sclk = hardware::CAN_SPI_CLK,      // GPIO18 (after Ethernet init)
        miso = hardware::CAN_SPI_MISO,      // GPIO19
        mosi = hardware::CAN_SPI_MOSI,      // GPIO23 (after Ethernet init)
        cs = hardware::CAN_SPI_CS           // GPIO5
    );
    
    SPI.setFrequency(hardware::CAN_SPI_FREQ);  // 10 MHz
}
```

### 3. CAN Controller (MCP2515)
```cpp
// src/communication/can/mcp2515_driver.cpp

void MCP2515Driver::begin() {
    // Initialize MCP2515 CAN controller
    // Assumes SPI bus already initialized
    
    pinMode(hardware::CAN_INT, INPUT_PULLUP);  // Interrupt pin
    
    // Initialize MCP2515 with desired CAN speed
    mcp2515.reset();
    mcp2515.setBitrate(CAN_500KBPS);  // or CAN_250KBPS
    mcp2515.setNormalMode();
    
    // Enable interrupt for message reception
    attachInterrupt(
        digitalPinToInterrupt(hardware::CAN_INT),
        can_interrupt_handler,
        FALLING
    );
}
```

### 4. RS485 Serial (Optional - if needed)
```cpp
// src/communication/rs485/rs485_driver.cpp

void RS485Driver::begin() {
    // Initialize UART1 for RS485 communication
    Serial1.begin(
        hardware::RS485_BAUD,
        SERIAL_8N1,
        hardware::RS485_RX,
        hardware::RS485_TX
    );
    
    // Configure driver enable pin
    pinMode(hardware::RS485_DE, OUTPUT);
    digitalWrite(hardware::RS485_DE, LOW);  // Start in RX mode
}

void RS485Driver::transmit_packet(const uint8_t* data, size_t length) {
    digitalWrite(hardware::RS485_DE, HIGH);  // Enable transmitter
    Serial1.write(data, length);
    Serial1.flush();
    digitalWrite(hardware::RS485_DE, LOW);   // Return to receiver
}
```

---

## CAN Bus Topology

### Single Device (Current Configuration)
```
┌─────────────────────────────────────┐
│  Olimex ESP32-POE2 (Transmitter)   │
│  - Runs control loop (10ms)         │
│  - Reads BMS/Charger/Inverter data │
│  - Sends commands to devices        │
└──────────────┬──────────────────────┘
               │
              SPI
        ┌─────┴─────┐
        │ (5 wires) │
        │ SCLK/MOSI/MISO/CS/INT
        │           │
    ┌───▼───────────▼────┐
    │ Waveshare HAT (B)  │
    │ - MCP2515 CAN ctrl │
    │ - TJA1050 transceiver
    └─────────┬──────────┘
              │
           CAN Bus
              │
    ┌─────────┼─────────┐
    │         │         │
 ┌──▼──┐  ┌──▼──┐  ┌──▼──┐
 │ BMS │  │CHG  │  │INV  │
 └─────┘  └─────┘  └─────┘
```

### Future: Daisy-Chain (Multiple HATs)
```
Olimex ESP32-POE2
       │
      SPI ────► HAT 1 (MCP2515)
       │         │
      CS1        ├─► CAN Bus 1 (BMS)
       │         │
      CS2        └─► CAN Bus 2 (Charger)
       │
       ├────► HAT 2 (MCP2515)
       │         │
      CS3        └─► CAN Bus 3 (Inverter)
```

**Note**: Daisy-chaining requires separate CS pins per HAT (GPIO5, GPIO4, GPIO2 available).

---

## CAN Message Configuration

### BMS Messages (Example: LiFePO4 Battery)
```cpp
// CAN ID 0x305 - BMS Status (from BMS → Transmitter)
struct {
    uint16_t soc;              // State of Charge (%)
    int16_t  voltage;          // Pack voltage (V)
    int16_t  current;          // Charge/discharge current (A)
    int8_t   temp_min;         // Min cell temperature (°C)
    int8_t   temp_max;         // Max cell temperature (°C)
    uint8_t  status;           // Fault flags
} bms_status;

// Rate: 10 Hz (100 ms) from BMS to transmitter
// Transmitter → Receiver: Low-rate summary via ESP-NOW (1-5 Hz)
```

### Charger Messages (Example: Victron/Meanwell)
```cpp
// CAN ID 0x623 - Charger Status (from Charger → Transmitter)
struct {
    uint16_t hv_voltage;       // High voltage DC (V)
    uint16_t lv_voltage;       // Low voltage DC (V)
    int16_t  current;          // Charging current (A)
    uint16_t power;            // Power (W)
    uint8_t  state;            // Charger state
} charger_status;

// Rate: 1 Hz (1 sec) from charger to transmitter
```

### Inverter Messages (Example: Pylontech/Victron)
```cpp
// CAN ID 0x351 - Inverter Status (from Inverter → Transmitter)
struct {
    uint16_t ac_voltage;       // AC output voltage (V)
    int16_t  ac_current;       // AC output current (A)
    int16_t  ac_power;         // AC power (W)
    uint16_t ac_frequency;     // AC frequency (Hz)
    uint8_t  state;            // Inverter state
} inverter_status;

// Rate: 1 Hz (1 sec) from inverter to transmitter
```

---

## Timing Constraints

### Critical Path (Must maintain 10ms)
```
┌──────────────────────────────────────────────────┐
│ CAN Interrupt Handler (in ISR)                   │
├──────────────────────────────────────────────────┤
│ 1. Read MCP2515 status register    [~50 µs]     │
│ 2. Identify RX buffer with message [~10 µs]     │
│ 3. Copy message to queue           [~20 µs]     │
│ 4. Signal main task                [~5 µs]      │
└──────────────────────────────────────────────────┘
                    Total: ~85 µs (non-blocking)

┌──────────────────────────────────────────────────┐
│ Main Control Loop (10 ms = 10,000 µs)           │
├──────────────────────────────────────────────────┤
│ 1. Read CAN messages from queue    [~100 µs]    │
│ 2. Update datalayer                [~500 µs]    │
│ 3. Run control logic (contactors)  [~200 µs]    │
│ 4. Write CAN messages (commands)   [~300 µs]    │
│ 5. SSE/MQTT (background queue)     [ASYNC]      │
└──────────────────────────────────────────────────┘
                Total: ~1.1 ms (well under 10 ms)

Remaining time (~8.9 ms): Available for:
- Background tasks (MQTT, logging)
- SPI reads/writes
- No blocking operations in control loop
```

### CAN Bus Timing
| Operation | Latency | Notes |
|-----------|---------|-------|
| SPI read (1 byte) | ~1 µs | At 10 MHz SPI clock |
| SPI read (8 bytes) | ~8 µs | Typical CAN frame |
| MCP2515 interrupt → ISR | ~5 µs | ESP32 interrupt latency |
| CAN frame transmission | ~2-3 ms | At 500 kbps, 8 bytes |
| CAN frame reception | ~2-3 ms | Propagation time |

---

## Testing & Validation

### Hardware Checklist
- [ ] Verify SPI connections (CLK, MOSI, MISO, CS, INT)
- [ ] Verify CAN bus termination (120Ω resistors on both ends)
- [ ] Verify power supply (3.3V, adequate current)
- [ ] Verify GND connections (multiple points for stability)

### Software Checklist
- [ ] Ethernet initializes successfully
- [ ] SPI bus communicates with MCP2515
- [ ] CAN messages received from BMS
- [ ] CAN messages received from charger
- [ ] CAN messages received from inverter
- [ ] Transmitter sends control commands on CAN
- [ ] Control loop maintains 10ms timing

### Debugging Tools
```cpp
// Check SPI connectivity
void test_spi_communication() {
    MCP2515 mcp(hardware::CAN_SPI_CS);
    if (mcp.reset()) {
        LOG_INFO("SPI", "MCP2515 detected on SPI bus");
    } else {
        LOG_ERROR("SPI", "MCP2515 NOT detected - check connections!");
    }
}

// Check CAN bus activity
void test_can_bus() {
    can_frame frame;
    if (mcp2515.readMessage(&frame)) {
        LOG_INFO("CAN", "Received ID: 0x%03X, DLC: %d", frame.can_id, frame.can_dlc);
    } else {
        LOG_WARN("CAN", "No CAN messages received - check termination/power");
    }
}

// Check interrupt pin
void test_can_interrupt() {
    digitalWrite(hardware::CAN_INT, LOW);   // Simulate interrupt
    delayMicroseconds(10);
    digitalWrite(hardware::CAN_INT, HIGH);
    LOG_INFO("INT", "Interrupt pin test complete");
}
```

---

## Future Enhancements

### 1. Multiple CAN Buses (Daisy-chain HATs)
- Add GPIO4, GPIO2 as additional CS pins
- Support 3x independent CAN buses
- Allows isolation of BMS/Charger/Inverter traffic

### 2. CAN FD (Flexible Data Rate)
- Waveshare HAT (B) supports CAN FD
- Requires firmware update to MCP2515 firmware
- Allows 64-byte frames instead of 8-byte

### 3. Isolated CAN (Optional Power Isolation)
- Use isolated RS485/CAN HAT variant
- Protects against ground loops
- Useful for multi-source power systems

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
