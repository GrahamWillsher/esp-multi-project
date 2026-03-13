# LilyGo T-Display-S3 Hardware Pinout

**Board:** LilyGo T-Display-S3  
**MCU:** ESP32-S3 (WROOM-1)  
**Display:** ST7789 (320×170 pixels, 8-bit parallel)  
**Documentation Date:** February 25, 2026

---

## Display Interface (ST7789 Controller)

### Control Signals

| Function | GPIO | Purpose | Active State | Notes |
|----------|------|---------|--------------|-------|
| **Power Enable** | 15 | Display power rail | HIGH | Must be HIGH before TFT init |
| **Backlight** | 38 | Display brightness (PWM) | HIGH | 200Hz PWM, 8-bit resolution |
| **DC** (Data/Cmd) | 7 | TFT control | HIGH=data, LOW=cmd | Parallel interface control |
| **Reset** | 5 | TFT reset | LOW | Active-low reset signal |
| **Chip Select** | 6 | TFT chip select | LOW | Active-low CS |
| **Write Enable** | 8 | Parallel write strobe | LOW | Active-low write pulse |
| **Read Enable** | 9 | Parallel read strobe | LOW | Active-low read pulse |

### 8-bit Parallel Data Bus

| Signal | GPIO | Notes |
|--------|------|-------|
| **D0** | 39 | LSB |
| **D1** | 40 | |
| **D2** | 41 | |
| **D3** | 42 | |
| **D4** | 45 | |
| **D5** | 46 | ⚠️ Has on-board pull-down (strapping pin) |
| **D6** | 47 | |
| **D7** | 48 | MSB |

---

## Display Specifications

- **Controller:** ST7789V
- **Resolution:** 320×170 pixels (physical)
- **Interface:** 8-bit parallel (not SPI)
- **Color Depth:** 16-bit RGB565
- **Orientation:** Landscape (rotation=1)
- **Pixel Order:** BGR (red and blue swapped)
- **Inversion:** ON

---

## Other On-Board Features

### USB/Serial Interface

| Function | GPIO | Notes |
|----------|------|-------|
| USB D- | 19 | USB OTG interface |
| USB D+ | 20 | USB OTG interface |
| USB Serial TX | 43 | ⚠️ Used by display data bus |
| USB Serial RX | 44 | ⚠️ Used by display data bus |

### User Interface

| Function | GPIO | Notes |
|----------|------|-------|
| **BOOT Button** | 0 | Has on-board pull-up, boot mode selection |

### SD Card (If Populated)

| Function | GPIO | Notes |
|----------|------|-------|
| SD_CLK | 1 | |
| SD_CMD | 2 | |
| SD_DATA0 | 14 | |
| SD_DATA1 | 17 | |

---

## ESP32-S3 Considerations

### Strapping Pins (Boot Mode Selection)

| GPIO | Pull Resistor | Function | Safe States |
|------|---------------|----------|-------------|
| 0 | Pull-up | Boot button | HIGH (normal), LOW (download) |
| 3 | Float | JTAG enable | Any |
| 45 | Pull-down | VDD_SPI voltage | LOW=3.3V, HIGH=1.8V |
| 46 | Pull-down | Boot mode | LOW=SPI, HIGH=download |

⚠️ **Important:** GPIO 46 is used for display D5 and has a pull-down resistor. This is acceptable for data bus usage but be aware during boot.

### PSRAM Interface (Built-in)

| GPIO | Function |
|------|----------|
| 33 | PSRAM_CS |
| 34 | PSRAM_CLK |
| 35 | PSRAM_D0 |
| 36 | PSRAM_D1 |
| 37 | PSRAM_D2 |

---

## Pin Conflict Analysis

### ✅ No Conflicts Detected

All display pins (GPIO 5-9, 15, 38-48) are dedicated to the display subsystem and do not conflict with:
- USB interface (GPIO 19, 20)
- PSRAM (GPIO 33-37)
- Boot button (GPIO 0)

### ⚠️ Potential Issues

1. **GPIO 43, 44 (USB Serial):** Shared with display data bus. USB-Serial still works because the display doesn't actively drive these pins except during writes.
   
2. **GPIO 46 (Strapping Pin):** Used for D5 on data bus. Has on-board pull-down which affects boot mode selection. Works fine for display data but must be aware during flash/boot sequences.

---

## Power Sequencing

**Critical:** Display power (GPIO 15) **must** be set HIGH before initializing TFT_eSPI.

**Correct Sequence:**
1. Set GPIO 15 HIGH (enable display power)
2. Wait 100ms for power stabilization
3. Initialize TFT_eSPI
4. Configure backlight PWM (GPIO 38)
5. Fade in backlight

---

## Configuration Files

All hardware pin definitions are centralized in:

- **Primary HAL:** `src/hal/hardware_config.h`
- **TFT_eSPI Setup:** `src/hal/tft_espi_user_setup.h`

Build flags in `platformio.ini` no longer contain GPIO definitions - all hardware configuration is in the HAL layer.

---

## References

- [LilyGo T-Display-S3 GitHub](https://github.com/Xinyuan-LilyGO/T-Display-S3)
- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ST7789V Datasheet](https://www.waveshare.com/w/upload/a/ae/ST7789_Datasheet.pdf)
