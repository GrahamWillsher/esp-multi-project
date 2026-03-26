# ESP-NOW Receiver: Project Architecture Master Document

**Scope**: Receiver-side runtime architecture for ESP-NOW data ingestion, UI display, web API/control, and configuration orchestration.  
**Version**: 1.1 (Current Workspace Baseline)  
**Date**: March 20, 2026  
**Device**: LilyGo T-Display-S3 (Receiver)  
**Status**: Active development baseline (build passing in current workspace)

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Architecture Overview](#architecture-overview)
3. [Core Systems](#core-systems)
4. [Current Codebase Snapshot (Mar 2026)](#current-codebase-snapshot-mar-2026)
5. [Technical References](#technical-references)
6. [Hardware, HAL, and Pin Layout](#hardware-hal-and-pin-layout)
7. [Timing, NVS, Metadata, and Memory](#timing-nvs-metadata-and-memory)
8. [Build, Flash, and Validation](#build-flash-and-validation)
9. [Cross-Project References](#cross-project-references)

---

## Project Overview

### Mission

Provide a resilient receiver that:

- Receives and validates ESP-NOW telemetry/control packets from transmitter.
- Drives local display/UI state (TFT/LVGL paths and dashboard pages).
- Exposes web endpoints for monitoring, configuration, and component apply orchestration.
- Bridges data outward through MQTT subscription/client integration where required.

### Hardware

- **Receiver**: LilyGo T-Display-S3
  - ESP-NOW receiver path
  - TFT display/UI rendering
  - WiFi for local web interface and integrations

### Key Features

| Feature | Purpose | Current Status |
|---------|---------|----------------|
| ESP-NOW RX state machine | Discovery/connection health + packet handling | ✅ Active |
| Component apply orchestration | Batched type/interface update workflow | ✅ Active |
| Type catalog caching | Battery/inverter catalog synchronization | ✅ Active |
| Web UI and API modularization | Segmented pages + API handlers | ✅ Active |
| Reboot orchestration | Shared countdown/reboot UX path | ✅ Active |
| Event logs page | Receiver-side event view and API integration | ✅ Active |
| MQTT integration hooks | Receiver-side subscription/client services | ✅ Active |

---

## Architecture Overview

### High-Level Data Flow

```
[Transmitter] --ESP-NOW--> [Receiver ESP-NOW Layer]
                                |
                                +--> [State/Cache Layer]
                                |      - battery_data_store
                                |      - battery_settings_cache
                                |      - type_catalog_cache
                                |      - component_apply_tracker
                                |
                                +--> [Display Layer]
                                |      - src/display/*
                                |
                                +--> [Web API + Pages]
                                |      - lib/webserver/api/*
                                |      - lib/webserver/pages/*
                                |
                                +--> [MQTT / external integrations]
                                       - src/mqtt/*
```

### Layered View

- **ESP-NOW Layer**: Receive/send helpers, callback routing, connection manager, heartbeat manager, RX state machine.
- **State/Cache Layer**: Packet-derived state, settings cache, component-apply request/result tracking.
- **Presentation Layer**: Display widgets/pages + web page generation and APIs.
- **Integration Layer**: MQTT services, network config, system/time sync interactions.

---

## Core Systems

### 1) ESP-NOW Receive + Control Path

Primary implementation files:

- `src/espnow/rx_state_machine.*`
- `src/espnow/rx_connection_handler.*`
- `src/espnow/rx_heartbeat_manager.*`
- `src/espnow/espnow_callbacks.*`
- `src/espnow/espnow_tasks.*`
- `src/espnow/espnow_send.*`

Notes:

- Receiver keeps transmitter connection state and routes message types to handlers.
- Batched component apply is the active control path (`component_apply_request`).

### 2) Component Apply + Catalog Synchronization

Primary implementation files:

- `src/espnow/component_apply_tracker.*`
- `src/espnow/component_config_handler.*`
- `src/espnow/type_catalog_cache.*`
- `lib/webserver/api/api_type_selection_handlers.*`

Notes:

- Active API workflow: `/api/component_apply` + `/api/component_apply_status`.
- Legacy direct `set_*` type/interface routes were retired in current baseline.

### 3) Web Server and API Modularization

Primary implementation files:

- `lib/webserver/api/api_*_handlers.*`
- `lib/webserver/api/api_response_utils.*`
- `lib/webserver/pages/*.cpp`
- `lib/webserver/common/page_generator.*`

Notes:

- Shared response helpers unify JSON/error/no-cache behavior.
- Reboot UI follows shared reboot countdown orchestration path.

### 4) Display Runtime

Primary implementation files:

- `src/display/*`
- `src/hal/display/*`

Notes:

- Display update queue and widget architecture remain modularized.
- LVGL/TFT behavior and known findings are documented in receiver docs.

### 5) MQTT and External Data Integration

Primary implementation files:

- `src/mqtt/mqtt_client.*`
- `src/mqtt/mqtt_task.*`

Notes:

- MQTT subscription enhancements and data-source tagging are implemented in current codebase lineage.

### 6) Inter-device versioning and heartbeat contracts

Primary references:

- `../esp32common/firmware_version.h`
- `../esp32common/docs/ESP-NOW_Communication_Architecture.md`
- `../esp32common/docs/ESPNOW_HEARTBEAT.md`
- `src/espnow/rx_heartbeat_manager.cpp`
- `src/espnow/handlers/system_status_handler.cpp`

Notes:

- Receiver validates and consumes transmitter state/messages under the shared protocol contract.
- Heartbeat and connection health are coordinated by the receiver RX state machine plus heartbeat manager.

---

## Hardware, HAL, and Pin Layout

Primary files:

- `src/hal/hardware_config.h`
- `src/hal/tft_espi_user_setup.h`
- `src/display/tft_impl/tft_display.cpp`

Board summary:

- Board: LilyGo T-Display-S3
- Display: ST7789, 8-bit parallel interface
- PSRAM-capable ESP32-S3 configuration enabled in `platformio.ini` (`BOARD_HAS_PSRAM`)

---

## Timing, NVS, Metadata, and Memory

Timing/NTP:

- Receiver no longer owns a standalone SNTP manager; legacy `src/time/time_sync_manager.cpp` is obsolete by design.
- Operational timing now follows ESP-NOW/runtime workflows and shared timing constants where needed.

NVS:

- Receiver configuration persistence paths are implemented via:
  - `lib/receiver_config/receiver_config_manager.h`
  - `lib/webserver/utils/receiver_config_manager.cpp`
  - `lib/webserver/utils/transmitter_nvs_persistence.cpp`

Firmware metadata and `.bin` identity:

- Metadata structure embedded in `.rodata` via `../esp32common/firmware_metadata/firmware_metadata.h`
- Build-time metadata/version naming: `../esp32common/scripts/version_firmware.py`

Memory/PSRAM:

- Receiver environments define PSRAM-enabled build flags in `platformio.ini`.
- Display and web subsystems are structured to avoid large transient allocations in hot paths.

Project rules reference:

- `../esp32common/docs/project guidlines.md`

## Current Codebase Snapshot (Mar 2026)

Top-level runtime anchors:

- `src/main.cpp`
- `src/state_machine.*`
- `src/state/*`
- `src/espnow/*`
- `src/display/*`
- `src/hal/*`
- `lib/webserver/*`

High-value review and migration records:

- `RECEIVER_FULL_CODE_REVIEW_2026_03_16.md`
- `RECEIVER_COMMON_CODE_REVIEW_2026_03_17.md`
- `ARCHITECTURE_REDESIGN.md`
- `IMPLEMENTATION_SUMMARY.md`
- `MASTER_CHECKLIST.md`

### Runtime Feature Note (Mar 26, 2026): Webserver Local-Only Logging

To quickly validate observed HTTP UI latency and MQTT responsiveness regression, receiver
webserver logging was switched to **Local-only (Serial)** for webserver paths.

Implemented:

1. Receiver webserver override
  - File: `lib/webserver/logging.h`
  - Added pre-include override:
    - `#undef LOG_USE_MQTT` (if defined)
    - `#define LOG_USE_MQTT 0`
  - Effect: `lib/webserver/*` `LOG_*` calls stay local and do not route to MQTT logger.

2. Shared logging guard hardening
  - File: `../esp32common/logging_utilities/logging_config.h`
  - Updated include/define flow to honor externally pre-defined `LOG_USE_MQTT`.
  - Added conditional include for `mqtt_logger.h` when `LOG_USE_MQTT == 1`.
  - Effect: override works cleanly without macro redefinition warnings.

Validation:

- `pio run -e receiver_tft -j 12` succeeds after change.
- `LOG_USE_MQTT` redefinition warnings were eliminated.

Recommended next step (granularity):

- Keep hot request-path `INFO/DEBUG` local-only.
- Route selected `WARN/ERROR` to both sinks where operationally useful.
- Use `log_routed(LogSink::Local|Mqtt|Both, ...)` only for explicit per-call sink policy.

### Runtime Feature Note (Mar 26, 2026): Dashboard Transmitter Ethernet 2-State Indicator

Implemented a quick, operator-focused transmitter status indicator on the dashboard root page (`/`) using existing runtime Ethernet state data.

Behavior:

- `ethernet_connected=true` → **Connected** (Green text + Green status dot)
- `ethernet_connected=false` → **Disconnected** (Red text + Red status dot)

Implementation details:

1. Dashboard API payload extension
  - File: `lib/webserver/api/api_telemetry_handlers.cpp`
  - Added `transmitter["ethernet_connected"] = TransmitterManager::isEthernetConnected();`

2. Initial page render state source
  - File: `lib/webserver/pages/dashboard_page.cpp`
  - Root-page transmitter status now initializes from `TransmitterManager::isEthernetConnected()`.

3. Dynamic card LED/text update
  - File: `lib/webserver/pages/dashboard_page_content.cpp`
  - Added `id='txStatusDot'` to transmitter status LED element.
  - File: `lib/webserver/pages/dashboard_page_script.cpp`
  - Polling update path now uses `tx.ethernet_connected` and updates both status text and dot color.

Operational note:

- Receiver already receives Ethernet runtime state from transmitter version beacon path (`version_beacon_t.ethernet_connected`) and caches it in `TransmitterState`.
- The dashboard reflects latest cached beacon state.

---

## Technical References

### Receiver-Specific

- [ARCHITECTURE_REDESIGN.md](ARCHITECTURE_REDESIGN.md)
- [DISPLAY_ARCHITECTURE_SUMMARY.md](DISPLAY_ARCHITECTURE_SUMMARY.md)
- [EVENT_LOGS_ARCHITECTURE_DIAGRAMS.md](EVENT_LOGS_ARCHITECTURE_DIAGRAMS.md)
- [MQTT_SUBSCRIPTION_ENHANCEMENTS.md](MQTT_SUBSCRIPTION_ENHANCEMENTS.md)
- [START_HERE.md](START_HERE.md)
- [RECEIVER_FULL_CODE_REVIEW_2026_03_16.md](RECEIVER_FULL_CODE_REVIEW_2026_03_16.md)
- [RECEIVER_COMMON_CODE_REVIEW_2026_03_17.md](RECEIVER_COMMON_CODE_REVIEW_2026_03_17.md)

### Shared Protocol/Infrastructure (esp32common)

- [../esp32common/docs/ESP-NOW_Communication_Architecture.md](../esp32common/docs/ESP-NOW_Communication_Architecture.md)
- [../esp32common/docs/ESPNOW_HEARTBEAT.md](../esp32common/docs/ESPNOW_HEARTBEAT.md)
- [../esp32common/docs/MQTT_LOGGER_IMPLEMENTATION.md](../esp32common/docs/MQTT_LOGGER_IMPLEMENTATION.md)
- [../esp32common/docs/project guidlines.md](../esp32common/docs/project%20guidlines.md)
- [../esp32common/README.md](../esp32common/README.md)

---

## Build, Flash, and Validation

### Build

```bash
pio run -e receiver_tft
```

### Upload + Monitor

```bash
pio run -e receiver_tft -t upload -t monitor
```

### Baseline Status (Mar 17, 2026)

- Receiver build for `receiver_tft` succeeds in current workspace baseline.

---

## Cross-Project References

- Transmitter master architecture document:
  - [../ESPnowtransmitter2/espnowtransmitter2/PROJECT_ARCHITECTURE_MASTER.md](../ESPnowtransmitter2/espnowtransmitter2/PROJECT_ARCHITECTURE_MASTER.md)
- Shared library and protocol foundation:
  - [../esp32common/README.md](../esp32common/README.md)

---

**Status**: ✅ Receiver architecture master baseline created and aligned to current repository layout.
