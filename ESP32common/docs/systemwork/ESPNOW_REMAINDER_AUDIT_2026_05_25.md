# ESP-NOW Remainder Audit (2026-05-25)

## Scope
Full workspace audit across these active project roots:

- `esp32common`
- `ESPnowtransmitter2/espnowtransmitter2`
- `espnowreceiver_2`
- `espnowreceiver_LCD`

## Audit Method
I performed exhaustive string and symbol scans for ESP-NOW indicators across code/config/scripts/docs/build outputs, including:

- Tokens: `espnow`, `ESP-NOW`, `ESPNOW`, `esp_now`, `EspNow`
- API symbols: `esp_now_init`, `esp_now_send`, `esp_now_register_*`, peer functions
- Legacy architecture tokens: `EspnowTxScheduler`, `RxRadioArbiterFsm`, `radio_pressure`
- Filename sweep for `*espnow*` source files

I then manually reviewed all non-doc source/config files with matches to classify them as:

1. **Active runtime risk**
2. **Legacy compatibility/phantom code**
3. **Naming/comment/documentation residue**
4. **Build artifact residue**

---

## Executive Summary

### What is good
1. **No direct ESP-NOW runtime API calls remain in active source.**
   - No `esp_now_*` callsites found in active `*.c/*.cpp/*.h/*.hpp` source trees.
2. **Receiver runtime trees no longer contain active `src/espnow/*` source files.**
3. **MQTT-only architecture is largely implemented in receiver and transmitter runtime paths.**

### What still remains
There is still meaningful ESP-NOW residue in three areas:

1. **One active compatibility implementation file is still compiled:**
   - `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp`
2. **Many legacy names/comments/constants remain in active code (not just docs).**
3. **Stale build artifacts still contain ESP-NOW objects/symbols (`.pio*` and `firmware.map`).**

---

## Findings by Category

## A) Active code with ESP-NOW compatibility shims (high-priority tidy-up)

### 1) Legacy settings transport shim still present and built
- File: `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp`
- Status: **active source file**, currently provides MQTT-path behavior plus no-op/suppressed ESP-NOW functions.
- Risk: keeps dead transport glue in runtime build and confuses future maintenance.

Recommended action:
- Rename and refactor to transport-neutral naming (e.g., `settings_transport_bridge.cpp`) **or** fold logic into existing MQTT/settings services and delete this file.

### 2) ESP-NOW update-rate setting still active in transmitter settings model
- Files:
  - `ESPnowtransmitter2/espnowtransmitter2/src/system_settings.h`
  - `ESPnowtransmitter2/espnowtransmitter2/src/system_settings.cpp`
- Examples:
  - `DEFAULT_ESPNOW_UPDATE_RATE_MS`
  - `set_espnow_update_rate_ms()`
  - `espnow_update_rate_ms_`
- Risk: dead semantic config in a MQTT-only system.

Recommended action:
- Replace with transport-neutral or MQTT-relevant cadence naming (e.g., `snapshot_publish_rate_ms`) and migrate NVS key safely.

---

## B) Active code with legacy ESP-NOW naming/comments (medium-priority)

### 1) Receiver and transmitter comments/text still describe ESP-NOW behavior
Representative files:
- `espnowreceiver_2/src/main.cpp`
- `espnowreceiver_2/src/state_machine.cpp`
- `espnowreceiver_2/lib/webserver/pages/*` (monitor/debug wording)
- `espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/network/time_manager.*`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_manager.*`
- `ESPnowtransmitter2/espnowtransmitter2/src/config/task_config.h`

Recommended action:
- Run a wording cleanup pass to remove outdated transport descriptions.

### 2) MQTT IDs/topic prefixes still use espnow naming
Representative files:
- `espnowreceiver_2/src/mqtt/mqtt_client.cpp` (`"espnow_receiver"`)
- `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp` (`"espnow_receiver"`)
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp` (`"espnow_transmitter"` default)
- `ESPnowtransmitter2/espnowtransmitter2/src/config/network_config.h` (`espnow/transmitter/...` topic prefixes)

Recommended action:
- Keep if backward-compatible topic naming is required.
- Otherwise introduce versioned transport-neutral namespace and migrate consumers deliberately.

---

## C) Shared contract/config residue (medium-priority)

Files:
- `esp32common/include/esp32common/contracts/shared_contracts.h`
- `esp32common/include/esp32common/config/timing_config.h`
- `esp32common/include/esp32common/mqtt/mqtt_feature_flags.h`
- `esp32common/firmware_version.h`

Current state:
- Mostly transport-neutral functionality, but many field/type names still embed `espnow` terminology.

Recommended action:
- Preserve binary/message compatibility first.
- Add aliasing + deprecation plan to migrate `espnow_*` names to neutral names without breaking payload layout.

---

## D) Receiver web API middleware references (low risk, cosmetic)

Files:
- `espnowreceiver_2/lib/webserver/api/api_middleware.cpp`
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_middleware.cpp`

Current state:
- These contain explanatory comments about removed ESP-NOW pressure headers.
- Runtime behavior is already MQTT-only.

Recommended action:
- Optional: keep comments as migration rationale, or reduce to concise neutral wording.

---

## E) Test/dev/tooling residue (low to medium)

### 1) Test helper still uses legacy payload type name
- `espnowreceiver_2/test/test_helpers/test_helpers.cpp` (`espnow_payload_t`)

### 2) VS Code include path residue
- `espnowreceiver_2/.vscode/c_cpp_properties.json` references legacy include path(s).

### 3) Script/path wording residue
- `esp32common/scripts/hardening_guardrails.py`
- `esp32common/scripts/version_firmware.py`
- `esp32common/scripts/generate_type_mappings.py`

Recommended action:
- Clean naming and stale include paths; keep functional behavior unchanged.

---

## F) Build artifact residue (important operational note)

A large number of ESP-NOW hits are from generated build artifacts, especially:

- `.pio/` and `.pio_build_tmp/`
- `.pio_localbuild/`
- `firmware.map`

These can make audits look like ESP-NOW runtime is still present even when source has been removed or disabled.

Recommended action:
1. Perform full clean in all project variants before final verification.
2. Rebuild once.
3. Re-run grep audit excluding docs and including only source/config.
4. Optionally add guard script to fail CI if `esp_now_*` APIs appear in active source.

---

## Prioritized Cleanup Plan

## Priority 1 (real residue)
1. ✅ **COMPLETE (2026-05-26)** Remove/refactor `settings_espnow.cpp` compatibility file.
  - Runtime implementation moved to transport-neutral `settings_apply.cpp`.
  - Legacy no-op ESP-NOW shim code removed.
2. ✅ **COMPLETE (2026-05-26)** Remove/rename transmitter `espnow_update_rate_ms` settings path.
  - Renamed to transport-neutral snapshot publish naming:
    - `DEFAULT_SNAPSHOT_PUBLISH_RATE_MS`
    - `set_snapshot_publish_rate_ms()` / `get_snapshot_publish_rate_ms()`
    - `snapshot_publish_rate_ms_`
  - NVS key retained as `"upd_rate"` via `NVS_SNAPSHOT_RATE_KEY` for safe migration continuity.
3. ✅ **COMPLETE (2026-05-26)** Clean `.pio*` artifacts and re-verify build.
  - Executed clean + rebuild for transmitter target (`olimex_esp32_poe2`) successfully.
  - Rebuild confirms new settings code path (`settings_apply.cpp`) compiles and links cleanly.

## Priority 2 (naming + clarity)
4. ✅ **COMPLETE (2026-05-26)** Rename MQTT IDs/topic prefixes where safe.
  - Receiver defaults updated to transport-neutral IDs:
    - `battery_emulator_receiver`
    - `battery_emulator_transmitter` (fallback for receiver API update payloads)
  - Transmitter defaults/topics updated in active config/runtime paths:
    - `battery_emulator_transmitter`
    - `batt-emu/mqtt-v1/...` topic defaults for data/status/OTA command route
  - MQTT logger root updated to `batt-emu/mqtt-v1/tx`.
5. ✅ **COMPLETE (2026-05-26)** Update task/config constant names containing `ESPNOW_*` to transport-neutral names.
  - In transmitter task config:
    - `STACK_SIZE_ESPNOW_RX` → `STACK_SIZE_RX_INGRESS`
    - `PRIORITY_ESPNOW` → `PRIORITY_RX_INGRESS`
    - `ESPNOW_QUEUE_SIZE`/`ESPNOW_MESSAGE_QUEUE_SIZE` → `RX_MESSAGE_QUEUE_SIZE`
  - Removed redundant ESPNOW alias constant.
6. ✅ **COMPLETE (2026-05-26)** Cleanup stale comments in receiver/transmitter/webserver code.
  - Removed/updated stale ESP-NOW wording in:
    - transmitter `time_manager.*`, OTA root banner, test-data mode comments
    - receiver boot/state comments and debug page transport text
    - receiver/LCD webserver SSE and scheduling comments

## Priority 3 (hygiene)
7. ✅ **COMPLETE (2026-05-26)** Cleanup active-source naming/comment/dead-code residue.
  - Deleted `settings_espnow.cpp` deprecated placeholder (was 4-line comment stub).
  - Removed stale `friend class EspNowTransmitter` from `ethernet_manager.h` (class never existed in active code).
  - Removed permanently-disabled `MQTT_FEATURE_ESPNOW_COEXISTENCE_GATES 0` block from `mqtt_feature_flags.h`.
  - `ETH.setHostname("espnow-transmitter")` → `"battery-emulator-tx"` in `ethernet_manager.cpp`.
  - JSON test-data transport value `"ESP_NOW"` → `"MQTT"` in `test_data_config.cpp`.
  - Renamed `timing_config.h` (shared library) identifiers:
    - `HeartbeatTiming::espnow_connecting_timeout_ms` → `connecting_timeout_ms`
    - `ESPNOW_CONNECTING_TIMEOUT_MS` → `CONNECTING_TIMEOUT_MS`
    - `ReceiverEspnowTiming` → `ReceiverRxTiming`; `RECEIVER_ESPNOW` → `RECEIVER_RX`
    - `RX_ESPNOW_*` → `RX_INGRESS_*`
    - `DataTransmissionTiming::espnow_send_interval_ms` → `data_send_interval_ms`
    - `ESPNOW_SEND_INTERVAL_MS` → `DATA_SEND_INTERVAL_MS`
    - Section header "RECEIVER ESP-NOW TASKS" → "RECEIVER RX TASKS"
  - Updated all consumer references for renamed timing fields (receiver `main.cpp`).
  - Cleaned migration-history comments in `shared_contracts.h`, `version_utils.h`, `ota_manager.h`, `static_data.h`, `runtime_context.h`, `main.cpp` (transmitter), `api_middleware.cpp` (both receivers).
  - Fixed typo `espnowreciever_2` → `espnowreceiver_2` in `generate_type_mappings.py` help/example string.
  - Updated remaining receiver web UI titles/headings from `ESP-NOW Receiver` to `Battery Emulator Receiver` (both `espnowreceiver_2` and `espnowreceiver_LCD`).
  - Updated remaining active web UI/status strings and comments that still mentioned ESP-NOW data paths (monitor mode labels, OTA metadata comments, page subtype comments, hardware-config comments, transmitter identity comments, platformio descriptor comments).
  - All three projects verified: **BUILD SUCCESS** (transmitter 85s, espnowreceiver_2 65s, espnowreceiver_LCD confirmed).
8. Leave historical docs in place — these are architecture decision records and remain useful as-is.

---

- **Direct ESP-NOW API usage in active runtime source:** **Not found**.
- **Priority 1 + Priority 2 + Priority 3 runtime residue cleanup:** **Complete** (2026-05-26).
- **Remaining ESP-NOW residue:** only historical/archive docs and build artifacts. No active runtime residue.

The codebase is now fully transport-neutral across all active source, config, and shared library files.
User-visible web page titles and labels have been updated to Battery Emulator naming in active receiver web UI sources.