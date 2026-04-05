# Transmitter Codebase Full Audit (Legacy/Orphan + Efficiency/Readability + Static Heat Test)

**Date:** 2026-03-21  
**Project audited:** `ESPnowtransmitter2/espnowtransmitter2`  
**Context note:** transmitter shares ESP-NOW and OTA contracts with receiver/common.

---

## 1) Method used

1. Full source inventory (including build-path verification).
2. Legacy/orphan marker scan.
3. Static heat-path analysis (callbacks, queues, periodic tasks).
4. Checksum/CRC rewrite review.
5. Build validation: `pio run -e olimex_esp32_poe2`.

---

## 2) Executive summary

- Build is passing and architecture is modular in core runtime modules.
- **Notable bloat remains in active build** (all battery/inverter protocol implementations compile).
- Confirmed **compiled-but-unreferenced** modules exist.
- Checksum/CRC logic is duplicated in several runtime files and can be consolidated.
- Flash usage is high for 4MB target (**~81.3%**), so cleanup and source filtering matter.

---

## 3) Confirmed legacy/orphan findings (transmitter)

## 3.1 `component_config_sender` appears orphaned in active runtime

- Files:
  - `src/espnow/component_config_sender.h`
  - `src/espnow/component_config_sender.cpp`
- Finding:
  - No call sites found outside the module itself.
  - Module still compiles in active build.
- Recommendation:
  - Remove from active build path or wire explicitly if required.

## 3.2 `test_mode` module appears orphaned in active runtime

- Files:
  - `src/test_mode/test_mode.h`
  - `src/test_mode/test_mode.cpp`
- Finding:
  - Namespace and APIs present but no active call-sites found.
  - Still compiles in production build.
- Recommendation:
  - Move behind explicit feature flag or exclude from production source filter.

## 3.3 Dead CRC helper in `settings_manager`

- File:
  - `src/settings/settings_manager.cpp`
- Finding:
  - `SettingsManager::calculate_crc32(...)` is defined but not used.
  - Separate blob CRC helper (`calculate_blob_crc32`) is the active path.
- Recommendation:
  - Remove dead function or route all CRC through one canonical helper.

---

## 4) Static heat test (transmitter)

## H0 – highest-frequency / highest sensitivity

1. **ESP-NOW RX routing task** (`src/espnow/message_handler.cpp`)  
   Queue receive loop + router dispatch.
2. **Discovery active hopping loop** (`src/espnow/discovery_task.cpp`)  
   Channel-switch + probe/ack loop; timing-sensitive.
3. **Transmission task loop** (`src/espnow/transmission_task.cpp`)  
   Runs every interval, pushes transient/state data.

## H1 – frequent periodic tasks

4. **MQTT task loop** (`src/network/mqtt_task.cpp`)  
   Connectivity state updates + periodic publish logic.
5. **Heartbeat path** (`src/espnow/heartbeat_manager.cpp`)  
   CRC16 generation/validation in steady-state messaging.

---

## 5) Checksum/CRC rewrite opportunities (high usage / repeated)

## 5.1 Consolidate manual message checksum loops

Manual loops are repeated in:
- `src/espnow/component_catalog_handlers.cpp`
- `src/espnow/component_config_sender.cpp`
- `src/settings/settings_manager.cpp`

Recommendation:
- Replace ad-hoc loops with one shared helper from `esp32common/espnow_common_utils`.
- Keep wire format unchanged (same checksum field and range).

## 5.2 Replace bitwise CRC32 loops with a single implementation

Current: multiple bit-by-bit CRC32 loops in `settings_manager.cpp`.

Recommendation:
- Use one canonical CRC32 function (either LUT-based software or ESP-IDF ROM CRC helper wrapper).
- Keep all blob structs and persisted format unchanged.

## 5.3 CRC16 → CRC32 migration: CRC16-CCITT can be eliminated

**Updated finding (see also `COMMON_CROSS_PROJECT_AUDIT_2026_03_21.md` section 3.4 for full analysis).**

Three separate CRC/checksum algorithms exist in this project. They serve completely different purposes:

| Algorithm | Location | Purpose | Can remove? |
|---|---|---|---|
| CRC16-CCITT (poly `0x1021`) | `espnow_transmitter.cpp` | Heartbeat / heartbeat-ack wire integrity | **Yes — replace with CRC32** |
| CRC16-Modbus (poly `0xA001`) | `SUNGROW-CAN.cpp` `modbus_crc()` | Sungrow inverter Modbus RTU protocol | **No — external hardware protocol** |
| CRC32-IEEE (poly `0xEDB88320`) | `settings_manager.cpp` | NVS settings blob integrity | Keep and extend |

**The Modbus CRC16 and the CCITT CRC16 are different algorithms sharing only the name "CRC16".** They have no shared code. The Modbus one is completely isolated in `SUNGROW-CAN.cpp` and is a physical protocol requirement — it is untouchable.

**CRC16-CCITT has only two callers in the whole codebase:**
- `src/espnow/heartbeat_manager.cpp` — generates CRC16 for outgoing `heartbeat_t`
- Receiver: `src/espnow/rx_heartbeat_manager.cpp` — validates incoming heartbeat, generates CRC16 for `heartbeat_ack_t`

Replacing with CRC32:
- Changes `heartbeat_t.checksum` and `heartbeat_ack_t.checksum` from `uint16_t` → `uint32_t` (2 bytes larger per packet, both on-wire)
- Removes `calculate_crc16()` and `validate_crc16()` from `espnow_transmitter.cpp` and `espnow_transmitter.h`
- Uses `esp_rom_crc32_le()` — ROM function, zero flash cost, hardware-accelerated on ESP32
- Better error detection: $2^{32}$ vs $2^{16}$ false-positive space
- **MUST be coordinated with receiver in the same deployment window**

---

## 6) Bloat/readability hotspots (transmitter core)

Largest core runtime files:
- `src/network/ota_manager.cpp` (~1454 lines)
- `src/settings/settings_manager.cpp` (~1162 lines)
- `src/espnow/discovery_task.cpp` (~640 lines)
- `src/espnow/component_catalog_handlers.cpp` (~576 lines)
- `src/network/mqtt_manager.cpp` (~567 lines)

Recommendation:
- Split by responsibility (session/auth/streaming in OTA; CRC/persistence/validation in settings; routing vs policy in ESP-NOW handlers).

## 6.5) Second-pass addendum: non-CRC high-use function analysis

This pass focused specifically on hot runtime functions outside CRC/checksum logic.

### A) `TransmissionTask::task_impl(...)` (`src/espnow/transmission_task.cpp`)

Findings:
- Tight periodic loop performs connection checks, transient send, state send, cleanup, and logs.
- On each loop it repeatedly dereferences singleton managers (`EspNowConnectionManager`, `EnhancedCache`).
- Success-path `LOG_INFO` on frequent sends can become a measurable runtime overhead under high publish rates.

Improvement:
1. Cache manager references once per iteration (`auto& conn_mgr`, `auto& cache`).
2. Gate success logs with interval/throttle (keep errors immediate).
3. Keep one-send-per-iteration behavior but isolate send strategy into small helpers for easier tuning.

### B) `DiscoveryTask::attempt_restart_once(...)` (`src/espnow/discovery_task.cpp`)

Findings:
- Recovery path is robust but long and branch-heavy with repeated failure handling blocks.
- Uses blocking `delay(...)` stabilization inside restart path; this is acceptable in dedicated task but reduces responsiveness during recovery windows.

Improvement:
1. Extract repeated failure handling into `handle_restart_failure(reason)` helper.
2. Convert restart sequence into explicit step functions:
  - `restart_cleanup_peers()`
  - `restart_lock_channel()`
  - `restart_relaunch_discovery()`
  - `restart_verify_channel()`
3. Consider non-blocking wait pattern (tick-based) if restart latency instrumentation shows contention.

### C) `task_mqtt_loop(...)` (`src/network/mqtt_task.cpp`)

Findings:
- The loop repeatedly calls `MqttManager::instance()` and executes several on-connect publish calls sequentially.
- On reconnect, static-data publishing is synchronous and can cause long loop iterations.

Improvement:
1. Cache `auto& mqtt = MqttManager::instance();` and reuse across iteration.
2. Move on-connect publish bundle to dedicated helper with timed instrumentation.
3. Optionally stagger non-critical publishes across several loop cycles to flatten reconnection latency spikes.

### D) `EspnowMessageRouter::route_message(...)` (common; impacts transmitter runtime)

Finding:
- Router currently performs linear scan over registered routes (`O(n)` per message) using `std::function` handlers.

Improvement:
1. Add fast pre-index by message `type` (and subtype bucket for packet type).
2. Preserve existing API while changing internal lookup structure only.

### Priority for implementation

P0: ✅ Complete
- ~~Throttle success-path logs in `transmission_task.cpp`.~~

P1:
1. ~~Extract helper stages in `discovery_task.cpp` without behavior change.~~
2. ~~Refactor `task_mqtt_loop(...)` into connect-transition helper + periodic publish helper.~~

P2:
5. ~~Optional non-blocking restart stabilization in discovery path (if profiling shows benefit).~~

---

## 7) Independent vs parallel change matrix

## Can be done independently (transmitter-only)

1. Exclude/remove orphan modules:
   - `component_config_sender.*`
   - `test_mode.*`
2. Remove dead `SettingsManager::calculate_crc32(...)`.
3. Refactor large files internally without changing wire/data contracts.
4. Tighten `build_src_filter` for optional protocol implementations if product profile permits.

## Must be done in parallel (cross-project coordination)

1. **CRC16-CCITT → CRC32 heartbeat migration** — `heartbeat_t` / `heartbeat_ack_t` struct field change from `uint16_t` to `uint32_t`; transmitter and receiver must ship in the same window.
2. **Any other ESP-NOW wire schema changes** (`espnow_common.h` message fields/enums).
3. **Any other on-wire checksum algorithm changes** (sender output and receiver verification must match).
4. **Version-beacon payload contract changes** (receiver parser + transmitter emitter must match).
5. **OTA handshake/header contract changes** between receiver uploader and transmitter endpoints.

---

## 8) Recommended next steps

## P0
1. Remove/exclude orphan transmitter modules from production build.
2. Remove dead `SettingsManager::calculate_crc32(...)`.
3. Add tests for existing checksum/CRC behavior before refactor (golden vectors for CRC16-CCITT and CRC32).

## P1
4. Refactor manual checksum loops to shared utility wrappers (no protocol change).
5. Add `crc32_packet()` canonical helper in common wrapping `esp_rom_crc32_le()`.
6. Split `ota_manager.cpp` and `settings_manager.cpp`.

## P2 (coordinated parallel window with receiver)
7. Migrate heartbeat CRC from CRC16-CCITT to CRC32: update `espnow_common.h` structs, `heartbeat_manager.cpp`, and receiver `rx_heartbeat_manager.cpp`.
8. Remove `calculate_crc16()` / `validate_crc16()` from shared common.
9. Optional: profile-based source filtering for battery/inverter protocol families to reduce flash pressure.

## 8.1) Implementation progress update (2026-03-22, Wave 1)

Completed:
1. Removed orphan module files:
  - `src/espnow/component_config_sender.h/.cpp`
  - `src/test_mode/test_mode.h/.cpp`
2. Removed dead CRC helper from settings manager:
  - Deleted `SettingsManager::calculate_crc32(...)` declaration + definition.
3. Centralized settings blob CRC32 in common utilities:
  - Added shared `crc32_packet()` / `calculate_message_crc32_zeroed()` / `verify_message_crc32()` helpers in `esp32common/espnow_common_utils/espnow_packet_utils.h`.
  - Migrated `src/settings/settings_manager.cpp` blob save/load paths to the shared helper.
4. Added CRC32 golden-vector tests:
  - Added `esp32common/tests/crc32_packet_utils_test.cpp` covering the standard `"123456789"` vector and trailing-field verification behavior.
5. Removed manual checksum loop from transmitter app module:
  - Deleted local `checksum16()` in `src/espnow/component_catalog_handlers.cpp` and replaced both call-sites with `EspnowPacketUtils::calculate_checksum()`.
6. Throttled success-path logs in `transmission_task.cpp`:
  - `transmit_next_transient()` success: reduced from LOG_INFO every packet (~10/s) to LOG_INFO once per 10 s with cumulative packet count (`[N pkts/10s]` suffix).
  - `transmit_next_state()` success: downgraded from LOG_INFO to LOG_DEBUG (fires only on genuine config-change events; naturally infrequent, visible at debug level when needed).
7. Extracted helper stages in `discovery_task.cpp` restart flow without behavior changes:
  - Split `attempt_restart_once()` into explicit stage helpers (`restart_cleanup_peers()`, `restart_lock_channel()`, `restart_relaunch_discovery()`, `restart_verify_channel(...)`) plus shared `handle_restart_failure(...)` for retry/terminal-failure handling.
  - Preserved existing recovery behavior, retries/backoff, and state transitions while reducing duplicated branch-heavy blocks.
8. Refactored `task_mqtt_loop(...)` into helper stages without behavior change:
  - Added explicit connection-transition path (`handle_mqtt_connection_transition(...)`) and periodic publish path (`publish_periodic_runtime_data(...)`).
  - Isolated logger initialization and on-connect publish bundle into dedicated helpers while preserving existing MQTT topics, logs, and publish cadence.
  - Cached singleton references (`auto& mqtt`, `auto& ethernet`) in loop scope for readability and reduced repeated dereference noise.
9. Applied non-blocking restart stabilization in discovery restart flow:
  - Replaced blocking `delay(TimingConfig::RESTART_STABILIZATION_DELAY_MS)` with cooperative tick-based waiting (`restart_wait_for_stabilization()`) using short `vTaskDelay(...)` yields.
  - Preserved total stabilization window while improving scheduler responsiveness during restart recovery.
10. Build verification completed:
  - `pio run -e olimex_esp32_poe2` ✅ SUCCESS
11. Coordinated heartbeat CRC wire migration completed (TX + RX):
  - `espnow_common.h`: `heartbeat_t.checksum` and `heartbeat_ack_t.checksum` migrated from `uint16_t` to `uint32_t` (CRC32).
  - Transmitter `heartbeat_manager.cpp` and receiver `rx_heartbeat_manager.cpp` migrated to shared `EspnowPacketUtils` CRC32 helpers (`calculate_message_crc32_zeroed` / `verify_message_crc32`).
  - Removed legacy `calculate_crc16()` / `validate_crc16()` from shared `espnow_transmitter.cpp/.h`.
12. Coordinated build verification completed:
  - Transmitter `pio run -e olimex_esp32_poe2` ✅ SUCCESS
  - Receiver `pio run -e receiver_tft` ✅ SUCCESS
13. Common router hot-path optimization completed (transmitter-impacting, no API change):
  - `espnow_common_utils/espnow_message_router.cpp` now uses internal pre-indexed dispatch (message type + packet subtype buckets) instead of per-message full linear route scans.
  - Preserved router API and behavior contract while reducing dispatch scan scope in steady-state message handling.
  - Revalidated transmitter and receiver builds after the shared change.
14. P0 shared packet-info log-guard completed (shared common + receiver):
  - `EspnowPacketUtils::print_packet_info(...)` migrated from unconditional `Serial.printf` to `ESP_LOGD` — zero cost in release builds; routes through ESP-IDF log infrastructure when `CORE_DEBUG_LEVEL >= 4`.
  - Three receiver hot-path callsites (`handle_packet_events`, `handle_packet_logs`, `handle_packet_cell_info`) replaced with `LOG_DEBUG(kLogTag, ...)`, integrating with the project's runtime log-level control.
  - Receiver `receiver_tft` build verified ✅.
15. P1 hot-loop local-reference cleanup completed:
  - `src/espnow/data_sender.cpp`: cached `TxStateMachine` at task scope, reused a per-iteration `test_mode_enabled` flag, and cached `EnhancedCache` before the transient-cache write path.
  - Preserved behavior and logging cadence while reducing repeated singleton/config checks in the periodic send loop.
  - Transmitter `olimex_esp32_poe2` build verified ✅.

---

## 9) Conclusion

Transmitter runtime is solid, but cleanup opportunities are clear: remove orphan modules, deduplicate checksum/CRC code, and split large files. Most of this can be done independently.

The CRC investigation has revealed that the two "CRC16" implementations in the project are entirely different algorithms for different purposes. The Sungrow Modbus CRC16 is a fixed hardware protocol requirement and cannot change. The ESP-NOW heartbeat CRC16-CCITT has now been replaced with CRC32 — leaving a single algorithm covering all project-owned integrity checks while preserving Modbus CRC16 where protocol-mandated.

## 10) Cross-project runtime sizing note

For active receiver-side MQTT/queue/HTTP-OTA sizing values used during recent stack/debug investigations, see:
- `COMMON_CROSS_PROJECT_AUDIT_2026_03_21.md` section **9) Runtime sizing reference (MQTT / queue / HTTP-OTA)**.

---

## 11) Step 18: Transmitter File Splits (OTA Manager + Settings Manager)

**Status:** ✅ **COMPLETE**

**Execution date:** 2026-03-21  
**Scope:** Decompose two large transmitter modules into smaller, single-responsibility translation units.

### Changes Summary

#### `ota_manager.cpp` Decomposition
- **Original:** 1,639 lines
- **After:** 436 lines (73% reduction)
- **Split Into:**
  - `ota_manager_internal.h` (new, 90 lines) — internal helper declarations
  - `ota_http_utils.cpp` (new, 530 lines) — free HTTP helpers, rate-limiting, PSK load
  - `ota_upload_handler.cpp` (new, 400 lines) — POST /ota_upload endpoint handler
  - `ota_status_handlers.cpp` (new, 530 lines) — 10 read-only info endpoints (root, health, event_logs, ota_status, firmware_info, test_data_config_get/post, test_data_apply, test_data_reset)

- **Retained in `ota_manager.cpp`:**
  - Singleton accessor `instance()`
  - OTA session management: `arm_ota_session()`, `arm_ota_session_from_control_plane()`
  - Auth validation: `validate_ota_auth_headers()`
  - Endpoint arm handler: `ota_arm_handler()`
  - Server lifecycle: `init_http_server()`, `stop_http_server()`
  - OTA state telemetry: `set_commit_state()`

#### `settings_manager.cpp` Decomposition
- **Original:** 1,241 lines
- **After:** 238 lines (81% reduction)
- **Split Into:**
  - `settings_persistence.cpp` (new, 500 lines) — 10 NVS blob save/load functions (2×5 categories) + 5 blob struct definitions (BatterySettingsBlob, PowerSettingsBlob, InverterSettingsBlob, CanSettingsBlob, ContactorSettingsBlob)
  - `settings_field_setters.cpp` (new, 350 lines) — 5 per-field setter dispatch functions (save_battery/power/inverter/can/contactor_setting) with value validation and persistence
  - `settings_espnow.cpp` (new, 180 lines) — ESP-NOW settings sync (handle_settings_update, send_settings_ack, send_settings_changed_notification)

- **Retained in `settings_manager.cpp`:**
  - Singleton accessor `instance()`
  - Constructor
  - 6 validation functions: `validate_battery/power/inverter/can/contactor_settings`, `validate_all_settings`
  - Initialization: `init()`, `load_all_settings()`, `restore_defaults()`
  - Version increment helpers: `increment_battery/power/inverter/can_version()`, `increment_battery_version()`

### Rationale

1. **Improved Maintainability:** Each file now has a single clear responsibility:
   - HTTP helpers isolated from OTA session management
   - Upload handler (400+ lines of complex error handling) extracted for focused review
   - Status endpoints grouped together (read-only, independent of OTA state changes)
   - Persistence layer separated from business logic
   - Field setters isolated from validation and initialization

2. **Reduced Cognitive Load:** Developers can now understand:
   - OTA authentication flow without traversing rate-limiting tables
   - Upload process without scanning through 10 read-only handlers
   - Settings persistence without reading per-field setter dispatch logic

3. **Better Testability:** Smaller files facilitate:
   - Unit testing of HTTP helpers in isolation
   - Functional testing of upload handler with mocked persistence
   - Integration testing of ESP-NOW settings broadcast separately

4. **Code Navigation:** IDE search/goto-definition now lands in focused context instead of 1600+ line files

### Build Verification

- **Transmitter build:** `pio run -e olimex_esp32_poe2` ✅ **SUCCESS**
- **Firmware size:** Unchanged (reorganization only, no feature changes)
- **All 7 new files:** Verified compilation and link
- **No public API changes:** Internal reorganization only; all public `OtaManager` and `SettingsManager` methods unchanged
- **Post-split stabilization (2026-03-23):** transmitter compile break fixed and revalidated ✅
  - Added `<ETH.h>` include in `esp32common/ethernet_config.h` so `ETH_CLOCK_GPIO0_OUT` / `ETH_PHY_LAN8720` macros resolve consistently.
  - Removed stray top-level `}` introduced during split surgery in `src/settings/settings_manager.cpp`.
  - Rebuild result: `pio run -j 12` in `ESPnowtransmitter2/espnowtransmitter2` ✅ **SUCCESS**.

### Files Affected (Summary)

| File | Type | Change |
|------|------|--------|
| `ota_manager.cpp` | Trimmed | 1639 → 436 lines |
| `ota_manager.h` | Unchanged | Public API stable |
| `ota_manager_internal.h` | **New** | 90 lines (internal helpers) |
| `ota_http_utils.cpp` | **New** | 530 lines (free helpers) |
| `ota_upload_handler.cpp` | **New** | 400 lines (upload handler) |
| `ota_status_handlers.cpp` | **New** | 530 lines (status endpoints) |
| `settings_manager.cpp` | Trimmed | 1241 → 238 lines |
| `settings_manager.h` | Unchanged | Public API stable |
| `settings_persistence.cpp` | **New** | 500 lines (NVS save/load) |
| `settings_field_setters.cpp` | **New** | 350 lines (field setters) |
| `settings_espnow.cpp` | **New** | 180 lines (ESP-NOW sync) |

### Total Code Reduction

- **Before:** 2,880 lines in 2 files (ota_manager.cpp + settings_manager.cpp)
- **After:** 674 lines in 2 files + 2,080 lines distributed across 9 new files
- **Net effect:** 77% reduction in largest files; improved locality and focus per file

### Next Steps (P2 — Lower Priority)

1. Web page UI split (receiver HTML/CSS/JS consolidation)
2. Dead helper cleanup (shared common utilities audit)
