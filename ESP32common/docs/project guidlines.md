
# PROJECT GUIDELINES
## Coding Standards, Architecture Rules & Best Practices
**Version:** 2.2  
**Last Updated:** April 14, 2026  
**Purpose:** Ensure all firmware in this repository is fast, readable, reliable, and maintainable using modern embedded-systems best practices.

# 1. Project Philosophy

- Clarity over cleverness.
- One responsibility per module.
- Prefer shared reusable code in `esp32common/` over duplication.
- Keep protocol behavior backward-compatible unless explicitly versioned.
- Validate changes with builds and reference checks.

## Standing Policies (apply to every step)

> **1. Legacy removal is mandatory at each step.**
> All redundant, obsolete, duplicate, or compatibility-only code introduced or superseded by a step is removed at that step's completion. Code that is merely "unused but harmless" does not survive.

> **2. This document is updated at each step completion.**
> The tracking table and step notes are updated to record exactly what was changed and what was removed. The document is the single source of truth for the state of the refactor.

> **3. Prefer a small signature adjustment over cloning.**
> When a function is *almost* reusable, adjust its signature or add a parameter rather than copying it. Cloning is only acceptable when the two functions have genuinely divergent invariants that cannot be unified without breaking consumers. Every copy-for-minor-variation is a future drift bug.


---

# 2. Repository Structure (Current Baseline)

```
ESP32Projects/
├── esp32common/                            # Shared libraries + shared docs
│   ├── docs/
│   │   ├── project guidlines.md
│   │   ├── ESP-NOW_Communication_Architecture.md
│   │   ├── ESPNOW_HEARTBEAT.md
│   │   └── MQTT_LOGGER_IMPLEMENTATION.md
│   ├── espnow_common_utils/
│   ├── espnow_transmitter/
│   ├── webserver_common_utils/
│   ├── logging_utilities/
│   └── firmware_metadata/
│
├── espnowreceiver_2/                       # Receiver (LilyGo T-Display-S3)
│   ├── PROJECT_ARCHITECTURE_MASTER.md
│   ├── platformio.ini
│   ├── src/
│   │   ├── espnow/
│   │   ├── display/
│   │   ├── mqtt/
│   │   ├── state/
│   │   ├── config/
│   │   └── main.cpp
│   ├── lib/webserver/
│   │   ├── api/
│   │   ├── pages/
│   │   ├── common/
│   │   └── utils/
│   └── data/
│
├── espnowreceiver_LCD/                     # Receiver v2 (Waveshare ESP32-S3-Touch-LCD-7)
│   ├── README.md
│   ├── platformio.ini                      # LVGL-only env: waveshare_esp32s3_lcd7_lvgl
│   ├── partitions_16mb_ota.csv             # 16 MB dual-OTA + LittleFS
│   ├── boards/waveshare_esp32s3_n16r8.json # 16 MB flash, 8 MB OPI PSRAM board def
│   ├── include/
│   │   ├── app_config.h                   # Screen + widget constants
│   │   ├── common_lcd.h                   # RTOS + ESPNow namespace scaffolding
│   │   ├── logging_config.h               # LOG_INFO/WARN/ERROR macros
│   │   ├── lv_conf.h                      # LVGL 8.4.0 compile-time config
│   │   └── task_config.h                  # FreeRTOS stack sizes + priorities
│   ├── src/
│   │   ├── main.cpp
│   │   ├── helpers.h / helpers.cpp        # smart_delay()
│   │   ├── app/demo_model.*               # Demo SOC/power cycle (Phase E: replaced)
│   │   ├── hal/lgfx_waveshare_7.h         # LovyanGFX RGB panel + CH422G expander
│   │   └── ui/runtime/                    # LVGL backend (ui_backend_lvgl.cpp)
│   └── data/                              # LittleFS image (splash JPEG)
│
├── ESPnowtransmitter2/espnowtransmitter2/  # Transmitter (Olimex ESP32-POE)
│   ├── PROJECT_ARCHITECTURE_MASTER.md
│   ├── platformio.ini
│   ├── src/
│   │   ├── espnow/
│   │   ├── network/
│   │   ├── battery/
│   │   ├── battery_emulator/
│   │   ├── communication/
│   │   ├── config/
│   │   ├── datalayer/
│   │   ├── settings/
│   │   └── main.cpp
│   └── docs/
│
└── Battery-Emulator-9.2.4/                 # Upstream/reference codebase snapshot
```

---

# 3. Repository Rules

1. Shared logic belongs in `esp32common/` whenever practical.
2. Avoid duplicate implementations across transmitter and receiver.
3. Each firmware project owns its own `platformio.ini` environments.
4. Keep web/API handlers modular.
5. Remove dead code during migrations.
6. Keep architecture/review docs aligned with behavior changes.
7. Prefer project logging utilities over ad-hoc serial prints.

---

# 4. Coding Style (C/C++)

## 3.1 Naming Conventions

| Category | Style | Example |
|---------|--------|----------|
| Types | `snake_case_t` | `component_apply_request_t` |
| Struct Types | `snake_case_t` | `device_state_t` |
| Functions | `snake_case()` | `send_component_apply_request()` |
| Variables | `snake_case` | `request_id` |
| Constants | `UPPER_SNAKE_CASE` | `MAX_URI_HANDLERS` |
| Enums | `UpperCamelCase` | `EspNowConnectionState` |
| Files | `snake_case` | `api_type_selection_handlers.cpp` |

---

## 3.2 Struct Rules

- Use this format:

```c
typedef struct {
    // fields
} name_t;
```

## 3.3 Function Rules

- One function = one responsibility.
- Prefer short functions.
- Use meaningful names.
- Use structured return values (`bool`, `esp_err_t`).
- Avoid long parameter lists; define small structs where helpful.

---

# 5. Performance & Reliability

## 5.1 ESP-NOW + WiFi Coexistence

- ESP-NOW and WiFi must share channel constraints.
- In STA mode, router channel influences radio behavior.
- State machines should govern readiness/recovery behavior.

## 5.2 Data Handling Rules

- Validate packet sizes and message types.
- Keep checksum/validation logic consistent with shared protocol definitions.
- Keep cache and state transitions deterministic.

---

# 6. FreeRTOS Task Architecture

## 6.1 Task Rules

- No direct cross-task coupling where queues/message buffers are appropriate.
- Avoid long blocking delays in runtime loops.
- Protect shared state with mutexes/atomics when needed.
- Keep stack sizes modest; avoid unnecessary large allocations.

---

# 7. ISR (Interrupt) Guidelines

Allowed in ISR (including ESP-NOW receive callback):

- Copy fixed-size data.
- Push to queue/ringbuffer (`xQueueSendFromISR`).
- Set atomic flags.
- Return immediately.

Forbidden in ISR:

- Logging.
- Dynamic memory (`malloc/free/new/delete`).
- ESP-NOW send.
- WiFi calls.
- JSON parsing/string manipulation.
- Display writes.
- SPI/I2C transactions.
- Any blocking/waiting.

---

# 8. Web/API Change Rules

- Keep API/page handlers modular.
- Centralize JSON/error responses through shared helpers.
- Validate URI handler capacity when adding endpoints.
- Remove retired endpoints and references completely.

---

# 9. Validation Requirements

After code changes:

1. Build affected project(s).
2. Verify no unresolved references or dead declarations.
3. Update relevant architecture/review docs when behavior changes.

Minimum build checks:

- Receiver (T-Display-S3): `pio run -e lilygo-t-display-s3_tft`
- Receiver LCD (Waveshare 7"): `pio run -e waveshare_esp32s3_lcd7_lvgl`
- Transmitter: `pio run -e olimex_esp32_poe2`

---

# 10. Primary References

- `esp32common/docs/ESP-NOW_Communication_Architecture.md`
- `esp32common/docs/ESPNOW_HEARTBEAT.md`
- `esp32common/docs/MQTT_LOGGER_IMPLEMENTATION.md`
- `espnowreceiver_2/PROJECT_ARCHITECTURE_MASTER.md`
- `ESPnowtransmitter2/espnowtransmitter2/PROJECT_ARCHITECTURE_MASTER.md`
- `esp32common/docs/systemworks/ESPNOWRECEIVER_LCD_PORT_ANALYSIS_2026_04_14.md` — port plan (Phases A–H)

## Hardware inventory

| Project | Board | Display | Framework | UI stack |
|---------|-------|---------|-----------|----------|
| espnowreceiver_2 | LilyGo T-Display-S3 | 320×170 ST7789 (SPI) | Arduino | TFT_eSPI direct render |
| ESPnowtransmitter2 | Olimex ESP32-POE2 | None | Arduino | None |
| espnowreceiver_LCD | Waveshare ESP32-S3-Touch-LCD-7 | 800×480 RGB (EK9716) | Arduino | **LovyanGFX + LVGL 8.4.0** |

`espnowreceiver_LCD` key hardware facts:
- GT911 capacitive touch (5-point), GPIO4=TP_IRQ, I2C on GPIO8/9
- CH422G I/O expander (I2C 0x24): EXIO1=TP_RST, EXIO2=LCD_BL, EXIO3=LCD_RST
- 16 MB Flash, 8 MB OPI PSRAM; dual-OTA partitions + LittleFS
- Touch input driver not yet implemented; available via LVGL `lv_indev_drv_t`