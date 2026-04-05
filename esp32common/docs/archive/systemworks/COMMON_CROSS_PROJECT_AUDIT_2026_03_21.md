# Common Cross-Project Audit (Shared ESP-NOW/OTA Contracts + High-Use Function Rewrite Opportunities)

**Date:** 2026-03-21  
**Scope:** shared code and shared contracts affecting both transmitter and receiver  
**Projects considered:** transmitter (`ESPnowtransmitter2`), receiver (`espnowreceiver_2`), common (`esp32common`).

---

## 1) Why this matters

You have hard shared boundaries:
- ESP-NOW message schema (`espnow_common.h` + packet handlers)
- Heartbeat/ACK integrity checks
- OTA session/auth + upload contract
- Version/capability beacon semantics

Any change across these boundaries must be coordinated.

---

## 2) Shared findings (legacy/orphan/duplication)

## 2.1 Integrity logic is fragmented across projects

Checksum/CRC logic exists in multiple places:
- transmitter runtime modules (manual loops)
- receiver runtime modules (manual loops)
- shared common helpers (`espnow_packet_utils`, `espnow_transmitter`)

Result:
- duplicated logic,
- higher drift risk,
- harder validation for protocol compatibility.

## 2.2 Existing shared helper is under-leveraged

`esp32common/espnow_common_utils/espnow_packet_utils.h` already provides:
- `calculate_checksum(...)`
- `calculate_message_checksum(...)`
- `verify_message_checksum(...)`

But several transmitter/receiver modules still manually recompute checksums.

## 2.3 Legacy compatibility remains intentional in shared wire definitions

`esp32common/espnow_transmitter/espnow_common.h` retains deprecated subtype entries for wire compatibility. This is acceptable, but requires explicit migration planning (capabilities/versioning).

---

## 3) High-use/shared-function rewrite opportunities

## 3.1 Create one canonical integrity layer in common

Add a dedicated shared API (example):
- `esp32common/espnow_common_utils/espnow_integrity.h`

With canonical helpers:
1. `checksum16_sum(...)` for payload sums
2. `checksum16_xor_message<T>(...)` for message structs
3. `crc16_ccitt(...)` (table-driven, fixed polynomial/init)
4. `crc32_blob(...)` (single implementation wrapper)

Then route transmitter + receiver callsites to this layer.

## 3.2 Replace manual loops with shared helpers

Manual checksum loops currently appear in both projects (examples):
- transmitter component config/catalog handlers
- transmitter settings update verification
- receiver component-apply send/ack verify paths

Rewrite target:
- no manual byte-sum loops in application modules; use shared helper only.

## 3.3 CRC32 standardization for persisted settings blobs

Current behavior should stay exactly compatible, but implementation should be centralized.  
Use one tested implementation and remove duplicate bit-by-bit functions.

## 3.4 CRC16 vs CRC32 — elimination feasibility analysis

### What was found

The audit identified **three distinct checksum/CRC algorithms** in use across the projects. They must not be confused:

| Label | Algorithm | Polynomial | Where | Changeable? |
|---|---|---|---|---|
| CRC16-CCITT | 16-bit CCITT | `0x1021`, init `0xFFFF` | `espnow_transmitter.cpp` — heartbeat wire packets | **Yes (coordinated)** |
| CRC16-Modbus | 16-bit Modbus RTU | `0xA001` (reversed `0x8005`), init `0xFFFF` | `SUNGROW-CAN.cpp` — `modbus_crc()` only | **No — external hardware protocol** |
| CRC32-IEEE | 32-bit IEEE 802.3 | `0xEDB88320`, init `0xFFFFFFFF` | `settings_manager.cpp` — NVS blob integrity | **Yes (local storage only)** |

The simple 16-bit byte-sum (`calculate_checksum`, `checksum16`) labelled "checksum" in various ESP-NOW packet types is **not** CRC16 — it is a plain additive sum and a separate concern.

### The Modbus CRC16 is not our CRC16 and cannot be removed

`SUNGROW-CAN.cpp` contains a local `modbus_crc()` function that implements Modbus RTU CRC16.  
This is **not** the same algorithm as CRC16-CCITT (different polynomial, different domain) and **shares no code** with the heartbeat CRC16.  
It is a hardware protocol compliance requirement for communication with the Sungrow inverter.  
It cannot be removed or replaced regardless of any ESP-NOW changes.

### Can CRC16-CCITT be eliminated in favour of CRC32?

**YES — this is the recommended path.**

The only use of CRC16-CCITT in our code is in the ESP-NOW heartbeat/heartbeat-ack wire packets:
- `heartbeat_t.checksum` (`uint16_t`) in `espnow_common.h`
- `heartbeat_ack_t.checksum` (`uint16_t`) in `espnow_common.h`
- `calculate_crc16()` / `validate_crc16()` in `espnow_transmitter.cpp`
- Call sites: `heartbeat_manager.cpp` (transmitter), `rx_heartbeat_manager.cpp` (receiver)

Replacing it with CRC32 requires:
1. Change `uint16_t checksum` → `uint32_t checksum` in both packed structs (adds 2 bytes each).
2. Remove `calculate_crc16()` and `validate_crc16()` from `espnow_transmitter.cpp`.
3. Update `heartbeat_manager.cpp` and `rx_heartbeat_manager.cpp` to use `esp_crc32_le()` (or the canonical shared helper).
4. Coordinate transmitter and receiver in the same deployment window.

Consequences and benefits:

| Aspect | CRC16-CCITT (current) | CRC32 (proposed) |
|---|---|---|
| ESP32 hardware support | No — software bit-at-a-time loop | **Yes** — `esp_rom_crc32_le()` in ROM, zero flash cost |
| Error detection probability | 1 in 65,536 false positive | 1 in 4,294,967,296 false positive |
| Number of algorithms to maintain | 2 (CRC16 + CRC32) | **1** (CRC32 only) |
| Wire breaking change required | N/A | Yes — coordinated TX + RX |
| On-wire size delta | N/A | +2 bytes per heartbeat / +2 bytes per ack |

### Can CRC32 be replaced with CRC16 instead?

**No — this would be a downgrade.**  
- CRC16 has no hardware support on ESP32; CRC32 does.  
- CRC16 provides weaker protection for NVS blobs.  
- Reformatting all 5 persisted blob types would require an NVS migration.  
- There is no benefit.

### Conclusion: after migration, the CRC landscape simplifies to

1. **CRC32** — all project-owned integrity (NVS blobs + heartbeat), using `esp_rom_crc32_le()`.  
2. **Modbus CRC16** — isolated in `SUNGROW-CAN.cpp`, separate protocol adapter, no shared code, untouched.

## 3.5 Second-pass addendum: non-CRC shared high-use functions

### A) `EspnowMessageRouter::route_message(...)` (`espnow_common_utils/espnow_message_router.cpp`)

Finding:
- Current implementation is a linear scan over all registered routes for every incoming message (`O(n)` dispatch).
- This function is in the receiver/transmitter hot path because it is called once per dequeued ESP-NOW message.

Improvement (shared, behavior-preserving):
1. Keep existing public API (`register_route`, `route_message`).
2. Internally maintain per-type buckets to reduce scan set size.
3. For `msg_packet`, maintain subtype buckets for direct hit instead of scanning all packet routes.

Expected outcome:
- Lower dispatch latency jitter under burst traffic.
- Cleaner scaling when additional route types are added.

### B) Packet logging utilities (`EspnowPacketUtils::print_packet_info(...)`)

Finding:
- Utility prints via `Serial.printf` directly from common helper path.
- In high message-rate scenarios, verbose packet logging can dominate CPU time and serial I/O.

Improvement:
1. Keep helper but guard usage with compile-time/runtime log-level policy.
2. Route through project logger abstraction where possible to respect throttling and backend settings.

### C) Cross-project repeated singleton lookups in hot loops

Finding:
- Both receiver and transmitter frequently call singleton accessors inside inner loops.

Improvement:
1. Standardize a per-loop local-reference pattern for readability and minor overhead reduction.
2. Add this to coding guideline for high-frequency tasks.

### Shared implementation priority

P0:
1. Router internal pre-index optimization (no API change).
2. Log-level guard policy for packet-info helper usage.

P1:
3. Apply local-reference loop pattern in both projects' hottest tasks.

---

## 4) Independent vs parallel execution map

## Can be done independently (safe, no cross-device protocol change)

1. Add new shared helper APIs in common **without changing current callers**.
2. Add unit tests/golden vectors for checksum/CRC helper outputs.
3. Internal refactors in a single project that preserve emitted/accepted wire format.

## Must be done in parallel (same release window or compatibility-gated rollout)

1. **CRC16-CCITT → CRC32 migration for heartbeat wire packets** — `heartbeat_t` / `heartbeat_ack_t` checksum field changes from `uint16_t` to `uint32_t`; transmitter and receiver must be deployed together.
2. **Any other on-wire checksum algorithm change** (sum → CRC etc.).
3. **Any other packet struct layout change** in `espnow_common.h`.
4. **Version-beacon field semantics changes**.
5. **OTA handshake/header field contract changes** between receiver and transmitter.

---

## 5) Recommended rollout strategy for shared changes

## Stage A (independent prep — no wire change)
1. Add canonical common integrity helpers (`espnow_integrity.h`) including a `crc32_packet()` wrapper around `esp_rom_crc32_le()`.
2. Add test vectors for current CRC16-CCITT and CRC32 outputs (golden values).
3. Keep old CRC16 path alive and equivalent — no wire change yet.
4. Remove dead `SettingsManager::calculate_crc32()` (transmitter-only, safe now).

## Stage B (parallel migration — coordinated wire change)
5. Update `espnow_common.h`: change `heartbeat_t.checksum` and `heartbeat_ack_t.checksum` from `uint16_t` to `uint32_t`.
6. Update `heartbeat_manager.cpp` (transmitter) and `rx_heartbeat_manager.cpp` (receiver) to use `crc32_packet()` helper.
7. Remove `calculate_crc16()` and `validate_crc16()` from `espnow_transmitter.cpp`.
8. Deploy transmitter and receiver in the same window.
9. Verify with integration tests (heartbeat exchange + ACK round-trip).

## Stage C (cleanup)
10. Remove remaining manual checksum loops from application modules.
11. Remove now-orphaned CRC16 declarations from `espnow_transmitter.h`.
12. Leave `modbus_crc()` in `SUNGROW-CAN.cpp` untouched — it is not our CRC.

---

## 6) Immediate practical recommendations

1. Do **not** change wire-level algorithms first.  
2. First centralize implementation in common and prove output equivalence.  
3. Only then consider any protocol-level upgrades, and only with coordinated transmitter+receiver rollout.

---

## 7) Conclusion

The biggest cross-project risk is checksum/CRC drift and contract drift across shared ESP-NOW/OTA paths. This audit has now established that **CRC16-CCITT can and should be eliminated** in favour of CRC32 — giving a single algorithm for all project-owned integrity checks, with ESP32 hardware acceleration at no flash cost.

The Sungrow Modbus CRC16 is a completely separate concern (different algorithm, isolated to one file, external hardware requirement) and is unaffected by this consolidation.

Consolidating integrity logic into common utilities is the highest-value change and can start independently in Stage A. The CRC16 → CRC32 heartbeat migration is the only step that requires a coordinated parallel rollout across transmitter and receiver.

## 8) Implementation progress update (2026-03-22, Wave 1)

Completed independent items:
1. Stage A item #4 completed:
	- Removed dead `SettingsManager::calculate_crc32(...)` from transmitter settings manager.
2. Independent orphan cleanup completed in both projects:
	- Transmitter removed: `component_config_sender.*`, `test_mode.*`
	- Receiver removed: legacy state/time/test modules (`receiver_state_manager.*`, `connection_state_manager.*`, `time_sync_manager.*`, `test_data.*`)
3. Build validation completed after cleanup:
	- Transmitter `olimex_esp32_poe2` ✅
	- Receiver `receiver_tft` ✅
4. Receiver build-path cleanup completed:
	- `receiver_tft` source filter now excludes LVGL page/widget files, removing mixed-backend compile bloat.
5. Receiver hot-path dedup completed:
	- Shared telemetry post-processing helper extracted in `espnow_tasks.cpp` and reused by DATA + EVENTS handlers.
6. Receiver connection initialization/request send-path simplification completed:
	- Added local send wrappers in `rx_connection_handler.cpp` and replaced repeated init-send boilerplate with a section-loop implementation.
	- Reused wrapper-based send path for REQUEST_DATA retry transmission.
7. Receiver catalog retry engine simplification completed:
	- Replaced repeated retry condition/send blocks in `rx_connection_handler.cpp` with a descriptor-table loop while preserving retry counters and limits.
8. Stage A shared CRC32 consolidation completed:
	- Added canonical common `crc32_packet()` plus trailing-field helpers in `esp32common/espnow_common_utils/espnow_packet_utils.h`.
	- Migrated transmitter settings blob CRC32 generation/verification to the shared helper.
	- Added CRC32 golden-vector coverage in `esp32common/tests/crc32_packet_utils_test.cpp`.
9. Remaining manual checksum loops removed:
	- Transmitter `src/espnow/component_catalog_handlers.cpp`: removed local `checksum16()`, replaced with `EspnowPacketUtils::calculate_checksum()`.
	- Receiver `src/espnow/battery_handlers.cpp`: replaced manual byte-sum loop in `validate_checksum()` with shared helper.
	- Receiver `src/espnow/component_config_handler.cpp`: replaced manual byte-sum loop in `ComponentConfigHandler::validate_checksum()` with shared helper.
10. Coordinated Stage B heartbeat migration completed (TX + RX):
	- Updated `espnow_common.h` heartbeat structs from `uint16_t checksum` to `uint32_t checksum`.
	- Migrated transmitter `heartbeat_manager.cpp` and receiver `rx_heartbeat_manager.cpp` to shared CRC32 helpers in `EspnowPacketUtils`.
	- Removed legacy `calculate_crc16()` / `validate_crc16()` from shared `espnow_transmitter.cpp/.h`.
	- Verified coordinated builds: transmitter `olimex_esp32_poe2` ✅, receiver `receiver_tft` ✅.
11. Stage B addendum completed: shared router dispatch pre-index optimization (no API change):
	- Updated `espnow_common_utils/espnow_message_router.cpp` internals to pre-index routes by message type, with packet subtype buckets plus packet-wildcard fallback.
	- Preserved public router API (`register_route`, `route_message`, `clear_routes`) and route registration order semantics.
	- Verified cross-project compatibility builds: transmitter `olimex_esp32_poe2` ✅, receiver `receiver_tft` ✅.
12. P0 shared packet-info log-guard completed:
	- `EspnowPacketUtils::print_packet_info(...)` in `espnow_common_utils/espnow_packet_utils.h` migrated from unconditional `Serial.printf` to `ESP_LOGD` (zero-cost no-op when `CORE_DEBUG_LEVEL < 4`; routes through ESP-IDF structured log infrastructure in debug builds).
	- Replaced the three hot-path `print_packet_info` callsites in `espnowreceiver_2/src/espnow/espnow_tasks.cpp` (`handle_packet_events`, `handle_packet_logs`, `handle_packet_cell_info`) with project-native `LOG_DEBUG(kLogTag, ...)` — integrating into the project's runtime log-level control and MQTT log backend.
	- Receiver `receiver_tft` build verified ✅.
13. P1 shared hot-loop local-reference cleanup completed:
	- Receiver `task_espnow_worker(...)` now caches `RxStateMachine`, `ReceiverConnectionHandler`, and `EspNowConnectionManager` references once at task scope, replacing repeated singleton lookups inside the per-message loop.
	- Transmitter `DataSender::task_impl(...)` now caches `TxStateMachine` at task scope and reuses a per-iteration `test_mode_enabled` flag; `DataSender::send_battery_data()` now caches `EnhancedCache` before the transient-cache write path.
	- Cross-project builds revalidated: transmitter `olimex_esp32_poe2` ✅, receiver `receiver_tft` ✅.
14. Receiver OTA upload-path decomposition continued:
	- In `espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp`, extracted challenge/session negotiation into focused helpers (`store_ota_challenge`, `try_get_prearmed_ota_challenge`, `try_arm_ota_challenge`, `acquire_ota_session_challenge`) and removed duplicated inline `/api/ota_status` + `/api/ota_arm` flow from `api_ota_upload_handler(...)`.
	- Preserved fallback/error behavior and auth-header population semantics for transmitter OTA streaming.
	- Receiver build revalidated: `receiver_tft` ✅.
15. Receiver OTA upload-path decomposition continued:
	- In `espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp`, extracted the OTA stream-forward loop into `forward_ota_stream_to_transmitter(...)` with explicit typed result reporting (`OtaForwardError`, `OtaForwardResult`).
	- Preserved chunk-forward stall detection, upload receive failure behavior, and early transmitter HTTP rejection parsing while reducing `api_ota_upload_handler(...)` inlined control-flow complexity.
	- Receiver build revalidated: `receiver_tft` ✅.
16. Receiver OTA upload-path decomposition completed:
	- Extracted TCP connection setup + HTTP request header write into `open_ota_transmitter_connection(...)` and 70-second response-wait loop + response parsing into `await_and_parse_ota_response(...)` (returning `OtaResponseResult` with typed `State` enum).
	- `api_ota_upload_handler(...)` now consists entirely of named helper calls — all inline TCP, header-write, and response-wait logic removed.
	- Receiver build revalidated: `receiver_tft` ✅.
17. Receiver `espnow_tasks.cpp` split into focused compilation units:
	- `espnow_tasks_internal.h` — internal shared header for the module family.
	- `espnow_message_handlers.cpp` — data, packet, LED, and debug handler implementations.
	- `espnow_settings_sync.cpp` — Phase 2 settings sync handlers and private `request_category_refresh(...)` helper.
	- `espnow_tasks.cpp` reduced from 1020 lines to 736 lines; retains route setup, worker loop, and utility helpers.
	- Receiver build revalidated: `receiver_tft` ✅ (identical binary footprint).

## 9) Runtime sizing reference (MQTT / queue / HTTP-OTA)

Receiver runtime values currently in code (for quick troubleshooting reference):

- MQTT task stack: `TaskConfig::MQTT_CLIENT_STACK = 10240` bytes (`src/config/task_config.h`).
- ESP-NOW RX queue depth: `ESPNow::QUEUE_SIZE = 10` messages (`src/common.h`, created in `src/main.cpp`).
- MQTT socket payload buffer: `mqtt_client_.setBufferSize(6144)` (`src/mqtt/mqtt_client.cpp`).
- MQTT reconnect throttle: `RECONNECT_INTERVAL_MS = 5000` (`src/mqtt/mqtt_client.h`).
- MQTT loop cadence: task delay `100 ms` (`src/mqtt/mqtt_task.cpp`).

HTTP/OTA streaming values (receiver → transmitter path in `lib/webserver/api/api_control_handlers.cpp`):

- Upload chunk buffer: `char buf[1024]`.
- OTA status poll timeout: `3000 ms` (`/api/ota_status`).
- OTA arm/status challenge timeout: `5000 ms`.
- Raw TCP OTA socket timeout: `60000 ms` (`tx_client.setTimeout(60000)`).
- Stream stall fail window during write retries: `60000 ms`.
- Post-stream response wait window: `70000 ms`.
- Early-response body read window: `1200 ms` (body capped to 512 bytes).
- Final-response body read window: `3000 ms` (body capped to 512 bytes).

Recent stack-safety note:
- MQTT catalog parse scratch buffers were moved off the MQTT callback stack to reusable static storage in `src/mqtt/mqtt_client.cpp` after an observed stack canary trip in `MqttClient`.
- MQTT task stack allocation was increased to provide additional fixed headroom for current payload and logging paths.

## 10) Step 18: Transmitter File Splits Completion

**Status:** ✅ **COMPLETE**

**Execution date:** 2026-03-21  
**Impact on shared code:** None (transmitter internal reorganization only)

### Summary

Transmitter module decomposition completed for two large files:

1. **`ota_manager.cpp`:** 1,639 → 436 lines (73% reduction)
	- Extracted: `ota_manager_internal.h` (helper declarations)
	- Extracted: `ota_http_utils.cpp` (free HTTP helpers, rate-limiting, PSK)
	- Extracted: `ota_upload_handler.cpp` (upload handler)
	- Extracted: `ota_status_handlers.cpp` (10 read-only endpoints)
	- Retained: Session management, auth validation, server lifecycle

2. **`settings_manager.cpp`:** 1,241 → 238 lines (81% reduction)
	- Extracted: `settings_persistence.cpp` (NVS save/load + blob structs)
	- Extracted: `settings_field_setters.cpp` (per-field dispatcher functions)
	- Extracted: `settings_espnow.cpp` (ESP-NOW settings sync)
	- Retained: Validation, initialization, version management

### Verification

- Transmitter build: `pio run -e olimex_esp32_poe2` ✅ **SUCCESS**
- No public API changes (internal reorganization only)
- All 7 new files compile + link without modification
- Shared contracts (ESP-NOW, OTA, settings) unchanged
- Post-split stabilization verification (2026-03-23): transmitter compile restored after two cleanup fixes and revalidated ✅
	- `esp32common/ethernet_config.h`: added `<ETH.h>` include for ETH PHY macro availability.
	- `ESPnowtransmitter2/src/settings/settings_manager.cpp`: removed stray unmatched brace from split artifact.
	- `pio run -j 12` (transmitter) ✅ SUCCESS.

### Files Summary

**New transmitter files (7 TUs + 1 header):**
- `ota_manager_internal.h` (90 lines)
- `ota_http_utils.cpp` (530 lines)
- `ota_upload_handler.cpp` (400 lines)
- `ota_status_handlers.cpp` (530 lines)
- `settings_persistence.cpp` (500 lines)
- `settings_field_setters.cpp` (350 lines)
- `settings_espnow.cpp` (180 lines)

**Trimmed transmitter files (2 TUs):**
- `ota_manager.cpp` (1,639 → 436 lines)
- `settings_manager.cpp` (1,241 → 238 lines)

**Total reduction in primary files:** 2,880 → 674 lines (77% reduction)  
**Total new code density:** 2,080 lines distributed across 7 focused translation units

### Impact on Shared Code

- ✅ No changes to `esp32common/` modules
- ✅ No changes to ESP-NOW contracts
- ✅ No changes to OTA wire format or session semantics
- ✅ No changes to settings persistence format
- ✅ Receiver builds independently and is unaffected

### P1 Status (Cross-Project)

All P1 items now complete:
- ✅ Steps 1–17: Receiver, common, and transmitter optimizations
- ✅ Step 18: Transmitter file splits (OTA + settings)
- 📋 P2 items: Web page consolidation, dead helper cleanup (not urgent)
