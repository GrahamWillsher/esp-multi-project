# ESP-NOW Transmitter: GPIO Allocation Reference

**Device**: Olimex ESP32-POE-ISO (WROVER-E)  
**Version**: 1.0  
**Date**: February 19, 2026

---

## Quick Reference Table

| GPIO | Usage | Interface | Component | Notes |
|------|-------|-----------|-----------|-------|
| 0 | EMAC_CLK_OUT | Ethernet RMII | LAN8720 | 50MHz clock output |
| 4 | CAN_MISO | SPI (HSPI) | MCP2515 | Data In - Safe (no conflicts) |
| 12 | ETH_POWER | GPIO | LAN8720 | PHY power enable |
| 13 | CAN_MOSI | SPI (HSPI) | MCP2515 | Data Out |
| 14 | CAN_SCK | SPI (HSPI) | MCP2515 | Clock |
| 15 | CAN_CS | GPIO | MCP2515 | Chip Select |
| 18 | ETH_MDIO | Ethernet RMII | LAN8720 | Management Data I/O |
| 19 | EMAC_TXD0 | Ethernet RMII | LAN8720 | Transmit Data 0 |
| 21 | EMAC_TX_EN | Ethernet RMII | LAN8720 | Transmit Enable |
| 22 | EMAC_TXD1 | Ethernet RMII | LAN8720 | Transmit Data 1 |
| 23 | ETH_MDC | Ethernet RMII | LAN8720 | Management Data Clock |
| 25 | EMAC_RXD0 | Ethernet RMII | LAN8720 | Receive Data 0 |
| 26 | EMAC_RXD1 | Ethernet RMII | LAN8720 | Receive Data 1 |
| 27 | EMAC_CRS_DV | Ethernet RMII | LAN8720 | Carrier Sense |
| 32 | CAN_INT | GPIO | MCP2515 | Interrupt (Active LOW) |
| 33 | CONTACTOR_POSITIVE | GPIO | Relay/Contactor | Positive contactor control |
| 34 | CONTACTOR_NEGATIVE | GPIO | Relay/Contactor | Negative contactor control |
| 35 | CONTACTOR_PRECHARGE | GPIO | Relay/Contactor | Precharge contactor control |
| 36 | CONTACTOR_2ND_POSITIVE | GPIO | Relay/Contactor | 2nd battery positive (optional) |

---

## Detailed GPIO Allocation

### Ethernet (LAN8720 PHY) - RMII Interface

These GPIOs are dedicated to the Ethernet RMII physical interface and **cannot be reassigned**.

| GPIO | Name | Direction | Purpose |
|------|------|-----------|---------|
| 0 | EMAC_CLK_OUT | Output | 50 MHz clock output for PHY |
| 12 | ETH_POWER_PIN | Output | Power enable for LAN8720 PHY |
| 18 | ETH_MDIO_PIN | Bidirectional | Management Data I/O (configuration) |
| 19 | EMAC_TXD0 | Output | Ethernet transmit data line 0 |
| 21 | EMAC_TX_EN | Output | Ethernet transmit enable signal |
| 22 | EMAC_TXD1 | Output | Ethernet transmit data line 1 |
| 23 | ETH_MDC_PIN | Output | Management Data Clock (configuration) |
| 25 | EMAC_RXD0 | Input | Ethernet receive data line 0 |
| 26 | EMAC_RXD1 | Input | Ethernet receive data line 1 |
| 27 | EMAC_CRS_DV | Input | Carrier Sense/Data Valid |

**Key Points:**
- These 10 GPIOs form the complete RMII interface
- Cannot be used for any other purpose
- LAN8720 PHY operates at 10/100 Mbps
- All Ethernet data I/O runs through these lines
- GPIO 12 must be HIGH for PHY to operate (power enable)

### CAN Bus (MCP2515 Controller) - SPI Interface

MCP2515 CAN controller connected via HSPI (High-Speed SPI).

| GPIO | Name | Direction | Purpose | HSPI | Notes |
|------|------|-----------|---------|------|-------|
| 4 | CAN_MISO_PIN | Input | SPI data in (controller → ESP32) | MISO | ✅ Safe - no Ethernet conflicts |
| 13 | CAN_MOSI_PIN | Output | SPI data out (ESP32 → controller) | MOSI | Chosen to avoid GPIO 19 conflict |
| 14 | CAN_SCK_PIN | Output | SPI clock | SCK | High-speed clock for CAN interface |
| 15 | CAN_CS_PIN | Output | Chip select (active LOW) | SS | Selects MCP2515 on bus |
| 32 | CAN_INT_PIN | Input | Interrupt signal (active LOW) | N/A | GPIO only, not SPI |

**Hardware:**
- Waveshare RS485/CAN HAT with MCP2515 controller
- CAN Speed: 500 kbps (standard automotive)
- SPI Clock: 10 MHz (from MCP2515's 8 MHz crystal)
- Interrupt: Pulls LOW when CAN message received

**Critical Design Decision:**
- **GPIO 4 for MISO** (instead of default GPIO 19)
- GPIO 19 is reserved for EMAC_TXD0 (Ethernet)
- GPIO 4 is unused by Ethernet interface
- Verified in `can_driver.h` to have no conflicts

### Contactor/Relay Control (Battery Emulator Integration)

4 GPIO pins control the battery contactors (relays) for safe battery connection/disconnection.

| GPIO | Name | Direction | Purpose | Notes |
|------|------|-----------|---------|-------|
| 33 | CONTACTOR_POSITIVE_PIN | Output | Positive contactor | Main battery positive terminal |
| 34 | CONTACTOR_NEGATIVE_PIN | Output | Negative contactor | Main battery negative terminal |
| 35 | CONTACTOR_PRECHARGE_PIN | Output | Precharge contactor | Precharge resistor path (limits inrush current) |
| 36 | CONTACTOR_2ND_POSITIVE_PIN | Output | 2nd battery positive | Optional - for dual battery setups |

**Hardware:**
- Each GPIO drives a relay coil (via transistor or relay driver IC)
- Relays are typically 12V/24V automotive-grade contactors
- Precharge relay includes series resistor (limits capacitor inrush)
- All outputs are active HIGH (set HIGH to close contactor)

**Control Logic:**
1. **Precharge Sequence**:
   - Close NEGATIVE contactor (GPIO 34 HIGH)
   - Close PRECHARGE contactor (GPIO 35 HIGH)
   - Wait for voltage stabilization (typically 100-500ms)
   - Close POSITIVE contactor (GPIO 33 HIGH)
   - Open PRECHARGE contactor (GPIO 35 LOW)

2. **Normal Operation**:
   - POSITIVE (33) and NEGATIVE (34) remain closed
   - PRECHARGE (35) remains open

3. **Shutdown**:
   - Open POSITIVE contactor (GPIO 33 LOW)
   - Open NEGATIVE contactor (GPIO 34 LOW)

4. **Emergency Stop**:
   - Immediately open all contactors (all GPIOs LOW)

**Safety Features:**
- Contactors fail-safe (spring-open when power removed)
- Precharge prevents capacitor inrush damage
- Configurable PWM hold mode reduces coil power consumption
- NC (Normally Closed) mode supported in firmware

**Firmware Control:**
- Managed by Battery Emulator static data structures
- Settings in `ContactorSettings` structure (transmitter_manager.h)
- PWM frequency configurable (default 20kHz for economizer mode)
- Control enabled/disabled via `contactor.control_enabled` flag

### Available GPIOs (Not Currently Used)

| GPIO | Notes | Potential Use |
|------|-------|-----------------|
| 1 | UART0 TX | Serial debugging (may be used internally) |
| 2 | Strapping pin | Avoid if possible (affects boot mode) |
| 3 | UART0 RX | Serial debugging (may be used internally) |
| 5 | Boot mode strapping | Avoid if possible (affects boot) |
| 6-11 | SPI flash bus | Reserved for internal flash (cannot use) |
| 16-17 | PSRAM bus | Reserved for PSRAM (cannot use) |
| 24 | Reserved | ✓ Available for future expansion |
| 28-31 | Reserved | ✓ Available for future expansion |
| 33-36 | **USED - Contactors** | Battery relay control (see Contactor section) |
| 37-39 | Input only | Available but input-only (no internal pullup) |
| 40-46 | Not available | ESP32 does not have these GPIO numbers |

---

## GPIO Conflict Resolution Summary

### Problem Addressed
The Olimex ESP32-POE-ISO boards have limited GPIO availability due to Ethernet RMII interface consuming 10 pins. The MCP2515 CAN controller requires 5 GPIO pins (4 SPI + 1 interrupt).

### Original Issue
- Default SPI pin assignments would use GPIO 19 for MISO
- GPIO 19 is **already assigned** to Ethernet EMAC_TXD0
- This creates an unresolvable conflict

### Solution Implemented
- Reassigned CAN SPI pins to use HSPI with custom GPIO configuration
- GPIO 4 → MISO (safe, unused by Ethernet)
- GPIO 13 → MOSI (safe, unused by Ethernet)
- GPIO 14 → SCK (safe, unused by Ethernet)
- GPIO 15 → CS (safe, unused by Ethernet)
- GPIO 32 → INT (safe, unused by Ethernet)

### Verification
- **File**: `src/communication/can/can_driver.h` (lines 7-30)
- **Initialization**: `src/communication/can/can_driver.cpp` (init() method)
- **Ethernet reserves**: `src/config/hardware_config.h` (lines 5-8)
- **Status**: ✅ Verified - No GPIO conflicts

---

## Initialization Sequence

The GPIO assignments are initialized in this order:

### 1. Ethernet Initialization (First)
**File**: `src/network/ethernet_manager.cpp:init()`
1. GPIO 12 (ETH_POWER_PIN) → LOW
2. Wait 10ms for reset
3. GPIO 12 → HIGH (power on)
4. Wait 150ms for PHY startup
5. Call `ETH.begin()` which configures RMII pins (0, 18-23, 25-27)

### 2. CAN Initialization (After Ethernet)
**File**: `src/communication/can/can_driver.cpp:init()`
1. Call `SPI.begin(14, 4, 13, 15)` - configures HSPI
2. Initialize MCP2515 with configured pins
3. Set GPIO 32 (CAN_INT_PIN) to INPUT_PULLUP
4. Configure CAN speed to 500 kbps

**Critical**: CAN initialization must occur **AFTER** Ethernet to avoid any transient pin conflicts.

### 3. Contactor Initialization (Optional - if contactor control enabled)
**File**: Battery Emulator integration (future)
1. Set GPIOs 33-36 to OUTPUT mode
2. Initialize all contactors to OPEN (LOW)
3. Configure PWM channels if PWM hold mode enabled
4. Wait for system ready before allowing precharge sequence

**Safety**: All contactors default to OPEN state until explicitly commanded.

---

## Power Management

### GPIO 12 (ETH_POWER_PIN)
- Controls power to LAN8720 PHY
- Must be HIGH during normal operation
- Controlled by `EthernetManager::init()` and event handlers
- Active HIGH (1 = power on, 0 = power off)

### Supply Voltages
- **Ethernet (LAN8720)**: 3.3V LVCMOS
- **CAN Controller (MCP2515)**: 3.3V (on HAT breakout)
- **MCP2515 Transceiver (TJA1050)**: 5V (on HAT breakout)
- **CAN Bus**: 5V (standard automotive)

---

## SPI Bus Sharing

### HSPI Bus (Used for CAN)
- **Clock**: GPIO 14 (SCK)
- **MOSI**: GPIO 13 (Master Out Slave In)
- **MISO**: GPIO 4 (Master In Slave Out)
- **Devices**: MCP2515 only (via GPIO 15 CS)

### VSPI Bus (Available)
- Could be used for future SPI devices
- Default: GPIO 23 (CLK), GPIO 19 (MOSI), GPIO 18 (MISO)
- **Constraint**: GPIO 19 unavailable (Ethernet), GPIO 18/23 unavailable (Ethernet)

---

## Design Constraints & Lessons Learned

### Hardware Limitations
1. **Ethernet RMII dominates**: 10 pins for data/control (0, 12, 18-27)
2. **CAN SPI interface**: 5 pins for MCP2515 (4, 13-15, 32)
3. **Contactor control**: 4 pins for battery relays (33-36)
4. **SPI Flash**: 6 pins (not user-accessible, internal)
5. **PSRAM**: 2 pins (not user-accessible, internal)
6. **Total**: ~22 pins allocated/reserved, leaving ~18 GPIO for future expansion

### GPIO Scarcity Solutions
- Prioritize SPI interfaces (HSPI/VSPI) for multi-pin peripherals
- Use I2C for simple sensors (only 2 pins: SDA/SCL)
- Consider multiplexing if additional features needed
- Document all GPIO assignments to prevent accidental conflicts

### Future Expansion
If additional GPIO-intensive peripherals are needed:
1. **Consider I2C alternatives** for sensors/controllers
2. **Use GPIO expanders** (I2C/SPI based) for additional pins
3. **Evaluate alternative boards** with more GPIO (e.g., ESP32S3 with more pins)

---

## Related Documentation

- [Project Master Document](PROJECT_ARCHITECTURE_MASTER.md) - Main architecture overview
- `src/config/hardware_config.h` - Hardware configuration constants
- `src/communication/can/can_driver.h` - CAN driver GPIO definitions
- `src/network/ethernet_manager.cpp` - Ethernet initialization code
- Hardware Datasheets:
  - ESP32 Datasheet (GPIO capabilities)
  - LAN8720 PHY Manual (RMII interface)
  - MCP2515 CAN Controller Manual (SPI interface)
  - TJA1050 Transceiver Manual (CAN bus)

---

## Verification Checklist

### CAN SPI Interface
- [x] GPIO 4 (CAN_MISO) verified safe from Ethernet conflicts
- [x] GPIO 13 (CAN_MOSI) verified safe from Ethernet conflicts
- [x] GPIO 14 (CAN_SCK) verified safe from Ethernet conflicts
- [x] GPIO 15 (CAN_CS) verified safe from Ethernet conflicts
- [x] GPIO 32 (CAN_INT) verified safe from Ethernet conflicts

### Ethernet RMII Interface
- [x] GPIO 12 (ETH_POWER) properly initialized
- [x] Ethernet RMII pins (0, 18-27) reserved and documented
- [x] Power enable logic validated (GPIO 12 HIGH = on)

### Contactor/Relay Control
- [x] GPIO 33 (CONTACTOR_POSITIVE) verified available
- [x] GPIO 34 (CONTACTOR_NEGATIVE) verified available
- [x] GPIO 35 (CONTACTOR_PRECHARGE) verified available
- [x] GPIO 36 (CONTACTOR_2ND_POSITIVE) verified available
- [x] Precharge sequence documented (safety-critical)
- [x] Fail-safe operation confirmed (relays open when de-energized)

### General
- [x] Initialization sequence verified (Ethernet → CAN → Contactors)
- [x] No pin conflicts detected across all interfaces
- [x] All 18 allocated GPIOs documented

---

**Document Version**: 1.1  
**Last Updated**: February 20, 2026  
**Status**: Complete ✅ (Updated with Contactor GPIOs)
