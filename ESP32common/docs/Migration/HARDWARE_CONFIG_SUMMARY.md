# Hardware Configuration Summary

**Date**: February 16, 2026  
**Component**: Transmitter Hardware (Olimex ESP32-POE2 + Waveshare RS485/CAN HAT B)

---

## Quick Reference

### Transmitter Hardware Stack
```
┌─────────────────────────────────────────────────┐
│ Olimex ESP32-POE2 (WROVER + PSRAM)             │
│ - Dual-core ESP32-D0WDQ6 @ 240 MHz             │
│ - 520 KB SRAM + 4 MB external PSRAM            │
│ - 4 MB Flash (partitioned for OTA)             │
│ - Built-in Ethernet (W5500 PHY / LAN8720)      │
└──────────────────┬────────────────────────────┘
                   │ SPI bus (5 wires)
                   │ GPIO 18, 23, 19, 5, 32
                   ▼
┌──────────────────────────────────────────────────┐
│ Waveshare RS485/CAN HAT (B)                      │
│ - MCP2515 CAN Controller (SPI slave)             │
│ - TJA1050 CAN Transceiver                        │
│ - SP3485 RS485 Transceiver (optional)            │
│ - 8 MHz Crystal Oscillator                       │
└──────────────────┬───────────────────────────────┘
                   │ CAN Bus (2 wires + GND)
                   ▼
        ┌────────────┬────────────┬────────────┐
        │            │            │            │
    ┌───▼───┐   ┌───▼───┐   ┌───▼───┐
    │  BMS  │   │ CHG   │   │  INV  │
    └───────┘   └───────┘   └───────┘
```

---

## GPIO Pin Mapping (Complete Reference)

### SPI Interface (CAN Controller - MCP2515)
| Signal | GPIO Pin | ESP32 Func | Notes |
|--------|----------|-----------|-------|
| SCLK | 18 | SPI_CLK | Clock signal (10 MHz) |
| MOSI | 23 | SPI_MOSI | Master Out Slave In (data in) |
| MISO | 19 | SPI_MISO | Master In Slave Out (data out) |
| CS | 5 | GPIO5 | Chip Select (active low) |
| INT | 32 | GPIO32 | Interrupt pin (MCP2515 → ESP32) |

**Note**: GPIO 18 (SCLK) and GPIO 23 (MOSI) are also used for Ethernet MDIO/MDC, but only during initialization. After Ethernet initialization, they're available for SPI CAN communication.

### CAN Bus Connectors
| Signal | Type | Notes |
|--------|------|-------|
| CAN_H | CAN | High signal (twisted pair) |
| CAN_L | CAN | Low signal (twisted pair) |
| GND | Ground | Common ground (multiple points) |

**Termination**: 120Ω resistors required at both ends of CAN bus (between CAN_H and CAN_L).

### RS485 Interface (Optional - not currently used)
| Signal | GPIO Pin | Function | Notes |
|--------|----------|----------|-------|
| RX | 16 | UART1_RX | Receiver output |
| TX | 17 | UART1_TX | Transmitter output |
| DE | 25 | GPIO25 | Driver enable (direction control) |

---

## Initialization Sequence

### Step 1: Ethernet (Must be FIRST)
```cpp
// src/network/ethernet_manager.cpp
ETH.begin(
    phy_addr = 0,
    power_pin = 12,
    mdc_pin = 23,      // GPIO 23 (shared with SPI MOSI)
    mdio_pin = 18,     // GPIO 18 (shared with SPI SCLK)
    eth_type = ETH_PHY_LAN8720,
    eth_clk_mode = ETH_CLOCK_GPIO0_OUT
);
```
**Status**: Ethernet initializes, then releases GPIO 18 & 23 for SPI use.

### Step 2: SPI Bus (After Ethernet)
```cpp
// src/communication/can/can_driver.cpp
SPI.begin(
    sclk = 18,     // Now available (Ethernet finished)
    miso = 19,
    mosi = 23,     // Now available (Ethernet finished)
    cs = 5
);
SPI.setFrequency(10000000);  // 10 MHz
```

### Step 3: MCP2515 CAN Controller
```cpp
// src/communication/can/mcp2515_driver.cpp
pinMode(32, INPUT_PULLUP);  // Interrupt pin
mcp2515.reset();
mcp2515.setBitrate(CAN_500KBPS);
mcp2515.setNormalMode();
attachInterrupt(digitalPinToInterrupt(32), can_isr, FALLING);
```

---

## CAN Bus Configuration

### Speed: 500 kbps (Standard)
```cpp
// MCP2515 bit timing for 500 kbps @ 8 MHz crystal
MCP_BITTIME bittime;
bittime.CNF1 = 0x00;  // SJW = 1
bittime.CNF2 = 0x98;  // Prop segment + phase 1
bittime.CNF3 = 0x01;  // Phase 2
mcp2515.setBitrate(CAN_500KBPS);
```

### Alternative: 250 kbps (Lower speed)
```cpp
mcp2515.setBitrate(CAN_250KBPS);  // For long cable runs
```

### CAN Messages Expected (From Battery Emulator BMS/Charger/Inverter)
| Function | CAN ID | Rate | Payload |
|----------|--------|------|---------|
| BMS Status | 0x305 | 10 Hz | SOC, V, I, Temp, Status |
| Charger Status | 0x623 | 1 Hz | HV, LV, Current, Power |
| Inverter Status | 0x351 | 1 Hz | AC V, I, Power, Freq |
| Requests/Config | 0x351 | On-demand | Commands to devices |

**Total CAN Bandwidth**: ~12 bytes/100ms = 960 bits/sec (< 1% of 500 kbps capacity)

---

## Power Requirements

### Supply Voltage: +3.3V DC
| Component | Typical Current | Peak Current | Notes |
|-----------|-----------------|--------------|-------|
| ESP32 (WiFi TX) | ~160 mA | ~320 mA | Highest when transmitting |
| Ethernet (W5500) | ~60 mA | ~80 mA | Active Ethernet I/O |
| MCP2515 (CAN) | ~5 mA | ~10 mA | Low power SPI controller |
| TJA1050 (transceiver) | ~20 mA | ~40 mA | Varies with bus traffic |
| **Total System** | **~250 mA** | **~450 mA** | Typical operation |

**Recommendation**: Use regulated 3.3V supply capable of 500+ mA peak.

### +5V Power (Optional)
- Used for optional isolated power variant of Waveshare HAT
- Current implementation doesn't require +5V

### Ground Connections
- Multiple GND points required for stability
- Separate GND for CAN bus (multi-point grounding)
- Star topology preferred (single common ground point)

---

## Wiring Checklist

### SPI Connections (ESP32 → Waveshare HAT)
- [ ] GPIO 18 (SCLK) → HAT SCLK
- [ ] GPIO 23 (MOSI) → HAT MOSI  
- [ ] GPIO 19 (MISO) → HAT MISO
- [ ] GPIO 5 (CS) → HAT CS
- [ ] GPIO 32 (INT) → HAT INT
- [ ] GND → HAT GND
- [ ] 3.3V → HAT 3.3V (if powered separately)

### CAN Bus (Battery Equipment)
- [ ] CAN_H (from HAT) → BMS CAN_H (twisted pair)
- [ ] CAN_L (from HAT) → BMS CAN_L (twisted pair)
- [ ] GND (common point) ← HAT GND
- [ ] 120Ω terminator at CAN_H ↔ CAN_L (BMS end)
- [ ] 120Ω terminator at CAN_H ↔ CAN_L (Inverter end)

### Ethernet Connection
- [ ] RJ45 to network/DHCP server (or set static IP)
- [ ] PoE power (if using PoE variant)
- [ ] GND isolated or common depending on system

---

## Troubleshooting

### SPI Communication Issues
```
Symptom: MCP2515 not detected
Solution:
  1. Verify GPIO 18, 23 are released by Ethernet (check logs)
  2. Check GPIO 5 CS pin is pulled high (no contention)
  3. Verify 3.3V supply has adequate current
  4. Check SPI frequency (10 MHz is standard)
```

### CAN Bus Issues
```
Symptom: No CAN messages received
Solution:
  1. Verify CAN_H / CAN_L wiring (twisted pair)
  2. Verify 120Ω termination at BOTH ends of bus
  3. Check CAN baudrate matches BMS (500 kbps standard)
  4. Verify GPIO 32 interrupt is wired correctly
  5. Check MCP2515 status register via SPI debug
```

### Ethernet Issues
```
Symptom: Ethernet not initializing
Solution:
  1. Verify Ethernet PHY is detected (check logs at startup)
  2. Verify RJ45 is connected to network
  3. Check power supply (PoE or external 5V)
  4. Verify GPIO 18, 23 are correct (Ethernet MDIO/MDC)
```

---

## Performance Specifications

### CAN Bus Timing
| Operation | Latency | Throughput | Notes |
|-----------|---------|-----------|-------|
| SPI read byte | ~1 µs | 10 Mbps | At 10 MHz SPI clock |
| CAN frame RX | ~2-3 ms | 500 kbps | Propagation + processing |
| CAN frame TX | ~2-3 ms | 500 kbps | Bus access + transmission |
| Interrupt latency | ~5 µs | N/A | ESP32 interrupt response |

### Control Loop Constraint
```
Total CAN I/O time within 10ms control loop:
  - Read 3 CAN messages: ~9 ms (3 × 3 ms propagation)
  - Update datalayer: ~500 µs
  - Total: ~9.5 ms (within budget)

Remaining: ~0.5 ms for control logic (contactors, etc.)
```

---

## References

- **Olimex ESP32-POE2 Manual**: https://www.olimex.com/Products/IoT/ESP32/ESP32-POE2/
- **Waveshare HAT Wiki**: https://www.waveshare.com/wiki/RS485_CAN_HAT_(B)
- **MCP2515 Datasheet**: http://ww1.microchip.com/downloads/en/DeviceDoc/20001801J.pdf
- **ESP-IDF SPI Driver**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html
- **CAN 2.0 Standard**: ISO 11898-1

---

**Configuration Version**: 1.0  
**Last Updated**: February 16, 2026  
**Status**: Ready for implementation
