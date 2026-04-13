# Olimex ESP32-POE2 ↔ Waveshare RS485/CAN HAT(B) Pin Layout (Separate SPI Buses)

## Scope
This document consolidates the hardware pin layout for:
1. Olimex ESP32-POE2 EXT1 connector
2. Waveshare RS485/CAN HAT(B) yellow 12-pin control interface conventions
3. **Separate SPI bus architecture** for CAN (HSPI) and RS485 (VSPI) — **NO shared data lines**
4. Remaining GPIO availability for external relay control

**Architecture Principle**: CAN and RS485 have completely independent data buses to eliminate simultaneous I/O conflicts and bus contention.

**System note**: In the two-board architecture, external relay actuation is moved to the receiver board (**LilyGo T-Display-S3 non-touch**). Olimex transmitter controls receiver relays over ESP-NOW. See companion document: `LILYGO_T_DISPLAY_S3_RELAY_GPIO_AVAILABILITY.md`.

---

## Authoritative Sources

### Official vendor sources
- Olimex ESP32-POE2 user manual (EXT1 + GPIO notes)
- Olimex ESP32-POE2 Rev_B schematic (KiCad), symbol `EXT1`
- Waveshare RS485/CAN HAT(B) wiki (CAN/RS485 control tables and jumper notes)

### Project sources (for consistency)
- `ESPnowtransmitter2/espnowtransmitter2/src/communication/can/can_driver.h`
- `ESPnowtransmitter2/espnowtransmitter2/CAN_ETHERNET_GPIO_CONFLICT_ANALYSIS.md`
- `ESPnowtransmitter2/espnowtransmitter2/PROJECT_ARCHITECTURE_MASTER.md`

---

## Olimex ESP32-POE2 EXT1 (26-pin) Full Layout

> Orientation here follows schematic pin numbering (odd pins left column, even pins right column).
> On ESP32-POE2 (WROVER), GPIO16 is remapped behaviorally in Olimex docs; use caution and validate in firmware.

| EXT1 Pin | Signal (schematic net) | Type | Notes |
|---|---|---|---|
| 1 | GPIO13 | I/O | CAN MOSI (HSPI) |
| 2 | GPIO14 | I/O | CAN SCLK (HSPI) |
| 3 | GPIO12 | I/O | **Ethernet PHY power** in typical ETH config |
| 4 | GPIO15 | I/O | Strap-sensitive/general use |
| 5 | GPIO5 | I/O | Strap-sensitive/general use |
| 6 | GPIO16 (board remap caveat) | I/O (board-specific) | Olimex WROVER notes apply |
| 7 | GPIO4 | I/O | CAN MISO (HSPI) |
| 8 | GPIO32 | I/O | CAN INT (interrupt input) |
| 9 | GPIO3 (U0RXD) | I/O | RS485 MISO (VSPI) |
| 10 | GPIO33 | I/O | RS485 CE_1 (chip select) |
| 11 | GPIO2 | I/O | RS485 SCLK (VSPI) |
| 12 | GPI34 | Input-only | Cannot drive relay directly |
| 13 | GPIO1 (U0TXD) | I/O | RS485 MOSI (VSPI) |
| 14 | GPI35 | Input-only | Cannot drive relay directly |
| 15 | GPIO0 | I/O | **Ethernet clock output** in ETH mode; boot strap |
| 16 | GPI36 | Input-only | RS485 INT (interrupt input) |
| 17 | ESP_EN | Control | Chip enable/reset line (do not use as GPIO output) |
| 18 | GPI39 | Input-only | External power sense in Olimex docs |
| 19 | +3.3V | Power | Power rail |
| 20 | GND | Power | Ground |
| 21 | +5V | Power | 5V rail |
| 22 | GND | Power | Ground |
| 23 | +5VP | Power | PoE-related 5V rail |
| 24 | GND | Power | Ground |
| 25 | VPP | Power | 12V/24V selectable rail |
| 26 | GND | Power | Ground |

---

## Waveshare RS485/CAN HAT(B) Interface Naming (Yellow 12-pin convention)

Vendor control naming:

### CAN (`CAN_0`, SPI0 naming)
- `SCLK_0`
- `MOSI_0`
- `MISO_0`
- `CE_0`
- `INT_0`
- `5V`, `GND`

### RS485 (`RS485_0`/`RS485_1`, SPI1 naming on wiki)
- `SCLK_1`
- `MOSI_1`
- `MISO_1`
- `CE_1`
- `INT_1`
- `5V`, `GND`

Waveshare wiki also documents solder-jumper remap options for conflicting default pins (notably `CE_0`, `INT_0`, `INT_1`).

---

## CAN Bus Mapping (HSPI / SPI2) – Dedicated Data Lines

Preserved from existing code (current stable configuration):
- `SCLK_0` = GPIO14 (HSPI clock)
- `MOSI_0` = GPIO13 (HSPI master-out, ESP32 → CAN IC)
- `MISO_0` = GPIO4 (HSPI master-in, CAN IC → ESP32)
- `CE_0` (CAN CS) = GPIO15 (chip select)
- `INT_0` (CAN INT) = GPIO32 (interrupt input)

**Data lines are NOT shared with RS485** — CAN has exclusive use of GPIO14/13/4.

---

## RS485/Modbus Bus Mapping (VSPI / SPI3) – Dedicated Data Lines

**Completely separate from CAN bus** — RS485 has its own independent SPI data lines:
- `SCLK_1` = GPIO2 (VSPI clock, after Waveshare jumper remap)
- `MOSI_1` = GPIO1 (VSPI master-out, ESP32 → RS485 IC, after Waveshare jumper remap)
- `MISO_1` = GPIO3 (VSPI master-in, RS485 IC → ESP32, after Waveshare jumper remap)
- `CE_1` = GPIO33 (chip select output)
- `INT_1` = GPIO36 (interrupt input)

**Remap requirement**: Waveshare HAT has default VSPI pins on GPIO18/23/19 (Ethernet conflicts). Solder-jumper configuration redirects to GPIO2/1/3, which are available on EXT1.

**Key advantage**: GPIO14/13/4 (CAN data lines) are **never** used by RS485. Zero bus contention risk.

---

## External Relay GPIO Availability (after CAN + RS485 allocation on separate buses)

### Not available / avoid
- Ethernet reserved: GPIO0, GPIO12, GPIO18, GPIO19, GPIO21, GPIO22, GPIO23, GPIO25, GPIO26, GPIO27
- CAN reserved (HSPI): GPIO14, GPIO13, GPIO4, GPIO15, GPIO32
- RS485 remap candidates: GPIO1, GPIO2, GPIO3 (used as SPI data lines after Waveshare jumper config)
- Input-only (cannot drive relay coil directly): GPIO34, GPIO35, GPIO36, GPIO39
- RS485 allocated: GPIO33 (CE_1), GPIO36 (INT_1)

### Output-capable candidates remaining on EXT1
- GPIO5 (available, strap-sensitive)
- GPIO16 (board/WROVER caveat – validate in firmware)

### Recommended relay approach
- For 4 relays: use an I2C GPIO expander (recommended)
- For direct ESP32 control only: practical dedicated outputs are GPIO5 (+ optionally GPIO16 after validation)

### GPIO1/GPIO3 post-boot finding (important)
- GPIO1/GPIO3 can be repurposed as normal GPIO **after boot/programming** if UART0 logging/programming is not needed at runtime.
- In this selected separate-bus design, GPIO1/GPIO3 are assigned to RS485 (`MOSI_1`/`MISO_1`) and are therefore **not available** for relays while RS485 is active.
- If reassigned to relays in an alternate design, keep boot-safe levels and accept loss of UART0 console convenience.

---

## Practical Wiring Summary

### Olimex EXT1 to Waveshare CAN interface (HSPI – SPI2)
- EXT1-2 (GPIO14) → Waveshare `SCLK_0` (HSPI clock)
- EXT1-1 (GPIO13) → Waveshare `MOSI_0` (HSPI master-out)
- EXT1-7 (GPIO4) → Waveshare `MISO_0` (HSPI master-in)
- EXT1-4 (GPIO15) → Waveshare `CE_0` (CAN chip select)
- EXT1-8 (GPIO32) → Waveshare `INT_0` (CAN interrupt)

### Olimex EXT1 to Waveshare RS485 interface (VSPI – SPI3, with solder jumpers)
After configuring Waveshare solder jumpers to remap VSPI pins:
- EXT1-11 (GPIO2) → Waveshare `SCLK_1` (jumper remapped, VSPI clock)
- EXT1-13 (GPIO1) → Waveshare `MOSI_1` (jumper remapped, VSPI master-out)
- EXT1-9 (GPIO3) → Waveshare `MISO_1` (jumper remapped, VSPI master-in)
- EXT1-10 (GPIO33) → Waveshare `CE_1` (RS485 chip select)
- EXT1-16 (GPI36 input) → Waveshare `INT_1` (RS485 interrupt)

### Power connections
- EXT1-19/21/23 (+3.3V/+5V/+5VP) → Waveshare power as needed
- EXT1-20/22/24/26 (GND) → Waveshare ground

### Relay control (separate I2C GPIO expander recommended)
If using I2C GPIO expander:
- EXT1-6 (GPIO16 or alternate) → Expander SDA (if GPIO16 accessible on EXT1)
- EXT1 GPIO with I2C capability → Expander SCL
- Expander outputs → relay driver transistors/MOSFETs

### External relay control outputs
- Primary direct output: EXT1-5 (GPIO5)
- Conditional direct output: EXT1-6 (GPIO16, validate board behavior)
- For 4 relays: use external GPIO expander (recommended)

---

## Notes on Prior Internal Documentation
Some internal notes mention “GPIO33–GPIO36 for 4 contactors.”
Electrical reality: GPIO34/35/36 are input-only on ESP32, so only GPIO33 is output-capable among that group (and in this separate-bus mapping it is already allocated to `CE_1`).

Use this document as the corrected hardware planning baseline.