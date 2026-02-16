# Hardware Abstraction Layer (HAL) - Receiver (LilyGo T-Display-S3)

## Overview

The **receiver** is a portable wireless monitoring device with a built-in display for real-time battery status visualization. It receives data via ESP-NOW from the transmitter and displays it on a color touchscreen.

**Hardware Stack**:
- **MCU**: ESP32-S3 (dual-core, 240 MHz)
- **Display**: ST7789 TFT LCD (1.9", 320×170 pixels, 8-bit parallel interface)
- **Touch Input**: Touch-sensitive buttons (capacitive, on-display)
- **Network**: WiFi (2.4 GHz 802.11 b/g/n) + ESP-NOW
- **Connectivity**: USB-C (charging + serial programming)
- **Optional**: MQTT client (for persistent logging to Battery Emulator)

**No CAN hardware** — receiver is wireless-only, acts as display/logging hub.

---

## Hardware Specifications

### LilyGo T-Display-S3 (Receiver MCU)

- **Processor**: Espressif ESP32-S3 (dual-core Xtensa LX7, 240 MHz)
- **RAM**: 512 KB SRAM + 8 MB PSRAM (OCTAL SDRAM)
- **Flash**: 16 MB (QIO, auto-detected)
- **Power**: Built-in 1000 mAh LiPo battery
- **Charging**: USB-C with charging circuit (400 mA max)
- **GPIO Pins Available**: 45 pins (some dedicated to peripherals)
- **Operating Temp**: 0°C to +40°C (typical)
- **Dimensions**: 80mm × 52mm × 13mm (pocket-sized)
- **Weight**: ~50g

### ST7789 TFT Display

- **Type**: IPS TFT LCD (In-Plane Switching)
- **Resolution**: 320 × 170 pixels
- **Color Depth**: 16-bit (RGB565, 65k colors)
- **Interface**: 8-bit parallel (Intel 8080 mode, no SPI)
- **Refresh Rate**: ~60 Hz
- **Brightness**: ~200 cd/m² (readable in bright daylight)
- **Backlight**: LED (dimmable via PWM)
- **Gamma Control**: Adjustable for contrast

### Touch Buttons (Capacitive)

- **Type**: On-display capacitive touch buttons
- **Count**: 2 buttons (firmware-dependent)
- **Location**: Below main display area (physical buttons)
- **Functionality**: Menu navigation, brightness, page switching
- **Response Time**: <50 ms

---

## GPIO Pin Mapping

### Display (ST7789 8-Bit Parallel Interface)

| Signal | GPIO Pin | Type | Notes |
|--------|----------|------|-------|
| D0 (LCD_D0) | GPIO 5 | Output | Parallel data bit 0 |
| D1 (LCD_D1) | GPIO 6 | Output | Parallel data bit 1 |
| D2 (LCD_D2) | GPIO 7 | Output | Parallel data bit 2 |
| D3 (LCD_D3) | GPIO 8 | Output | Parallel data bit 3 |
| D4 (LCD_D4) | GPIO 9 | Output | Parallel data bit 4 |
| D5 (LCD_D5) | GPIO 46 | Output | Parallel data bit 5 |
| D6 (LCD_D6) | GPIO 3 | Output | Parallel data bit 6 |
| D7 (LCD_D7) | GPIO 4 | Output | Parallel data bit 7 |
| CS (LCD_CS) | GPIO 10 | Output | Chip select (active-low) |
| RS (LCD_RS) | GPIO 11 | Output | Register select (DC=data, low=command) |
| WR (LCD_WR) | GPIO 13 | Output | Write pulse (active-low) |
| RD (LCD_RD) | GPIO 14 | Output | Read pulse (active-low) |
| RST (LCD_RST) | GPIO 12 | Output | Reset (active-low) |
| BL (Backlight) | GPIO 2 | Output | Backlight PWM (brightness control) |

**Interface Notes**: 
- 8-bit parallel interface is ~8x faster than SPI
- TFT_eSPI library handles pin multiplexing automatically
- Backlight PWM allows dimming for power saving

### Touch Buttons (Physical Capacitive)

| Button | GPIO Pin | Type | Notes |
|--------|----------|------|-------|
| Button 0 | GPIO 0 | Input | Menu / Back (active-low) |
| Button 1 | GPIO 14 | Input | Power / Select (active-low) |

**Note**: Physical button implementation may vary; check `display_core.cpp` for actual pin assignment.

### USB & Power

| Signal | Pin | Type | Notes |
|--------|-----|------|-------|
| USB_DP | USB | Bidirectional | USB data + (programming) |
| USB_DM | USB | Bidirectional | USB data - (programming) |
| VBUS | USB | Input | 5V from USB charger |
| GND | USB | — | Common ground |
| BAT+ | Internal | — | LiPo battery positive |
| BAT- | Internal | — | LiPo battery negative (GND) |

### WiFi & ESP-NOW

| Function | Status | Notes |
|----------|--------|-------|
| WiFi (2.4 GHz) | Built-in | 802.11 b/g/n (no external antenna needed) |
| ESP-NOW | Built-in | Direct device-to-device communication |
| Antenna | PCB trace | Embedded on main board |

---

## Hardware Configuration Code

### `src/config/display_config.h`

```cpp
#pragma once

namespace display {
    // ===== DISPLAY PINOUT (ST7789 8-bit Parallel) =====
    constexpr int LCD_D0 = 5;      // Parallel data bit 0
    constexpr int LCD_D1 = 6;      // Parallel data bit 1
    constexpr int LCD_D2 = 7;      // Parallel data bit 2
    constexpr int LCD_D3 = 8;      // Parallel data bit 3
    constexpr int LCD_D4 = 9;      // Parallel data bit 4
    constexpr int LCD_D5 = 46;     // Parallel data bit 5
    constexpr int LCD_D6 = 3;      // Parallel data bit 6
    constexpr int LCD_D7 = 4;      // Parallel data bit 7
    
    constexpr int LCD_CS = 10;     // Chip select
    constexpr int LCD_RS = 11;     // Register select (DC pin)
    constexpr int LCD_WR = 13;     // Write strobe
    constexpr int LCD_RD = 14;     // Read strobe
    constexpr int LCD_RST = 12;    // Reset
    constexpr int LCD_BL = 2;      // Backlight PWM
    
    // ===== DISPLAY SETTINGS =====
    constexpr int SCREEN_WIDTH = 320;     // Pixels (landscape)
    constexpr int SCREEN_HEIGHT = 170;    // Pixels
    constexpr int FONT_SIZE_SMALL = 1;    // TFT library scale
    constexpr int FONT_SIZE_MEDIUM = 2;
    constexpr int FONT_SIZE_LARGE = 3;
    
    // ===== BACKLIGHT CONTROL =====
    constexpr int BL_PWM_FREQ = 1000;     // 1 kHz PWM frequency
    constexpr int BL_PWM_RESOLUTION = 8;  // 8-bit (0-255)
    constexpr int BL_DEFAULT_BRIGHTNESS = 200;  // ~78% brightness
    constexpr int BL_MIN_BRIGHTNESS = 50;       // ~20% (readable minimum)
    constexpr int BL_MAX_BRIGHTNESS = 255;      // 100%
}
```

### `src/config/button_config.h`

```cpp
#pragma once

namespace input {
    // ===== BUTTON PINOUT =====
    constexpr int BUTTON_0 = 0;        // Menu / Back button
    constexpr int BUTTON_1 = 14;       // Power / Select button
    
    // ===== DEBOUNCE SETTINGS =====
    constexpr int BUTTON_DEBOUNCE_MS = 50;     // Debounce time
    constexpr int BUTTON_LONG_PRESS_MS = 1000; // Long press threshold
    
    // ===== BUTTON STATES =====
    enum ButtonState {
        BUTTON_IDLE = 0,
        BUTTON_PRESSED = 1,
        BUTTON_RELEASED = 2,
        BUTTON_LONG_HELD = 3,
    };
}
```

### `src/config/hardware_config.h`

```cpp
#pragma once

namespace hardware {
    // ===== DEVICE IDENTIFICATION =====
    constexpr const char* DEVICE_NAME = "Battery Monitor";
    constexpr const char* DEVICE_MODEL = "LilyGo T-Display-S3";
    
    // ===== POWER MANAGEMENT =====
    constexpr int BATT_ADC_PIN = 4;         // Battery voltage sensing
    constexpr int BATT_ADC_CHANNEL = 0;     // ADC channel
    constexpr float BATT_ADC_COEFF = 3.3;   // Voltage divider ratio
    
    // ===== MEMORY CONFIGURATION =====
    constexpr int FLASH_SIZE_MB = 16;
    constexpr int PSRAM_SIZE_MB = 8;
    
    // ===== FILESYSTEM =====
    // LittleFS for web assets (HTML, CSS, JS)
    constexpr const char* SPIFFS_MOUNT_POINT = "/fs";
}
```

---

## Initialization Sequence

```cpp
// src/main.cpp - Receiver initialization

void setup() {
    // Step 1: Initialize Serial (debugging)
    Serial.begin(115200);
    delay(100);
    LOG_INFO("BOOT", "LilyGo T-Display-S3 starting...");
    
    // Step 2: Initialize Display
    if (!init_display()) {
        LOG_ERROR("DISPLAY", "Display init failed!");
        // Continue anyway (can debug via serial)
    } else {
        LOG_INFO("DISPLAY", "✓ ST7789 initialized");
        display_splash_screen();
    }
    
    // Step 3: Initialize WiFi + ESP-NOW
    if (!init_wifi()) {
        LOG_ERROR("WIFI", "WiFi init failed!");
    } else {
        LOG_INFO("WIFI", "✓ WiFi ready for ESP-NOW");
    }
    
    // Step 4: Initialize buttons
    init_buttons();
    LOG_INFO("INPUT", "✓ Buttons initialized");
    
    // Step 5: Initialize MQTT (optional)
    if (mqtt_enabled) {
        mqtt_client.begin();
        mqtt_client.subscribe_battery_topics();
    }
    
    // Step 6: Start main display loop
    xTaskCreate(display_update_loop, "DisplayTask", 4096, nullptr, 5, nullptr);
    xTaskCreate(button_input_loop, "ButtonTask", 2048, nullptr, 5, nullptr);
}

// Display initialization (uses TFT_eSPI library)
bool init_display() {
    // TFT_eSPI library auto-configures based on User_Setup.h
    // which is configured in platformio.ini build flags
    tft.begin();
    tft.setRotation(0);  // Portrait (320x170)
    tft.fillScreen(TFT_BLACK);
    
    // Initialize backlight
    pinMode(display::LCD_BL, OUTPUT);
    ledcSetup(0, display::BL_PWM_FREQ, display::BL_PWM_RESOLUTION);
    ledcAttachPin(display::LCD_BL, 0);
    ledcWrite(0, display::BL_DEFAULT_BRIGHTNESS);
    
    return true;
}

// WiFi initialization
bool init_wifi() {
    WiFi.mode(WIFI_STA);
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        LOG_ERROR("ESPNOW", "ESP-NOW init failed!");
        return false;
    }
    
    // Register receive callback
    esp_now_register_recv_cb(on_data_recv);
    
    LOG_INFO("ESPNOW", "✓ Listening for transmitter data");
    return true;
}

// Button initialization
void init_buttons() {
    pinMode(input::BUTTON_0, INPUT_PULLUP);
    pinMode(input::BUTTON_1, INPUT_PULLUP);
}
```

---

## Display Update Loop

```cpp
void display_update_loop(void* parameter) {
    const TickType_t delay_ms = 100;  // Update display every 100ms
    
    while (true) {
        // Read latest battery data from cache
        float soc = get_cached_soc();
        int32_t power = get_cached_power();
        uint16_t voltage = get_cached_voltage();
        
        // Update display
        display_soc(soc);
        display_power(power);
        display_voltage(voltage);
        
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
```

---

## Power Consumption

| Component | Current | Notes |
|-----------|---------|-------|
| MCU (active) | 80-160 mA | Depends on WiFi/radio state |
| Display (full brightness) | 60-100 mA | Backlight dominates power |
| WiFi (RX) | 40-80 mA | ESP-NOW mode (lower than TCP/IP) |
| WiFi (TX) | 100-200 mA | Peak during transmission |
| Idle (display off) | 2-5 mA | Sleep mode capable |
| **Total (typical)** | **150-250 mA** | At full brightness, WiFi active |

**Battery Life**: ~4-6 hours continuous use (1000 mAh @ 150 mA avg)

---

## Connectivity Options

### 1. ESP-NOW (Primary)
- **Range**: ~100 meters (line-of-sight)
- **Latency**: <10 ms
- **Power**: Very low (~40 mA RX)
- **Use Case**: Real-time monitoring on same network

### 2. WiFi + MQTT (Optional)
- **Range**: Unlimited (via WiFi AP)
- **Latency**: Variable (typically 50-500 ms)
- **Power**: Higher (~80-160 mA)
- **Use Case**: Persistent logging to Battery Emulator or cloud

### 3. USB Serial (Debug Only)
- **Use Case**: Firmware upload, serial debugging
- **Port**: USB-C (CH340 USB-to-serial on board)
- **Baud Rate**: 115200

---

## Testing & Diagnostics

### Display Communication Test

```cpp
void test_display_spi() {
    LOG_INFO("DISPLAY", "Testing ST7789 initialization...");
    
    // Fill screen with colors to verify parallel bus working
    tft.fillScreen(TFT_RED);
    delay(500);
    tft.fillScreen(TFT_GREEN);
    delay(500);
    tft.fillScreen(TFT_BLUE);
    delay(500);
    tft.fillScreen(TFT_BLACK);
    
    LOG_INFO("DISPLAY", "✓ Color sequence passed");
}
```

### Button Test

```cpp
void test_buttons() {
    LOG_INFO("INPUT", "Testing buttons (press each 3 times)...");
    int button_0_presses = 0, button_1_presses = 0;
    unsigned long timeout = millis() + 10000;  // 10 second timeout
    
    while (millis() < timeout) {
        if (digitalRead(input::BUTTON_0) == LOW) {
            button_0_presses++;
            LOG_INFO("INPUT", "Button 0 pressed (count: %d)", button_0_presses);
            delay(500);  // Debounce
            while (digitalRead(input::BUTTON_0) == LOW) delay(10);
        }
        
        if (digitalRead(input::BUTTON_1) == LOW) {
            button_1_presses++;
            LOG_INFO("INPUT", "Button 1 pressed (count: %d)", button_1_presses);
            delay(500);
            while (digitalRead(input::BUTTON_1) == LOW) delay(10);
        }
    }
    
    LOG_INFO("INPUT", "Button 0: %d presses, Button 1: %d presses", button_0_presses, button_1_presses);
}
```

### WiFi & ESP-NOW Test

```cpp
void test_espnow_reception() {
    LOG_INFO("ESPNOW", "Waiting for transmitter data (30 seconds)...");
    
    uint32_t last_message_ms = 0;
    bool received_any = false;
    
    // Set up a callback that records reception time
    auto on_test_recv = [](const uint8_t* mac, const uint8_t* data, int len) {
        LOG_INFO("ESPNOW", "Received %d bytes from %02X:%02X:%02X:%02X:%02X:%02X",
                 len, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        received_any = true;
        last_message_ms = millis();
    };
    
    esp_now_register_recv_cb(on_test_recv);
    
    unsigned long start = millis();
    while (millis() - start < 30000) {
        if (received_any) {
            LOG_INFO("ESPNOW", "✓ Reception confirmed!");
            return;
        }
        delay(100);
    }
    
    LOG_ERROR("ESPNOW", "✗ No data received in 30 seconds");
    LOG_ERROR("ESPNOW", "  - Check transmitter is powered on");
    LOG_ERROR("ESPNOW", "  - Check transmitter has receiver MAC address in EEPROM");
    LOG_ERROR("ESPNOW", "  - Try both devices on same WiFi channel (1-11)");
}
```

---

## Battery Management

### Battery Voltage Monitoring

```cpp
float read_battery_voltage() {
    int raw_adc = analogRead(hardware::BATT_ADC_PIN);
    float voltage = (raw_adc / 4095.0) * hardware::BATT_ADC_COEFF;
    return voltage;
}

void battery_monitor_task(void* parameter) {
    while (true) {
        float vbat = read_battery_voltage();
        
        if (vbat < 3.0) {
            LOG_WARN("POWER", "Low battery: %.2fV", vbat);
            // Reduce display brightness, disable WiFi if needed
            ledcWrite(0, display::BL_MIN_BRIGHTNESS);
        } else if (vbat > 4.2) {
            LOG_WARN("POWER", "Over-voltage: %.2fV", vbat);
            // Charging controller should handle this
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));  // Check every 5 seconds
    }
}
```

---

## Troubleshooting

### Display Shows Garbage/Distorted
**Cause**: 8-bit parallel interface timing issue
**Solution**:
- Check all 8 data lines (GPIO 5-9, 46, 3-4) for loose connections
- Verify LCD_WR, LCD_RD, LCD_RS, LCD_CS pins
- Try reducing display refresh rate in TFT_eSPI config

### Display Very Dim
**Cause**: Backlight PWM not working or battery low
**Solution**:
- Check GPIO 2 (backlight control) is properly connected
- Verify battery voltage: `Serial.println(read_battery_voltage())`
- Charge device via USB-C

### Buttons Not Responding
**Cause**: GPIO 0 or 14 not reading correctly
**Solution**:
- Check physical button contacts not corroded
- Verify pull-up resistors (internal should be sufficient)
- Test with `Serial.println(digitalRead(BUTTON_0))`

### ESP-NOW Data Not Received
**Cause**: WiFi/ESP-NOW not initialized or transmitter not paired
**Solution**:
- Verify both devices on same WiFi channel (use WiFi scanner)
- Check receiver MAC address stored in transmitter
- Try pairing MAC address dynamically
- Check transmitter is transmitting (monitor serial output)

---

## References

- **LilyGo T-Display-S3 GitHub**: https://github.com/Xinyuan-LilyGo/T-Display-S3
- **ESP32-S3 Datasheet**: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
- **TFT_eSPI Library**: https://github.com/Bodmer/TFT_eSPI
- **ST7789 Display Controller**: https://www.st.com/resource/en/datasheet/st7789v.pdf

---

**Document Version**: 1.0  
**Created**: February 16, 2026  
**Hardware Tested**: LilyGo T-Display-S3 (ESP32-S3 variant)  
**Scope**: Receiver only  
**Note**: No CAN hardware — wireless monitoring only
