# espnowreceiver_LCD

ESP-NOW battery telemetry receiver with Waveshare 7-inch LVGL display.

**Hardware:** Waveshare ESP32-S3-Touch-LCD-7 (16 MB flash, 8 MB PSRAM)  
**Framework:** Arduino + LVGL 8.4.0 (LVGL-only render path, no TFT_eSPI)  
**Connectivity:** WiFi (station + AP fallback), ESP-NOW peer, MQTT client, OTA

---

## What it does

- Receives battery telemetry from an ESP-NOW transmitter (ESPnowtransmitter2)
- Displays SOC%, bidirectional power bar, and link state on a 7-inch LVGL UI
- Hosts a web dashboard (webserver_lcd) for configuration and live monitoring
- Forwards MQTT topics to a broker when configured
- Supports OTA firmware updates over WiFi

---

## Build

```bash
pio run -j 2
```

## Flash

```bash
# Firmware
pio run --target upload --environment waveshare_esp32s3_lcd7_lvgl

# LittleFS (required after first flash or full erase)
pio run --target uploadfs --environment waveshare_esp32s3_lcd7_lvgl
```

> After a full flash erase, upload firmware **and** LittleFS.

---

## Key architecture constraints

- **GPIO conflict:** Ethernet (W5500) and CAN share SPI bus pins — only one active at compile time. See `CAN_ETHERNET_GPIO_CONFLICT_ANALYSIS.md` in ESPnowtransmitter2 if porting.
- **Touch:** GT911 controller present (GPIO4=IRQ, GPIO8/9=I2C, CH422G EXIO1=RST) but not yet wired to LVGL input device driver. Available for future implementation.
- **LVGL mutex:** All UI access must be guarded by `xSemaphoreTake(RTOS::lvgl_mutex, ...)`.
- **Dual-core layout:** ESP-NOW worker and MQTT task on APP_CPU (core 1); LVGL render task pinned to the same core to avoid GL race.

---

## Phase status

| Phase | Item | Status |
|---|---|---|
| 1–5 | Scaffold → LVGL render pipeline | ✅ Complete |
| 6 | Receiver stack port (ESP-NOW, WiFi, MQTT, OTA) | ✅ Complete |
| 7 | Codebase review + concurrency hardening | ✅ Complete 2026-04-21 |

---

## Docs

- `docs/systemworks/ESPNOWRECEIVER_LCD_FULL_CODEBASE_REVIEW_2026_04_21.md` — full review and implementation log
