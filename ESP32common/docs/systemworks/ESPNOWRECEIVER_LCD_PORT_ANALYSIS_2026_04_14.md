# espnowreceiver_LCD — Full Port Analysis from espnowreceiver_2
**Date:** 2026-04-14  
**Author:** Code analysis (GitHub Copilot)  
**Scope:** How to port the complete feature set of `espnowreceiver_2` into `espnowreceiver_LCD`, covering ESP-NOW, WiFi, FreeRTOS task architecture, webserver, MQTT, OTA, NVS configuration, and the expanded display model. Includes framework choice recommendation (Arduino vs ESP-IDF).

---

## 1. Executive Summary

`espnowreceiver_LCD` is currently a display-only proof-of-concept: a demo model cycles fake SOC and power values through a clean `UI::Runtime::set_state(soc, power)` LVGL interface. It has no networking, no tasks, no persistence, and no real data.

`espnowreceiver_2` is the full production receiver: it runs six FreeRTOS tasks, a complete ESP-NOW state machine with heartbeat and discovery, a multi-page webserver with SSE and REST API, MQTT pub/sub, NVS-backed WiFi and MQTT configuration, LittleFS, OTA boot-guard, and a rich multi-section telemetry data model covering battery, charger, inverter, and system status.

The good news is that the LCD project's `UI::Runtime` layer is already a clean abstraction boundary. Everything above that boundary (`set_state`, `tick`) can remain almost unchanged; the entire `espnowreceiver_2` infrastructure can be grafted below or beside it.

The port is large but well-structured. The critical issues are **PSRAM budget management**, **LVGL thread-safety** across a multi-task architecture, a **narrower current display interface** than the rich telemetry model requires, **partition table** alignment with the 16 MB board, and a **backlight capability gap** (no hardware PWM dimming path on the Waveshare board).

---

## 2. Codebase Comparison

### 2.1 espnowreceiver_2 — Feature inventory

| Layer | Components |
|---|---|
| Board | LilyGo T-Display-S3 — 320×170 ST7789, 8-bit parallel, LEDC backlight PWM GPIO38 |
| Display library | TFT_eSPI, JPEGDecoder |
| UI | Custom widgets (power bar, SOC arc, LED dot), TFT_eSPI rendering |
| FreeRTOS | 6 pinned tasks: ESPNowWorker, DisplayRenderer, MqttClient, LedRenderer, MemorySampler, + discovery task |
| ESP-NOW | Full stack: recv/send callbacks → message queue → worker task, channel manager, connection manager, heartbeat manager, RX state machine, peer manager, discovery |
| WiFi | Station mode, static IP, config from NVS |
| Webserver | ESP-IDF `httpd`, multi-page REST API, SSE, settings pages, transmitter manager |
| MQTT | PubSubClient, sub/pub, type catalog, cell data, event log subscriptions |
| Config persistence | NVS via `ReceiverNetworkConfig` (WiFi, MQTT, battery/inverter type) |
| Filesystem | LittleFS — webserver assets + splash JPEG |
| OTA | `OtaBootGuard`, `SetupHealthGate`, `BootstrapPhaseRunner` |
| Shared library | `esp32common` — `channel_manager`, `connection_manager`, `espnow_discovery`, `firmware_version`, `timing_config`, `runtime_common_utils` |
| Data model | `BatteryData::TelemetrySnapshot` — SOC, voltage, current, temp, power, BMS status, charger, inverter, system, cell data |
| Partitions | Custom 16 MB OTA: 2× 7.8 MB app, 704 KB SPIFFS |

### 2.2 espnowreceiver_LCD — Current state

| Layer | Current state |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-7 — 800×480 RGB, 8 MB OPI PSRAM, 16 MB Flash |
| Display library | LovyanGFX 1.2.x |
| UI | LVGL 8.4.0, clean `UI::Runtime::{init, run_startup_sequence, set_state, tick}` API |
| FreeRTOS | None — single-threaded Arduino `setup()` / `loop()` |
| ESP-NOW | None |
| WiFi | None |
| Webserver | None |
| MQTT | None |
| Config persistence | None |
| Filesystem | SPIFFS (splash JPEG only) |
| OTA | None |
| Shared library | Not referenced |
| Data model | `DemoModel` — fake SOC + power cycle only |
| Partitions | Default 8 MB board profile (insufficient) |

---

## 3. Hardware Differences and Constraints

### 3.1 Display interface (critical)

| | T-Display-S3 | Waveshare-7 |
|---|---|---|
| Controller | ST7789 | EK9716 (RGB parallel) |
| Interface | 8-bit parallel SPI | 16-bit RGB (HSYNC/VSYNC/PCLK) |
| Resolution | 320 × 170 | 800 × 480 |
| Display library | TFT_eSPI | LovyanGFX |
| PSRAM needed | No | Yes — double framebuffer required |
| Framebuffer size | N/A | ~1.5 MB (800×480×2 bytes × 2 buffers) |

The display libraries, drivers, and GPIO assignments are completely different and cannot be shared. The LCD project's display layer stays as-is; none of `espnowreceiver_2`'s display code comes across.

### 3.2 Backlight

The T-Display-S3 exposes backlight on GPIO38 with direct LEDC hardware PWM — smooth software-controllable dimming. The Waveshare-7 routes backlight through a CH422G I2C IO expander (EXIO2 = DISP), giving only digital on/off. This difference is already handled in `splash_sequence.cpp`. No further action needed for the port, but it means any status-LED PWM effects planned for the backlight won't be possible.

### 3.3 Status LED

Re-review confirms the T-Display-S3 receiver project does **not** use a physical status LED. In `src/display/display_led.cpp`, `set_led()` and `clear_led()` render a coloured circle directly on the TFT (`tft.fillCircle(...)`).

So both projects use an on-screen status indicator concept. The migration task is therefore API adaptation (TFT draw calls → LVGL object updates), not hardware replacement.

### 3.4 PSRAM budget

The Waveshare board has 8 MB OPI PSRAM. The display alone consumes a large chunk:

| Consumer | Size |
|---|---|
| RGB double framebuffer (LovyanGFX `use_psram=2`) | ~1.5 MB |
| LVGL draw buffers (2 × 800×120×2) | ~375 KB |
| LVGL heap (fonts, widgets, styles) | ~200–400 KB (estimate) |
| **Display total** | **~2.3 MB** |
| MQTT buffer (`setBufferSize(6144)`) | 6 KB |
| ArduinoJson scratch (cell data, 128 entries) | ~10–20 KB |
| Webserver httpd buffers | ~30–60 KB |
| FreeRTOS task stacks (all 6 tasks) | ~30 KB |
| **Non-display total** | **~120–150 KB** |
| **Available headroom** | **~5.5 MB** |

PSRAM headroom is comfortable. The main risk is internal IRAM/SRAM: the LCD project currently uses 46.5% (152 KB / 328 KB) with just the display. Each new FreeRTOS task stack defaults to IRAM unless explicitly directed to PSRAM. Task stacks should be placed in PSRAM using `pvPortMallocCaps` or `heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM)` for large stacks (MQTT 10 KB, ESPNow 4 KB, Display 4 KB).

### 3.5 Flash and partitions

The current `waveshare_esp32s3_n16r8.json` board file specifies **8 MB flash** — but the physical board has **16 MB**. The `espnowreceiver_2` OTA partition table uses a 16 MB layout. The board JSON **must be corrected** and a custom partition CSV added before any meaningful OTA or webserver asset storage works.

---

## 4. Architectural Issues

### 3.6 Touch screen — available but not yet implemented

The Waveshare ESP32-S3-Touch-LCD-7 includes a **GT911 capacitive touch controller** that is absent from the T-Display-S3. This means the LCD project has a significant capability not available in `espnowreceiver_2`.

**GPIO mapping (confirmed from Waveshare documentation):**

| Signal | GPIO / Expander pin |
|---|---|
| TP_IRQ | GPIO4 |
| TP_SDA | GPIO8 (shared I2C bus with CH422G) |
| TP_SCL | GPIO9 (shared I2C bus with CH422G) |
| TP_RST | CH422G EXIO1 |

**Current state:** Touch is not implemented. The HAL (`lgfx_waveshare_7.h`) does set TP_RST HIGH as part of the initial CH422G state during `init_impl`, but no `lgfx::Touch_GT911` object is declared or attached, and no LVGL input device driver (`lv_indev_drv_t`) is registered in `ui_backend_lvgl.cpp`.

**What would be needed to enable it:**
1. Add an `lgfx::Touch_GT911` member to `LGFX_Waveshare7` and call `panel_.touch(&touch_)` in the constructor (I2C address 0x14, INT on GPIO4, I2C already initialised on the shared bus).
2. Register an LVGL pointer input device driver in `ui_backend_lvgl.cpp` via `lv_indev_drv_init` / `lv_indev_drv_register`, with a read callback delegating to `display_->getTouch()`.
3. Enable `LV_USE_BTN=1` in `lv_conf.h` for any tappable LVGL widgets.

This is entirely self-contained and does not interact with any of the port phases above. It could be implemented at any point as an independent feature addition.

---

## 4. Architectural Issues

### Issue 1: LVGL is not thread-safe — highest risk item

**Severity: Critical**

`espnowreceiver_2` calls all display updates through `RTOS::tft_mutex` because TFT_eSPI has no internal locking. LVGL 8.x has the same constraint but enforces it differently: all LVGL calls (`lv_obj_*`, `lv_label_set_text`, `lv_timer_handler`) **must occur on the same task/thread**. Calling any LVGL API from the ESP-NOW worker task or MQTT task will corrupt the object tree.

The current LCD architecture calls `UI::Runtime::tick()` (which calls `lv_timer_handler()`) from Arduino `loop()`, which runs on Core 1. When FreeRTOS tasks are added, Core 1 keeps running the `loop()` task at priority 1.

**Recommended solution:** Introduce an explicit LVGL mutex, wrap every LVGL call in a lock, and tick LVGL only from a single dedicated task. The clean pattern is:

```
loop() → demoted to a thin coordinator, or eliminated
LVGL Task (Core 1, priority 2) → owns lv_timer_handler() + UI::Runtime::tick()
ESP-NOW Worker (Core 1, priority 3) → posts to a display snapshot queue
MQTT / webserver tasks → post to the same snapshot queue

Snapshot queue → consumed by LVGL Task → calls UI::Runtime::set_state() → lv_timer_handler()
```

This is exactly the `DisplayUpdateQueue` pattern already proven in `espnowreceiver_2`. Port it directly, replacing `display_soc()` / `display_power()` with `UI::Runtime::set_state()`.

### Issue 2: `set_state(soc, power)` is too narrow for the full data model

**Severity: High**

The current `UI::Runtime::set_state(float soc_percent, int32_t power_w)` signature only carries two fields. The `BatteryData::TelemetrySnapshot` has ~30 fields across five sections (battery status, info, charger, inverter, system). The Waveshare screen is 800×480 — more than 7× the area of the T-Display-S3 — so there is screen real estate to show much more.

**Recommended solution:** Evolve `set_state` in two steps:

1. **Phase 1 (port unblock):** Keep the two-argument signature for now. This gets real data showing immediately. Display additional fields as "N/A" placeholders.  
2. **Phase 2 (data expansion):** Replace with a `TelemetrySnapshot` struct parameter (either a copy or a `const&` under the LVGL lock). Add display panels for voltage, current, temperature, charger, inverter status progressively.

### Issue 3: `common.h` couples everything to TFT_eSPI

**Severity: High**

`espnowreceiver_2`'s `common.h` includes `<TFT_eSPI.h>`, declares the global `TFT_eSPI tft` object, and scatters display state into `namespace Display`. This header is included by almost every translation unit. None of this is portable to the LCD project.

**Recommended solution:** When porting the non-display subsystems, do not bring `common.h` across. Instead, create a new `common_lcd.h` (or equivalent) that:
- Omits all TFT/display includes
- Replaces `TFT_eSPI tft` with nothing (display access goes through `UI::Runtime`)
- Keeps `namespace ESPNow` (queue, MAC, metrics)
- Keeps `namespace RTOS` (mutex handles, task handles)
- Replaces `namespace Display` with a reference to `BatteryData::TelemetrySnapshot`

### Issue 4: LED indicator task is display-rendered and needs API adaptation

**Severity: Medium**

`task_led_renderer` in `espnowreceiver_2` animates a **drawn** status dot by calling `set_led()`/`clear_led()`, which write circles into the TFT framebuffer. It does not drive a physical RGB LED.

This means the behavior can port directly, but the rendering backend must change from TFT primitives to LVGL object state.

**Recommended solution:** Keep the existing LED state/effect model and either:
1. keep a dedicated `LedRenderer` task that publishes indicator-state events to the LVGL/display queue, or
2. fold the animation timing into the LVGL task timers.

In both options, render via a lightweight LVGL `ConnectionStatusWidget` (small coloured circle: orange waiting, green connected, red error), not direct TFT calls.

### Issue 5: Board JSON flash size is wrong

**Severity: High**

`waveshare_esp32s3_n16r8.json` declares `"flash_size": "8MB"` and `"maximum_size": 8388608`. The physical Waveshare ESP32-S3-Touch-LCD-7 has **16 MB flash**. Running with the 8 MB profile on a 16 MB board:
- Silently wastes 8 MB of flash
- Limits OTA app partitions to ~3.5 MB each (too small for a production image with webserver assets)
- Webserver HTML/CSS assets stored in SPIFFS will be constrained

**Fix:** Update the board JSON to 16 MB and add `partitions_16mb_ota.csv` matching the layout used in `espnowreceiver_2`.

### Issue 6: Filesystem — SPIFFS vs LittleFS

**Severity: Medium**

The LCD project uses SPIFFS (for the splash JPEG). `espnowreceiver_2` uses LittleFS (for webserver assets, firmware metadata, config). SPIFFS is deprecated in newer ESP-IDF versions and has no wear-levelling or directory support. LittleFS is strictly better.

**Recommended solution:** Migrate to LittleFS in the LCD project before adding webserver assets. Replace `SPIFFS.begin(true)` with `LittleFS.begin(true)` in `splash_sequence.cpp`. Change `board_build.filesystem = littlefs` in `platformio.ini`. The splash JPEG will work identically.

### Issue 7: esp32common not referenced

**Severity: High**

All the shared infrastructure (`channel_manager`, `connection_manager`, `espnow_discovery`, `firmware_version`, `ota_boot_guard`, `timing_config`, `bootstrap_phase_runner`, `setup_health_gate`, `runtime_common_utils`) lives in `esp32common` and is pulled in via `lib_extra_dirs = ../esp32common`. The LCD project doesn't reference it.

**Fix:** Add `lib_extra_dirs = ../esp32common` to `platformio.ini` and add the firmware metadata/version script:
```ini
extra_scripts = pre:../esp32common/scripts/version_firmware.py
```

### Issue 8: NVS config infrastructure absent

**Severity: High**

`espnowreceiver_2` uses `ReceiverNetworkConfig` (NVS-backed) for all WiFi credentials, MQTT broker, battery/inverter type selection. The LCD project has no NVS config layer at all.

**Recommended solution:** Port `lib/receiver_config/receiver_config_manager.h/.cpp` directly — it has no display dependencies. This gives the LCD project the same NVS-backed config that feeds the WiFi setup and MQTT client.

### Issue 9: WiFi and ESP-NOW coexistence on same channel

**Severity: Medium (known, managed)**

`espnowreceiver_2` manages this carefully: it connects WiFi to a known AP on a fixed channel, then runs ESP-NOW on the same channel using `WIFI_STA` mode. `esp_wifi_set_ps(WIFI_PS_NONE)` prevents power-save from disrupting ESP-NOW reception. The channel is tracked by `ChannelManager`. All of this must be preserved on the LCD port.

The key risk is that the webserver and ESP-NOW must share the WiFi radio — if WiFi drops, ESP-NOW stops too. This is inherent to the architecture and already handled by the state machine, but the port must ensure `esp_wifi_set_ps(WIFI_PS_NONE)` is still called after WiFi init.

### Issue 10: Task core affinity and PSRAM-allocated stacks

**Severity: Medium**

`espnowreceiver_2` pins all worker tasks to Core 1 (`WORKER_CORE = 1`), leaving Core 0 for the WiFi/BLE stack. This must be preserved. Additionally, large task stacks (MQTT at 10 KB, ESPNow worker at 4 KB, Display renderer at 4 KB) should be allocated in PSRAM rather than internal SRAM to preserve the ~175 KB of internal SRAM headroom.

PSRAM-backed task stacks require creating the task with `xTaskCreatePinnedToCore` using a stack buffer allocated via `heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`. This is a minor plumbing change to `RuntimeTaskStartup`.

### Issue 11: LVGL timer and watchdog during splash

**Severity: Low-Medium**

The current `run_splash_sequence()` runs entirely in `setup()` before any FreeRTOS tasks exist, calling `delay()` directly — this is fine. Once FreeRTOS tasks are running, calling the splash or any long blocking sequence from `setup()` will still work because the scheduler hasn't started yet at that point. However, with `BootstrapPhaseRunner`, phases run in `setup()` in order — if the splash runs in phase 2 and the FreeRTOS scheduler has been started in a prior phase, `delay()` calls in the splash must become `vTaskDelay()` calls. The existing `smart_delay()` utility from `espnowreceiver_2` handles this correctly and should be ported.

### Issue 12: webserver `lib/webserver` path coupling

**Severity: Medium**

`espnowreceiver_2`'s main.cpp includes `"../lib/webserver/webserver.h"` with a relative path. This works fine for that project. When porting, the webserver library should either be promoted to `esp32common/webserver_common_utils` (if it is genuinely shared) or kept as `lib/webserver` within the LCD project with the same relative path structure. It should **not** be duplicated.

Inspection shows the webserver has receiver-specific pages and API endpoints — it is not fully generic. Recommendation: keep it as a local `lib/webserver` directory within the LCD project.

---

## 5. Port Plan — Recommended Phase Sequence

Each phase should build and validate independently before the next begins.

### Phase A: Foundation ✅ COMPLETE — 2026-04-14

> **Status:** All items implemented and build validated clean (`[SUCCESS]`).

| Item | File(s) | Notes |
|------|---------|-------|
| Board JSON: 8 MB → 16 MB flash | `boards/waveshare_esp32s3_n16r8.json` | `flash_size`, `maximum_size` corrected; stale `default_8MB.csv` ref removed |
| Partition table | `partitions_16mb_ota.csv` (new) | NVS + OTA0/1 (7.8 MB each) + LittleFS (704 KB) |
| `platformio.ini` | `platformio.ini` | `board_build.flash_size=16MB`, partitions, `filesystem=littlefs`, `lib_extra_dirs`, version script, `FW_VERSION_*`, `LOG_USE_MQTT=0` |
| SPIFFS → LittleFS | `src/ui/runtime/splash_sequence.cpp` | All 5 SPIFFS references replaced with LittleFS |
| Logging macros | `include/logging_config.h` (new) | Standalone `LOG_INFO/WARN/ERROR/DEBUG/TRACE`; no MQTT dep in Phase A |
| FreeRTOS task constants | `include/task_config.h` (new) | Stack sizes, priorities, `WORKER_CORE`; adapted for LCD |
| `smart_delay()` | `src/helpers.h`, `src/helpers.cpp` (new) | Scheduler-aware delay for Phase B+ |
| Runtime namespace scaffolding | `include/common_lcd.h` (new) | `namespace RTOS` + `namespace ESPNow`; no display imports |

**Validation:** `pio run -j 2` — `[SUCCESS]`, zero errors, LittleFS library compiled.

~~1. Correct the board JSON — update flash to 16 MB.~~
~~2. Add `partitions_16mb_ota.csv`.~~
~~3. Migrate SPIFFS → LittleFS.~~
~~4. Add `lib_extra_dirs = ../esp32common` and version script.~~
~~5. Port `task_config.h` — FreeRTOS task sizing constants.~~
~~6. Port `logging_config.h` — LOG macros with compile-time level.~~
~~7. Port `src/helpers.h` / `helpers.cpp` — `smart_delay()`.~~
~~8. Create `common_lcd.h` — `namespace ESPNow`, `namespace RTOS`, no display imports.~~

### Phase B: FreeRTOS task structure
1. **Extract `UI::Runtime::tick()` into a dedicated LVGL Task** pinned to Core 1.  
2. **Port `DisplayUpdateQueue`** — change renderer callback from `display_soc()` / `display_power()` to `UI::Runtime::set_state()`.  
3. **Port `RuntimeTaskStartup`** — create primitives (LVGL mutex, ESP-NOW queue), start LVGL task. No ESP-NOW tasks yet.  
4. **Refactor `loop()`** — should now just call `vTaskDelay(portMAX_DELAY)` or tick only non-display state.  
5. **Restructure `setup()`** using `BootstrapPhaseRunner`.  

**Validation:** LVGL display still updates correctly from the display snapshot queue. Demo model data posts to queue → LVGL task renders it.

### Phase C: WiFi + NVS config
1. **Port `lib/receiver_config/receiver_config_manager.h/.cpp`**.  
2. **Port `src/config/wifi_setup.h/.cpp`** — uses `ReceiverNetworkConfig`.  
3. **Add `bootstrap_filesystem()` phase** — LittleFS mount + `ReceiverNetworkConfig::loadConfig()` + `setupWiFi()`.  
4. **Add `esp_wifi_set_ps(WIFI_PS_NONE)`** after WiFi init.  

**Validation:** Board connects to WiFi, IP visible on serial, LVGL display still works.

### Phase D: Webserver
1. **Copy `lib/webserver/`** into LCD project (or symlink — PlatformIO supports both).  
2. **Port `lib/webserver/utils/transmitter_manager.*`** and supporting utils.  
3. **Add `bootstrap_services()` phase** — `ReceiverConfigManager::init()`, `TransmitterManager::init()`, `init_webserver()`.  
4. **Add `CONFIG_HTTPD_MAX_URI_LEN` and `CONFIG_HTTPD_MAX_REQ_HDR_LEN`** to build flags.  

**Validation:** Webserver responds on expected IP. LVGL display unaffected.

### Phase E: ESP-NOW
1. **Port `src/espnow/` subsystem** — callbacks, tasks, state machines, heartbeat manager, connection handler, peer manager.  
2. **Port `esp_now_init()` + callback registration** into `bootstrap_espnow_state()` phase.  
3. **Wire `update_received_data_cache()` → `DisplayUpdateQueue::enqueue()`** so real data flows to LVGL.  
4. **Remove `DemoModel`** once real data is verified.  
5. **Replace `task_led_renderer`** with `ConnectionStatusWidget` LVGL object.  

**Validation:** Transmitter connected → SOC and power values appear on display.

### Phase F: MQTT
1. **Port `src/mqtt/`** (`mqtt_client.*`, `mqtt_task.*`).  
2. **Add `MqttClient` task** in `RuntimeTaskStartup`.  
3. **Wire MQTT subscription handlers** to `BatteryData::TelemetrySnapshot` updates.  

**Validation:** MQTT broker receives telemetry topics.

### Phase G: OTA + health gate
1. **Port `OtaBootGuard`** from esp32common — it's already a library component.  
2. **Port `SetupHealthGate`** and `BootstrapPhaseRunner` — also already in esp32common.  
3. **Add OTA health checks** (heap, LVGL mutex, ESP-NOW queue).  

**Validation:** OTA firmware update completes successfully.

### Phase H: Display expansion
1. **Extend `UI::Runtime::set_state`** to accept full `TelemetrySnapshot` (or a dedicated LCD display struct).  
2. **Add LVGL panels** for voltage, current, temperature, charger status, inverter status, system status using the available 800×480 screen real estate.  
3. **Add cell data display** if desired (requires MQTT cell_data subscription).  

---

## 6. Files to Port (by subsystem)

### From `espnowreceiver_2/src/`
| Source file | Destination | Notes |
|---|---|---|
| `config/task_config.h` | `include/task_config.h` | Update stack sizes for PSRAM allocation |
| `config/logging_config.h` | `include/logging_config.h` | Direct port |
| `config/wifi_setup.h/.cpp` | `src/config/wifi_setup.*` | Replace Config:: WiFi with ReceiverNetworkConfig |
| `config/littlefs_init.h/.cpp` | `src/config/littlefs_init.*` | Rename SPIFFS → LittleFS |
| `config/runtime_task_startup.h/.cpp` | `src/config/runtime_task_startup.*` | Replace display renderer with LVGL task |
| `helpers.h/.cpp` | `src/helpers.*` | `smart_delay()`, `handle_error()` |
| `state_machine.h/.cpp` | `src/state_machine.*` | Direct port |
| `state/connection_state.h` | `src/state/connection_state.h` | Direct port |
| `espnow/espnow_callbacks.*` | `src/espnow/espnow_callbacks.*` | Remove TFT refs |
| `espnow/espnow_tasks.*` | `src/espnow/espnow_tasks.*` | Replace display_soc/display_power with queue |
| `espnow/espnow_tasks_internal.h` | `src/espnow/espnow_tasks_internal.h` | Direct port |
| `espnow/espnow_message_handlers.*` | `src/espnow/espnow_message_handlers.*` | Direct port |
| `espnow/espnow_send.*` | `src/espnow/espnow_send.*` | Direct port |
| `espnow/espnow_settings_sync.*` | `src/espnow/espnow_settings_sync.*` | Direct port |
| `espnow/rx_connection_handler.*` | `src/espnow/rx_connection_handler.*` | Direct port |
| `espnow/rx_heartbeat_manager.*` | `src/espnow/rx_heartbeat_manager.*` | Direct port |
| `espnow/rx_state_machine.*` | `src/espnow/rx_state_machine.*` | Direct port |
| `espnow/battery_data_store.*` | `src/espnow/battery_data_store.*` | Direct port |
| `espnow/battery_handlers.*` | `src/espnow/battery_handlers.*` | Direct port |
| `espnow/battery_settings_cache.*` | `src/espnow/battery_settings_cache.*` | Direct port |
| `espnow/component_apply_tracker.*` | `src/espnow/component_apply_tracker.*` | Direct port |
| `espnow/component_config_handler.*` | `src/espnow/component_config_handler.*` | Direct port |
| `espnow/type_catalog_cache.*` | `src/espnow/type_catalog_cache.*` | Direct port |
| `espnow/handlers/*` | `src/espnow/handlers/*` | Direct port |
| `mqtt/mqtt_client.*` | `src/mqtt/mqtt_client.*` | Direct port |
| `mqtt/mqtt_task.*` | `src/mqtt/mqtt_task.*` | Direct port |
| `memory/memory_sampler.*` | `src/memory/memory_sampler.*` | Direct port |

### From `espnowreceiver_2/lib/`
| Source | Destination | Notes |
|---|---|---|
| `lib/webserver/` (entire tree) | `lib/webserver/` | Direct copy |
| `lib/receiver_config/receiver_config_manager.*` | `lib/receiver_config/` | Direct port |

### Files to create new (no equivalent in `espnowreceiver_2`)
| New file | Purpose |
|---|---|
| `src/common_lcd.h` | Replaces `common.h`; no display includes, no TFT globals |
| `src/ui/widgets/connection_status_widget.*` | LVGL on-screen connection status indicator (replaces TFT circle draw path) |
| `src/config/display_queue_bridge.*` | Thin adapter: `DisplayUpdateQueue` → `UI::Runtime::set_state()` |
| `partitions_16mb_ota.csv` | Copied/adapted from `espnowreceiver_2` |

### Files to remove/retire
| File | Reason |
|---|---|
| `src/app/demo_model.*` | Replaced by real ESP-NOW data once Phase E is validated |

---

## 7. Key Code Changes Detail

### 7.1 Display snapshot queue bridge

In `espnowreceiver_2`, `DisplayUpdateQueue::task_renderer` calls:
```cpp
display_soc(static_cast<float>(snapshot.soc));
display_power(snapshot.power_w);
```

In the LCD port, this becomes:
```cpp
// Must be called from the LVGL task only
UI::Runtime::set_state(static_cast<float>(snapshot.soc), snapshot.power_w);
// lv_timer_handler() is driven in the same LVGL task loop
```

The LVGL task loop pattern:
```cpp
static void task_lvgl(void* param) {
    for (;;) {
        DisplayUpdateQueue::Snapshot snap = {};
        if (xQueueReceive(s_snapshot_queue, &snap, pdMS_TO_TICKS(10)) == pdTRUE) {
            // All LVGL API calls happen here, on this task only
            UI::Runtime::set_state(static_cast<float>(snap.soc), snap.power_w);
        }
        UI::Runtime::tick(millis());  // drives lv_timer_handler()
    }
}
```

### 7.2 LVGL mutex strategy

LVGL 8.x can be made multi-task safe with an `lv_port_disp_init()` that installs a lock/unlock callback, or by ensuring all LVGL API calls occur on one task. The single-task approach above (all LVGL on `task_lvgl`) is simpler and sufficient. No mutex is needed inside the LVGL task itself.

The existing `RTOS::tft_mutex` from `espnowreceiver_2` can be repurposed as the LVGL access guard if any other task ever needs to post directly to LVGL (e.g., an OTA progress callback). In normal operation this should not be needed.

### 7.3 `platformio.ini` additions

```ini
board_build.filesystem = littlefs
board_build.partitions = partitions_16mb_ota.csv
board_build.flash_size = 16MB

extra_scripts = pre:../esp32common/scripts/version_firmware.py
lib_extra_dirs = ../esp32common

lib_deps =
    lovyan03/LovyanGFX@^1.2.7
    lvgl/lvgl@^8.4.0
    knolleary/PubSubClient@^2.8
    bblanchon/ArduinoJson@^6.21.5

build_flags =
    ... (existing flags) ...
    -DRECEIVER_DEVICE
    -DFW_VERSION_MAJOR=3
    -DFW_VERSION_MINOR=0
    -DFW_VERSION_PATCH=0
    -DTARGET_DEVICE=RECEIVER_LCD
    -DCOMPILE_LOG_LEVEL=LOG_INFO
    -DCONFIG_HTTPD_MAX_URI_LEN=1024
    -DCONFIG_HTTPD_MAX_REQ_HDR_LEN=2048
```

### 7.4 PSRAM task stacks

In `RuntimeTaskStartup::create_task_or_fail`, for large stacks:
```cpp
// Allocate stack in PSRAM to preserve internal SRAM
StaticTask_t* task_buf = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
StackType_t* stack_buf = (StackType_t*)heap_caps_malloc(task.stack, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
TaskHandle_t handle = xTaskCreateStaticPinnedToCore(task.task_fn, task.name,
    task.stack / sizeof(StackType_t), NULL, task.priority, stack_buf, task_buf, WORKER_CORE);
```

### 7.5 `main.cpp` structure for LCD port

Following the `BootstrapPhaseRunner` pattern from `espnowreceiver_2`:
```cpp
void setup() {
    static const BootstrapPhaseRunner::Phase kPhases[] = {
        {"hardware",        bootstrap_hardware},       // Serial, backlight suppress, OTA guard
        {"display",         bootstrap_display},        // LovyanGFX init, LVGL init
        {"splash",          bootstrap_splash},         // run_splash_sequence()
        {"filesystem",      bootstrap_filesystem},     // LittleFS + ReceiverNetworkConfig + WiFi
        {"services",        bootstrap_services},       // ReceiverConfigManager, webserver, esp_now_init
        {"tasks",           bootstrap_tasks},          // create primitives, start LVGL task + all workers
        {"espnow_state",    bootstrap_espnow_state},   // state machines, callbacks, initial state
    };
    BootstrapPhaseRunner::run_phases(kPhases, sizeof(kPhases) / sizeof(kPhases[0]));
}

void loop() {
    RxHeartbeatManager::instance().tick();
    ReceiverConnectionHandler::instance().tick();
    SystemStateManager::instance().update();
    smart_delay(10);
}
```

---

## 8. Arduino Framework vs ESP-IDF — Recommendation

### 8.1 What `espnowreceiver_2` actually uses from Arduino

| Feature | Arduino API | Alternative |
|---|---|---|
| `setup()` / `loop()` | Yes | `app_main()` + tasks in IDF |
| `Serial.print()` | Yes | `esp_log_*` in IDF |
| `delay()` / `millis()` | Yes | `vTaskDelay()` / `esp_timer_get_time()` |
| `WiFi.h` | Yes | `esp_wifi_*` event loop in IDF |
| `Preferences` (NVS) | Yes | `nvs_flash_*` in IDF (same underlying API) |
| `SPIFFS` / `LittleFS` | Yes | `esp_littlefs` in IDF |
| `LovyanGFX` | Supported on both | — |
| `LVGL 8.x` | Supported on both | — |
| `PubSubClient` | Arduino only | `esp-mqtt` IDF component |
| `esp_now_*` | Both (same API) | — |
| `esp_http_server.h` | Already using IDF directly | — |
| FreeRTOS (`xTaskCreate`, `xQueueCreate`) | Both (same API) | — |

### 8.2 Assessment

The codebase is already **"ESP-IDF lite"**: it uses `esp_now.h`, `esp_http_server.h`, `esp_wifi.h`, and FreeRTOS APIs directly. Arduino is only used for Serial, WiFi.h (a thin C++ wrapper over esp_wifi), delay/millis, and the setup/loop paradigm. The overhead of the Arduino layer on an ESP32-S3 is negligible (~10 KB Flash, minimal RAM).

**Recommendation: Stay on Arduino framework for the port.**

Reasons:
1. **Migration cost is high, benefit is marginal.** Replacing `WiFi.h` with the IDF event loop, `Serial` with `esp_log`, and `setup()`/`loop()` with `app_main()` + `esp_event_loop` involves touching every file without any functional improvement.
2. **LovyanGFX + LVGL are better tested in Arduino context.** Both libraries work under IDF, but the Arduino path has more community examples and more stable PlatformIO integration.
3. **PubSubClient is Arduino-only.** Migrating to IDF's `esp-mqtt` component would be non-trivial.
4. **The existing architecture already bypasses the Arduino abstractions** for everything that matters (ESP-NOW, webserver, WiFi power management, FreeRTOS). Removing the Arduino shell gains nothing.

**When a future IDF migration would make sense:**
- If `PubSubClient` needs to be replaced (e.g., for TLS/MQTT5 support) with `esp-mqtt`
- If deeper WiFi power management (DTIM, modem sleep) is needed
- If the project grows to use ESP-IDF components unavailable in Arduino (e.g., `esp_netif` fine control, `esp_event` system-wide bus)

Even then, the migration could be done incrementally (IDF component + `ARDUINO_RUNNING_CORE` = none) rather than a full rewrite.

---

## 9. Risk Register

| Risk | Severity | Likelihood | Mitigation |
|---|---|---|---|
| LVGL corruption from off-task API calls | Critical | High if not addressed | Single LVGL task pattern; no LVGL calls outside it |
| Internal SRAM exhaustion from task stacks | High | Medium | PSRAM-backed task stacks for large tasks |
| PSRAM contention causing RGB display tearing | High | Low (already tuned) | 14 MHz pixel clock, double framebuffer already in place |
| ESP-NOW packet loss during WiFi reconnect | Medium | Low | Channel manager already handles this |
| Webserver blocking during large SSE/JSON responses | Medium | Low | Memory sampler burst mode already limits this |
| OTA failing on wrong partition size | High | High without fix | Correct board JSON and partition CSV first |
| LittleFS mount failure if SPIFFS data present | Low | Low | Format on first mount (`LittleFS.begin(true)`) |
| `common.h` TFT_eSPI includes polluting the build | High | Certain if copied verbatim | Create `common_lcd.h` without display imports |

---

## 10. Summary Checklist

### Phase A — Foundation
- [ ] Fix `boards/waveshare_esp32s3_n16r8.json` — flash 8 MB → 16 MB
- [ ] Add `partitions_16mb_ota.csv`
- [ ] Migrate SPIFFS → LittleFS in `splash_sequence.cpp` + `platformio.ini`
- [ ] Add `lib_extra_dirs = ../esp32common` + version script to `platformio.ini`
- [ ] Port `task_config.h`, `logging_config.h`, `helpers.*`
- [ ] Create `common_lcd.h` (no TFT/display)

### Phase B — FreeRTOS
- [ ] Create dedicated LVGL task (owns `lv_timer_handler()`)
- [ ] Port `DisplayUpdateQueue` adapted for `UI::Runtime::set_state()`
- [ ] Port `RuntimeTaskStartup` with PSRAM stacks
- [ ] Refactor `setup()` → `BootstrapPhaseRunner`

### Phase C — WiFi + Config
- [ ] Port `lib/receiver_config/receiver_config_manager.*`
- [ ] Port `config/wifi_setup.*`
- [ ] Add LittleFS + WiFi bootstrap phases

### Phase D — Webserver
- [ ] Copy `lib/webserver/` tree
- [ ] Add webserver bootstrap phase + build flags

### Phase E — ESP-NOW
- [ ] Port entire `src/espnow/` subtree
- [ ] Wire data flow to `DisplayUpdateQueue`
- [ ] Add `ConnectionStatusWidget` (replaces TFT circle indicator rendering)
- [ ] Remove `DemoModel` once real data verified

### Phase F — MQTT
- [ ] Port `src/mqtt/` subtree
- [ ] Add MQTT task to startup

### Phase G — OTA
- [ ] Enable `OtaBootGuard` + `SetupHealthGate` from esp32common

### Phase H — Display expansion
- [ ] Expand `UI::Runtime::set_state` to `TelemetrySnapshot`
- [ ] Add LVGL panels for extended fields
