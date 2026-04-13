# espnowreceiver_LCD

Minimal Waveshare ESP32-S3-Touch-LCD-7 display project (Arduino + LovyanGFX).

## Scope

- Simulated LED (continuous / flash / heartbeat)
- SOC large numeric display
- Bidirectional power bar
- Medium power text
- No webserver / MQTT / ESP-NOW / receiver stack

## Build

```bash
pio run -j 2
```

## Phase status

- Phase 1: ✅ scaffold + RGB panel baseline bring-up
- Phase 2: ✅ primitive rendering
- Phase 3: ✅ widgets
- Phase 4: ✅ autonomous demo model
- Phase 5: ⏳ panel fit/polish + hardware diagnostics (color-cycle + dual serial) + final pin verification

## Important

`src/hal/lgfx_waveshare_7.h` uses a phased baseline RGB/CH422G mapping.
Verify all pins/timings against the official Waveshare 7-inch schematic before production flashing.
