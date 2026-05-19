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

## Webserver heap-fix (2026-Q2)

Root cause: HTTP page renders collapsed internal heap to ~5.5 KB during `common_styles` / `common_script_helpers` stages, starving ESP-NOW ACK processing and causing link drops.

### Changes applied

| Phase | File(s) | Change |
|---|---|---|
| 0 | `webserver.cpp` | httpd conservative coexistence profile: `task_priority`→+2, `max_open_sockets`→6, `send_wait_timeout`→15 s |
| 1 | `ota_page_content.h/cpp`, `ota_page.cpp` | Static `const char kContent[]` literal; no runtime String build |
| 1 | `settings_page.cpp` | `kSettingsContent[]`, `kSettingsScript[]` flash literals; `g_settings_content` String removed |
| 1 | `transmitter_hub_page_content.h/cpp`, `transmitter_hub_page.cpp` | `emit_` function with `_SEND_LIT`/`_SEND_STR` macros; `HubRequestData` struct replaces 6 String globals |
| 1 | `dashboard_page_content.h/cpp`, `dashboard_page.cpp` | `emit_dashboard_page_content()` with 4 RX char-buffer args; TX values baked as `---` (JS updates via `/api/dashboard_data`) |
| 2 | `page_generator.cpp`, `page_generator.h`, `page_registration_factory.cpp` | `COMMON_SCRIPT_HELPERS` moved to `/static/helpers.js` endpoint with `Cache-Control: max-age=86400`; eliminates ~12 KB inline send per request |
| 3 | `page_generator.cpp` | Preflight heap check (20 KB free + 8 KB largest block) before any render; returns `503` immediately if heap unsafe |
| 4 | `page_generator.cpp` | `webserver_on_request_start/end` accounting on all return paths in `send_rendered_page_streaming` |


## Docs

- `docs/systemworks/ESPNOWRECEIVER_LCD_FULL_CODEBASE_REVIEW_2026_04_21.md` — full review and implementation log
