# espnowreceiver_LCD

Waveshare ESP32-S3-Touch-LCD-7 display project — **Arduino + LovyanGFX + LVGL 8.4.0**.

## Scope

- LVGL 8.4.0 rendering pipeline (active and default)
- Simulated LED indicator (continuous / flash / heartbeat) — LVGL `lv_obj` circle
- SOC large numeric display — LVGL label with colour gradient
- Bidirectional power bar — LVGL objects with animation
- Medium power text — LVGL label
- No webserver / MQTT / ESP-NOW / receiver stack (see port analysis doc)

### Touch screen

The board includes a **GT911 capacitive touch controller** (GPIO4=TP_IRQ, GPIO8/9=I2C, CH422G EXIO1=TP_RST). Touch is **not yet implemented** but is available for future use via the LVGL pointer input device driver (`lv_indev_drv_t`). See `docs/systemworks/ESPNOWRECEIVER_LCD_PORT_ANALYSIS_2026_04_14.md` section 3.6.

## Build

```bash
pio run -j 2
```

## Phase status

- Phase 1: ✅ scaffold + RGB panel baseline bring-up
- Phase 2: ✅ primitive rendering
- Phase 3: ✅ widgets
- Phase 4: ✅ autonomous demo model
- Phase 5: ✅ LVGL migration — LVGL-only path active, legacy non-LVGL code removed
- Phase 6: ⏳ receiver stack port (ESP-NOW, WiFi, MQTT, OTA — see port analysis doc)

## Important

`src/hal/lgfx_waveshare_7.h` uses a phased baseline RGB/CH422G mapping.
Verify all pins/timings against the official Waveshare 7-inch schematic before production flashing.
