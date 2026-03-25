# Whole Codebase Audit and Hardening Recommendations — 2026-03-25

## Scope

Audited codebases:
- `ESPnowtransmitter2/espnowtransmitter2`
- `espnowreceiver_2`
- shared modules under `esp32common`

Audit focus:
- remaining magic numbers / hard-coded policy
- `String` usage and fragmentation risk
- manual memory management / ownership risks
- defensive-coding gaps
- industry-standard improvements
- candidate full rewrites where warranted

---

## Relationship to Prior Roadmap Work

This audit represents a **shift in focus from bloat reduction and structural decomposition to embedded production hardening**. Key distinction:

### Prior roadmap documents (P0-P4):
- **CODEBASE_ANALYSIS_AND_BLOAT_REDUCTION_ROADMAP.md** — Identified duplication and consolidation opportunities across spec pages, API handlers, and transmitter manager classes
- **RECEIVER_CODEBASE_FULL_AUDIT_2026_03_21.md** — Identified orphan/legacy modules and static heat paths
- **TRANSMITTER_CODEBASE_FULL_AUDIT_2026_03_21.md** — Identified checksum duplication, legacy components, and large monolithic files
- **RECEIVER_FILE_BY_FILE_COMMONIZATION_AND_REWRITE_ANALYSIS_2026_03_24.md** — Mapped receiver-to-shared extraction opportunities
- **RECEIVER_WEBSERVER_COMMONIZATION_AND_BLOAT_REDUCTION_ANALYSIS_2026_03_23.md** — Analyzed web layer consolidation

**The roadmap work executed steps P1-P4.4** successfully:
- ✅ Consolidated bloat through cross-project timing centralization (P3)
- ✅ Extracted shared boot/startup utilities (P4.1)
- ✅ Decomposed and re-composed state-machine DSLs (P4.2)
- ✅ Generated config-driven webserver content (P4.3)
- ✅ Extracted shared webserver layout utilities (P4.4)

**Result:** The codebase is now more modular and discoverable, but the focus on **decomposition and structural recomposition** did not directly address:
- Remaining dynamic allocations in hot paths
- Uneliminated magic number literals in refactored code
- String-based fragmentation risks in web/rendering paths
- Defensive-coding gaps in persistence and network layers

### This audit document:
- **Focuses on robustness, not structure**
- Treats the already-decomposed code as the starting point
- Identifies remaining **runtime-risk hotspots** that persist despite structural improvements
- Proposes **complete rewrites where existing structures (even if decomposed) are still fundamentally unsafe**

**Key insight:** Prior roadmap work cleaned up **code organization and duplication**. This audit cleans up **correctness, defensiveness, and memory safety in the reorganized code**. These are complementary efforts.

## Executive Summary

The codebase is in materially better shape than it was at the start of the roadmap work. The highest-value structural improvements already landed: shared timing centralization, boot/startup decomposition, shared webserver layout utilities, and removal of large amounts of compatibility/shim code.

However, this audit confirms that **the codebase is not yet fully free of magic numbers** and still contains several **heap-fragmentation and defensive-coding risks**, especially in:
- shared ESP-NOW queue internals
- receiver HTTP/web rendering paths
- transmitter OTA upload/auth helpers
- settings persistence write paths
- remaining validation logic containing embedded bounds and defaults

### Bottom-line assessment

- **Runtime-risk hotspots remain** in a few shared and networking paths.
- **Most `String` usage is web/UI-side and therefore lower risk**, but there are enough dynamic-string construction paths that a long-running system could still see fragmentation over time.
- **Magic numbers still exist** in validation, OTA policy, settings defaults, queue timeouts, and several web/formatting buffers.
- The codebase is now ready for a **hardening phase**, not just bloat reduction.

## Important Constraint / Truthful Status

It would be inaccurate to claim there are currently **zero** magic numbers left in the code.

This document identifies the remaining hotspots and the remediation needed to reach that standard. The correct next step is a deliberate hardening sweep that:
1. moves remaining policy values into named config/default-profile objects,
2. replaces allocator-heavy hot paths with deterministic storage,
3. removes long-lived `String` assembly from runtime request paths,
4. adds write-result and send-result verification where currently absent.

---

## Prioritized Findings

## 1. Shared ESP-NOW queue uses dynamic allocation in a long-lived runtime path
- **Severity:** High
- **Scope:** Shared
- **Files:**
  - `esp32common/espnow_common_utils/espnow_message_queue.h`
  - `esp32common/espnow_common_utils/espnow_message_queue.cpp`
- **Problem:** The shared queue relies on dynamic container behavior and mixed synchronization patterns for a long-lived embedded data path.
- **Why it matters:** This creates both heap-fragmentation risk and observability/race risk in firmware that is meant to run indefinitely.
- **Recommendation:** Replace with a fixed-capacity ring buffer backed by static storage with one lock discipline for all mutating and introspection operations.
- **Action type:** Full rewrite
- **Industry-standard direction:** Deterministic bounded queue, explicit overflow policy, metrics counters for drops/overwrites.

## 2. Receiver generic specs page builds full HTML into heap/PSRAM buffers per request
- **Severity:** High
- **Scope:** Receiver
- **Files:**
  - `espnowreceiver_2/lib/webserver/pages/generic_specs_page.cpp`
- **Problem:** The page path builds full response content in heap/PSRAM and separately formats a secondary buffer, mixing stack and heap assumptions.
- **Why it matters:** Under repeated HTTP use or low-memory conditions this increases fragmentation pressure and late failure probability.
- **Recommendation:** Rewrite to chunked/streamed HTTP output with bounded scratch buffers and explicit `httpd_resp_send*` result handling.
- **Action type:** Medium refactor
- **Rewrite note:** This is a good candidate for a shared “streaming HTML response builder” in `esp32common`.

**[2026-03-25] 🔄 IN PROGRESS** - `generic_specs_page.cpp` now streams header/spec/footer using `httpd_resp_send_chunk` with explicit send/finalization result checks, removing the large full-page PSRAM response assembly buffer. Additional page-path migrations to shared streaming helpers remain.

**[2026-03-25] 🔄 IN PROGRESS** - Added shared `send_chunked_html_response(...)` helper in `webserver_common_utils/spec_page_layout.*` and migrated `generic_specs_page.cpp` to use it, moving A4 from a page-local fix toward shared streaming infrastructure.

**[2026-03-25] ✅ COMPLETE** - Migrated `battery_specs_display_page.cpp` from manual PSRAM-buffer + `httpd_resp_send` to `GenericSpecsPage::send_formatted_page` (which routes through the shared chunked helper). All 4 spec display pages (`battery`, `charger`, `inverter`, `system`) now use chunked streaming via the shared `send_chunked_html_response` helper. Build: RX ✅ SUCCESS, TX ✅ SUCCESS.

## 3. OTA upload path still uses manual `malloc/free` with multi-branch cleanup
- **Severity:** High
- **Scope:** Transmitter
- **Files:**
  - `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_upload_handler.cpp`
- **Problem:** The OTA upload flow is careful, but still hand-manages ownership and cleanup through multiple success/failure branches.
- **Why it matters:** This is a security- and availability-critical path. Any future change can easily introduce leaks or incomplete cleanup.
- **Recommendation:** Introduce RAII-style scoped wrappers for upload buffers, OTA session state, and cleanup finalization; centralize all exits through one controlled commit/abort path.
- **Action type:** Medium refactor
- **Industry-standard direction:** Single owner/session object, explicit lifecycle states, guaranteed cleanup on all exits.

## 4. Settings persistence does not consistently verify NVS write success
- **Severity:** High
- **Scope:** Transmitter
- **Files:**
  - `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`
- **Problem:** Multiple persistence paths write many keys or blobs without enforcing return-value validation for every write.
- **Why it matters:** Partial persistence failure can silently leave inconsistent configuration and cause confusing recovery behavior after reboot.
- **Recommendation:** Centralize persistence through a canonical helper that checks every `put*` result, reports partial failure explicitly, and prefers blob-first canonical storage over many legacy keys.
- **Action type:** Medium refactor
- **Industry-standard direction:** Transaction-like persistence helper + versioned schema + explicit error surface.

**[2026-03-25] 🔄 IN PROGRESS** - Added centralized checked NVS helpers in `settings_persistence.cpp` and migrated battery/power/inverter/can/contactor save paths to verified `put*`/blob writes with explicit per-key error logging. Descriptor-driven schema/validation metadata still pending.

**[2026-03-26] ✅ FOLLOW-UP COMPLETE** - Moved all remaining legacy fallback defaults in `settings_persistence.cpp` into typed per-category default profiles (`BatteryLegacyDefaults`, `PowerLegacyDefaults`, `InverterLegacyDefaults`, `CanLegacyDefaults`, `ContactorLegacyDefaults`) so load paths no longer embed repeated inline domain defaults. Build: TX (`olimex_esp32_poe2`) ✅ SUCCESS (73.0s).

**[2026-03-26] ✅ FOLLOW-UP COMPLETE** - Added explicit per-category blob schema-version headers to `settings_persistence.cpp` (`BatterySettingsBlob`, `PowerSettingsBlob`, `InverterSettingsBlob`, `CanSettingsBlob`, `ContactorSettingsBlob`) and enforced schema checks during blob load alongside CRC verification before accepting persisted values. This closes the remaining schema-version gap in the canonical blob path while preserving legacy-key fallback behavior for mismatched/older blobs. Build: TX (`olimex_esp32_poe2`) ✅ SUCCESS (91.1s), RX (`receiver_tft`) ✅ SUCCESS (54.3s).

## 5. Settings transport handler assumes inbound string field is safe to log/use
- **Severity:** High
- **Scope:** Transmitter
- **Files:**
  - `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp`
- **Problem:** The transport layer length-checks the struct but still treats inbound string fields as if they are guaranteed NUL-terminated.
- **Why it matters:** This is a classic defensive-coding gap that can become an over-read/logging bug if sender-side packing changes or is malformed.
- **Recommendation:** Clamp-copy every inbound textual field into a bounded local buffer before any `%s` logging or forwarding to validation logic.
- **Action type:** Small refactor
- **Industry-standard direction:** Treat all network text as untrusted binary until safely copied/terminated.
  
**[2026-03-25] ✅ COMPLETE** - `settings_espnow.cpp` now clamp-copies inbound `value_string[32]` before logging/dispatch and replaces unsafe `strcpy` with `strlcpy`. Build: SUCCESS

## 6. Receiver deferred save / persistence scheduling can fail silently
- **Severity:** Medium
- **Scope:** Receiver
- **Files:**
  - `espnowreceiver_2/lib/webserver/utils/transmitter_nvs_persistence.cpp`
  - `espnowreceiver_2/lib/webserver/utils/transmitter_state.cpp`
  - `espnowreceiver_2/lib/webserver/utils/transmitter_network.cpp`
- **Problem:** Deferred timers and downstream NVS writes are not consistently checked and surfaced.
- **Why it matters:** The receiver can continue serving stale transmitter metadata/network state without obvious fault indication.
- **Recommendation:** Add explicit result handling, “dirty but not persisted” state flags, and throttled warning/error logs.
- **Action type:** Small refactor

**[2026-03-25] 🔄 RELATED HARDENING APPLIED** - Receiver OTA forwarding now uses retried challenge acquisition in `api_control_handlers.cpp`, replacing a brittle one-shot `/api/ota_arm` request path that could fail on transient `HTTP -1` transport errors. Additional receiver-side persistence scheduling hardening remains pending.

**[2026-03-25] ✅ COMPLETE** - `transmitter_nvs_persistence.cpp` hardened: `prefs.begin()` failure now emits `LOG_ERROR` instead of silently returning; `xTimerReset`/`xTimerStart` results are checked and emit `LOG_WARN` + fall back to synchronous persist on failure; added `g_nvs_save_pending` volatile flag set on schedule and cleared only after a successful `persist_to_nvs_now()` call; all debounce-timer unavailable paths log at WARN level. Build: RX ✅ SUCCESS.

## 7. Ethernet manager still contains embedded retry and reachability heuristics
- **Severity:** Medium
- **Scope:** Transmitter
- **Files:**
  - `ESPnowtransmitter2/espnowtransmitter2/src/network/ethernet_manager.cpp`
- **Problem:** Retry counts, reset behavior, and reachability heuristics remain partially hard-coded inside logic.
- **Why it matters:** Networking policy becomes difficult to tune, test, or explain from logs.
- **Recommendation:** Move all retry counts and reachability policy to named config values / structs and emit them in boot diagnostics.
- **Action type:** Medium refactor

**[2026-03-25] ✅ COMPLETE** - Eliminated all embedded magic literals from `ethernet_manager.cpp`:
- Removed 5 duplicate `static constexpr` timeout constants from `ethernet_manager.h` (were exact copies of `TimingConfig::ETHERNET_*`); all usages in `check_state_timeout()` now reference `TimingConfig::*` directly.
- Added `TimingConfig::ETHERNET_PHY_POWER_ASSERT_DELAY_MS = 10` to `timing_config.h`; replaced `delay(10)` and `delay(150)` with `TimingConfig::ETHERNET_PHY_POWER_ASSERT_DELAY_MS` / `TimingConfig::ETHERNET_INIT_DELAY_MS`.
- Added file-scope `static constexpr uint8_t kGatewayPingAttempts = 3` and `kConflictCheckPingAttempts = 2` in `.cpp`; replaced `Ping.ping(…, 3)` / `Ping.ping(…, 2)` with named constants.
- Added boot-time `LOG_INFO` emitting active timeout policy (PHY_RESET / IP_ACQUIRE / RECOVERY) and ping attempt counts for traceability.
Build: TX ✅ SUCCESS (93.8s), RX ✅ SUCCESS (75.2s). RX state machine can silently drop updates on lock contention
- **Severity:** Medium
- **Scope:** Receiver
- **Files:**
  - `espnowreceiver_2/src/espnow/rx_state_machine.cpp`
- **Problem:** Failed mutex acquisition can discard updates without counters or diagnostic visibility.
- **Why it matters:** Under stress, state/metrics may become misleading while the system appears healthy.
- **Recommendation:** Add lock-failure counters and throttled warnings, or switch read-mostly paths to lock-free snapshot semantics.
- **Action type:** Small refactor

**[2026-03-25] ✅ COMPLETE** - Added `lock_failures` field to `Stats`; added `mutable volatile uint32_t lock_failure_count_` private member (incremented without mutex — benign diagnostic counter); all 10 `xSemaphoreTake` failure paths now call `warn_lock_contention()` helper which emits `LOG_WARN` every 25th failure; `stats()` snapshots the counter into the returned struct. Build: RX ✅ SUCCESS.

## 9. Receiver MQTT task still has local hard-coded delays outside shared timing contract
- **Severity:** Medium
- **Scope:** Receiver
- **Files:**
  - `espnowreceiver_2/src/mqtt/mqtt_task.cpp`
- **Problem:** Startup/poll delays remain hard-coded in task logic instead of shared timing config.
- **Why it matters:** Cross-device behavior drifts and timing changes become harder to audit.
- **Recommendation:** Migrate all remaining task delays to `TimingConfig` and document intent.
- **Action type:** Small refactor

**[2026-03-25] ✅ COMPLETE** - Added `MQTT_TASK_STARTUP_DELAY_MS = 2000` and `MQTT_TASK_POLL_MS = 100` to `timing_config.h` with documenting comments; `mqtt_task.cpp` now includes `timing_config.h` and uses these constants in place of both magic literals. Build: RX ✅ SUCCESS, TX ✅ SUCCESS.

## 10. OTA auth / rate-limit policy is still encoded as TU-local constants
- **Severity:** Medium
- **Scope:** Transmitter
- **Files:**
  - `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_http_utils.cpp`
- **Problem:** Security policy values (attempt windows, cooldowns, tracked IP count, buffer lengths) are embedded in a single translation unit.
- **Why it matters:** Security tuning and abuse hardening require recompilation and are hard to reason about globally.
- **Recommendation:** Introduce a typed `OtaSecurityPolicy` config struct with explicit names, bounds, and telemetry.
- **Action type:** Medium refactor

**[2026-03-25] ✅ COMPLETE** - Introduced typed `OtaSecurityPolicy` in `ota_http_utils.cpp` and migrated OTA auth/PSK/rate-limit literals behind named fields.

**[2026-03-26] ✅ FOLLOW-UP COMPLETE** - Refined `OtaSecurityPolicy` into explicit nested `OtaPskPolicy` and `OtaAuthRateLimitPolicy` sub-policies, and bound generated-PSK length/buffer/random-byte counts to named policy fields so the remaining `32` / `64` / `65` / `5` / `60000` / `120000` / `8` literals are no longer embedded at call sites. Build: TX (`olimex_esp32_poe2`) ✅ SUCCESS (100.3s).

**[2026-03-26] ✅ FOLLOW-UP COMPLETE** - Removed transient `String` allocation from `load_ota_psk(...)` by switching NVS PSK retrieval to bounded `char[]` + `Preferences::getString(key, buffer, size)` copy flow, keeping existing external behavior while reducing allocator churn in the OTA auth path. Build: TX (`olimex_esp32_poe2`) ✅ SUCCESS (79.4s).

## 11. Shared web layout generator still performs large `String` concatenation
- **Severity:** Medium
- **Scope:** Shared / Receiver web stack
- **Files:**
  - `esp32common/webserver_common_utils/src/spec_page_layout.cpp`
- **Problem:** Large page fragments are assembled via repeated `String` concatenation.
- **Why it matters:** This is not as dangerous as a tight loop path, but repeated page serving can still fragment heap over long uptime.
- **Recommendation:** Convert to static HTML fragments, PROGMEM constants, or streamed output builders.
- **Action type:** Medium refactor

**[2026-03-25] 🔄 IN PROGRESS** - Reduced transient `String` allocation pressure in `spec_page_layout.cpp` by replacing variable-interpolated concatenation expressions in `build_spec_page_html_header(...)` with sequential `append`/`+=` steps while keeping function signatures and output content unchanged. Remaining `String` return API and broader static/PROGMEM migration still pending. Build: RX (`pio run -e receiver_tft -j 12`) ✅ SUCCESS, TX (`pio run -j 12`) ✅ SUCCESS.

**[2026-03-25] 🔄 IN PROGRESS** - Further reduced transient allocation churn in `build_spec_page_nav_links(...)` by replacing multi-step per-link `String` appends with bounded `snprintf(...)` into a fixed stack buffer followed by a single append per link. API and rendered output remain unchanged.

**[2026-03-25] 🔄 IN PROGRESS** - Consolidated `build_spec_page_html_header()`: merged all adjacent static-only `html +=` chains into multi-line C string literal concatenations, reducing `String::operator+=` calls from ~50 to 31 while keeping rendered HTML and function API identical. Applied matching minor consolidation to `build_spec_page_html_footer()` (7 ops → 5 ops). `build_spec_page_html_header()` and footer full PROGMEM/static-fragment migration still pending. Build: RX ✅ SUCCESS (75.2s), TX ✅ SUCCESS (93.8s). Receiver helper utilities still return `String` for fixed-format values

**[2026-03-26] 🔄 MAJOR PROGRESS** - Added shared `SpecPageParams` and `send_spec_page_response(...)` streaming API in `webserver_common_utils/spec_page_layout.*`, including a flash-resident static CSS block (`kSpecPageStaticCss`) and chunked end-to-end page emission (`httpd_resp_send_chunk`) without constructing full `String` headers/footers. Migrated all 4 receiver spec pages (`battery`, `inverter`, `charger`, `system`) to the streaming sender. Build: RX (`receiver_tft`) ✅ SUCCESS.
- **Severity:** Medium
- **Scope:** Receiver
- **Files:**
  - `espnowreceiver_2/lib/webserver/utils/transmitter_identity.cpp`
  - `espnowreceiver_2/lib/webserver/utils/transmitter_network.cpp`
  - `espnowreceiver_2/lib/webserver/utils/transmitter_state.cpp`
- **Problem:** IP/MAC/metadata helpers allocate `String` for small bounded values.
- **Why it matters:** Low risk individually, but these are easy wins in a hardening pass.
- **Recommendation:** Replace with caller-supplied buffers, `snprintf`, or fixed-size return structs.
- **Action type:** Small refactor

**[2026-03-25] ✅ COMPLETE** - Migrated helper fixed-format paths to caller-supplied buffers with compatibility wrappers preserved:
- `transmitter_identity.*`: added `bool format_mac(const uint8_t*, char*, size_t)` and `bool get_mac_string(char*, size_t)`; legacy `String` APIs now wrap buffer versions.
- `transmitter_network.*`: added `bool format_ip(const uint8_t*, char*, size_t)`, `bool get_ip_string(char*, size_t)`, and `bool get_url(char*, size_t)`; legacy `String` APIs now wrap buffer versions.
- Logging hardening: `register_mac(...)` and `store_ip_data(...)` now log from stack buffers (no temporary `String` construction).
- `transmitter_state.cpp`: `load_metadata_from_prefs(...)` now uses `Preferences::getString(..., char*, size_t)` directly into fixed buffers with enforced NUL termination.
Build: RX (`pio run -e receiver_tft -j 12`) ✅ SUCCESS, TX (`pio run -j 12`) ✅ SUCCESS.

## 13. Shared timing is centralized but increasingly acts as a flat “numbers warehouse”
- **Severity:** Low
- **Scope:** Shared
- **Files:**
  - `esp32common/include/esp32common/config/timing_config.h`
  - `esp32common/espnow_common_utils/espnow_timing_config.h`
- **Problem:** Centralization is good, but policy values are becoming less grouped by behavior and rationale.
- **Why it matters:** Tuning will eventually become error-prone without structure.
- **Recommendation:** Group timing into typed scenario-based structs (`DiscoveryTiming`, `HeartbeatTiming`, `MqttTiming`, `OtaTiming`, etc.) and emit these at startup in diagnostics.
- **Action type:** Small refactor

**[2026-03-25] ✅ COMPLETE** - Added typed scenario-based timing profiles in `timing_config.h` (`StartupTiming`, `DiscoveryTiming`, `HeartbeatTiming`, `DataTransmissionTiming`, `VersionBeaconTiming`, `EthernetTiming`, `MqttTiming`, `TimeSyncTiming`, `OtaTiming`, `LoopTiming`) with compatibility aliases preserved for all existing `TimingConfig::*_MS` call sites. Updated `espnow_timing_config.h` to reference grouped canonical discovery/heartbeat fields for overlapping shared policy, and added grouped timing diagnostics to both receiver and transmitter boot logs. Build: RX (`receiver_tft`) ✅ SUCCESS, TX (`olimex_esp32_poe2`) ✅ SUCCESS.

**[2026-03-26] ✅ FOLLOW-UP COMPLETE** - Added `ReceiverEspnowTiming` to `timing_config.h` for receiver queue receive / stale / config-grace / queue-stats intervals, migrated `espnowreceiver_2/src/espnow/espnow_tasks.cpp` to those shared aliases, and grouped the remaining shared `EspNowTiming` policy literals in `espnow_timing_config.h` into typed policy objects (`ChannelLockTiming`, `DiscoveryPolicyTiming`, `HeartbeatPolicyTiming`, `RetryTiming`, `QueueTiming`, `WatchdogTiming`, etc.) while preserving legacy flat aliases for compatibility. Build: RX (`receiver_tft`) ✅ SUCCESS (61.5s), TX (`olimex_esp32_poe2`) ✅ SUCCESS (75.1s).

## 14. Validation/business-rule bounds are still embedded in setter logic
- **Severity:** Medium
- **Scope:** Transmitter
- **Files:**
  - `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_field_setters.cpp`
- **Problem:** Domain limits are coded inline in setter logic.
- **Why it matters:** This mixes validation policy with transport/update code and complicates traceability.
- **Recommendation:** Move all limits/defaults into declarative field descriptors or policy tables.
- **Action type:** Medium refactor
- **Rewrite note:** Good candidate for a descriptor-driven validation engine.

**[2026-03-25] 🔄 IN PROGRESS** - Implemented descriptor-driven battery field validation metadata and replaced inline battery range checks in `settings_field_setters.cpp`. Remaining categories still need descriptor coverage.

**[2026-03-25] ✅ COMPLETE** - Extended descriptor-driven validation to all remaining settings categories (Power, Inverter, CAN, Contactor). Generalized struct types to shared `UIntFieldDescriptor`/`FloatFieldDescriptor` (anon namespace); replaced four separate find/validate pairs with two reusable templates; added `LOG_INFO` for all previously-silent non-battery setter cases. Build: TX ✅ SUCCESS.

## 15. Large JS/CSS page factories are still allocator-heavy
- **Severity:** Low-Medium
- **Scope:** Receiver
- **Files:**
  - `espnowreceiver_2/lib/webserver/pages/monitor_page_script.cpp`
  - `espnowreceiver_2/lib/webserver/pages/monitor2_page_script.cpp`
  - `espnowreceiver_2/lib/webserver/pages/debug_page_script.cpp`
  - `espnowreceiver_2/lib/webserver/pages/event_logs_page_script.cpp`
- **Problem:** Repeated generation of large script/style strings.
- **Why it matters:** Mostly maintainability + some heap churn under repeated page loads.
- **Recommendation:** Move static assets to flash-resident constant resources or chunked send helpers.
- **Action type:** Medium refactor

**[2026-03-26] 🔄 IN PROGRESS** - `send_rendered_page(...)` in `page_generator.cpp` now emits chunked HTTP directly (no full-page `String html = renderPage(...)` allocation in the send path). Also migrated `dashboard_page.cpp` and `settings_page.cpp` to `send_rendered_page(...)` so they no longer build a full page `String` and call `httpd_resp_send(...)` directly. Build: RX (`receiver_tft`) ✅ SUCCESS.

**[2026-03-26] ✅ COMPLETE (current page-script/style factories)** - Added static-asset fast path to `PageRenderOptions` (`const char*` style/script pointers), and migrated all current `*_page_script` / `*_page_styles` factories to `const char*` return APIs (including monitor, monitor2, debug, event logs, OTA, dashboard, battery settings, cellmonitor, transmitter hub). This removes large temporary `String` construction for static JS/CSS payloads during request handling while preserving behavior. Build: RX (`receiver_tft`) ✅ SUCCESS.

**[2026-03-26] ✅ COMPLETE (item #15 scope in this codebase)** - Combined with chunked `send_rendered_page(...)`, this closes the allocator-heavy page-factory hotspot identified for receiver JS/CSS script/style generators.

**[2026-03-26] ✅ FOLLOW-UP COMPLETE (static page body fast path)** - Added `send_rendered_page(...)` overload for `const char*` title/content in `page_generator.h/.cpp`, then migrated `cellmonitor_page_content` from `String` to static `const char*` and updated `cellmonitor_page.cpp` to use the non-allocating path. This removes the last `String`-returning page-content factory in receiver web pages. Build: RX (`receiver_tft`) ✅ SUCCESS (60.27s).

---

## Remaining Magic-Number Hotspots

These are representative remaining hotspots that should be treated as active remediation targets.

### Transmitter
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_upload_handler.cpp`
  - **[2026-03-26] ✅ RESOLVED** — upload chunk size, progress log cadence, timeout warning cadence, and max receive-timeout thresholds are now grouped in typed `kOtaUploadPolicy` (`ota_manager_internal.h`) and consumed by `ota_upload_handler.cpp`.
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_http_utils.cpp`
  - **[2026-03-26] ✅ RESOLVED** — OTA auth policy now uses explicit nested PSK and rate-limit sub-policies, and generated-PSK size/format values are bound to named policy fields.
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ethernet_manager.cpp`
  - **[2026-03-25] ✅ RESOLVED** — PHY/reset/reachability literals (`10`, `150`, `2`, `3`) replaced with named `TimingConfig::*` and file-scope `k*` constants; duplicate class constants removed.
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`
  - **[2026-03-26] ✅ RESOLVED** — legacy fallback defaults are now grouped into typed per-category default profiles instead of repeated inline literals in load paths.
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_field_setters.cpp`
  - **[2026-03-26] ✅ RESOLVED** — validation remains descriptor-driven and now uses named `UIntRange` / `FloatRange` policy constants instead of raw inline numeric bound pairs in descriptor rows.
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp`
  - **[2026-03-26] ✅ RESOLVED** — active channel-hopping scan now uses named file-scope policy constants for channel table and loop poll cadence, removes duplicated inline channel list/count literals, replaces inline retry/poll/settle delays (`10`, `50`, `5s`, `13s`) with named constants/computed values, and derives full-scan/discovery duration logs from `TimingConfig::TRANSMIT_DURATION_PER_CHANNEL_MS` and channel-count metadata.

### Receiver
- `espnowreceiver_2/src/mqtt/mqtt_task.cpp`
  - **[2026-03-25] ✅ RESOLVED** — startup/poll delays migrated to shared `TimingConfig::MQTT_TASK_*` constants.
- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
  - **[2026-03-26] ✅ RESOLVED** — queue receive / stale / config-grace / queue-stats thresholds migrated to shared `TimingConfig::RX_ESPNOW_*` aliases.
- `espnowreceiver_2/lib/webserver/pages/generic_specs_page.cpp`
  - **[2026-03-26] ✅ RESOLVED** — fixed formatting scratch size now uses named constant `kDefaultSpecsScratchBufferBytes` instead of inline literal `2048`.

### Shared
- `esp32common/espnow_common_utils/espnow_timing_config.h`
  - **[2026-03-26] ✅ RESOLVED** — remaining shared ESP-NOW policy values are now grouped into typed policy objects with compatibility aliases preserved for existing call sites.

### Recommendation for “no magic numbers” standard
To meet a strict no-magic-number standard, adopt these rules:
- no embedded policy literals in function bodies
- no repeated fallback defaults inline
- all thresholds/retries/sizes/timeouts live in typed config/default-profile objects
- all field-validation bounds live in declarative metadata tables
- any unavoidable wire-format constants must be named and documented once near protocol definitions

---

## `String` / Fragmentation / Leak-Risk Hotspots

## Higher concern
- `esp32common/webserver_common_utils/src/spec_page_layout.cpp`
  - large `String` concatenation chains for HTML layout generation
- `espnowreceiver_2/lib/webserver/pages/generic_specs_page.cpp`
  - mixed `String` + heap/PSRAM response building
- `espnowreceiver_2/lib/webserver/pages/monitor_page_script.cpp`
- `espnowreceiver_2/lib/webserver/pages/monitor2_page_script.cpp`
- `espnowreceiver_2/lib/webserver/pages/debug_page_script.cpp`
- `espnowreceiver_2/lib/webserver/pages/event_logs_page_script.cpp`
  - large script/style blobs assembled dynamically per request

## Lower concern but worth cleaning
- `espnowreceiver_2/lib/webserver/utils/transmitter_identity.cpp`
- `espnowreceiver_2/lib/webserver/utils/transmitter_network.cpp`
- `espnowreceiver_2/lib/webserver/utils/transmitter_state.cpp`
  - fixed-format helper output currently returned as `String`
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_http_utils.cpp`
  - `Preferences::getString` and similar transient string usage in security-sensitive path
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_status_handlers.cpp`
  - status formatting with `String` in HTTP-only code path

**[2026-03-26] ✅ FOLLOW-UP COMPLETE (transmitter event-message path)** - Added non-allocating `get_event_message(EVENTS_ENUM_TYPE, char*, size_t)` in `events.cpp`/`events.h` and migrated OTA event-log JSON (`ota_status_handlers.cpp`) plus MQTT event-log publish JSON (`mqtt_manager.cpp`) to caller-supplied fixed buffers instead of `get_event_message_string(...)` temporaries. Also kept `String` API as compatibility wrapper for untouched call sites. Build: TX (`olimex_esp32_poe2`) ✅ SUCCESS (77.7s).

### Defensive coding standard for strings
Recommended policy:
- **No `String` in loop-frequency or long-lived background paths**
- **No `String` for fixed-format identifiers** (MAC/IP/version/build-date)
- **No direct `%s` logging of inbound transport/network buffers**
- Prefer:
  - `snprintf` into caller-provided fixed buffers
  - `std::array<char, N>` / POD structs where toolchain permits
  - flash-resident static fragments for HTML/JS/CSS
  - streamed/chunked HTTP responses instead of whole-page string assembly

---

## Complete Rewrite Candidates

### Overview

This audit identifies **four complete rewrite candidates** — all involving **replacement of existing decomposed/structured code** with fundamentally different implementations to address runtime-safety and resource-determinism concerns:

### 1. Shared ESP-NOW queue — Full rewrite (Shared runtime path)
**Files:**
- `esp32common/espnow_common_utils/espnow_message_queue.h`
- `esp32common/espnow_common_utils/espnow_message_queue.cpp`

**Current design problem:**
- Uses dynamic std::deque or similar resizable container for message storage
- Mixed synchronization: per-operation mutex + separate condition variables
- Unbounded growth under pause/stall conditions
- No overflow policy or metrics

**Rewrite target:**
- Fixed-capacity ring buffer (preallocated at creation)
- Single-lock discipline for all operations (enqueue, dequeue, introspection)
- Explicit overflow policy selection: `DROP_OLDEST`, `DROP_NEWEST`, or `REJECT`
- Embedded metrics: `push_failures`, `overflow_count`, `max_depth_seen`, `current_depth`
- Fully deterministic memory footprint

**Why this is not just a refactor:**
The current queue is fundamentally **allocator-dependent**. Refactoring would only patch symptoms. A ring buffer is a different algorithm entirely that eliminates the dependency.

**Scope impact:**
- Shared dependency used by both TX discovery flow and RX message router
- High visibility change but fully backward-compatible at interface level (same `push()`, `pop()`, `is_empty()`)
- Integration risk: **Low** (if queue semantics for overflow are carefully chosen)

---

### 2. Receiver generic page rendering + shared spec layout — Full rewrite path
**Files:**
- `espnowreceiver_2/lib/webserver/pages/generic_specs_page.cpp`
- `esp32common/webserver_common_utils/src/spec_page_layout.cpp`
- `esp32common/webserver_common_utils/include/webserver_common_utils/spec_page_layout.h`

**Current design problem:**
- `spec_page_layout.cpp` builds entire page HTML via repeated `String` concatenation (multiple passes)
- `generic_specs_page.cpp` allocates full response buffers in heap/PSRAM per request
- Two separate buffer strategies (stack + heap) not coordinated
- No explicit `httpd_resp_send*` result checking

**Rewrite target:**
- Introduce `StreamingHtmlBuilder` helper class in `esp32common` that wraps chunked/direct HTTP sending
- Replace `String` concatenation with sequential `httpd_resp_send*` calls with bounded (~256-512B) scratch buffers
- Pre-compute static HTML fragments as `const char` arrays in PROGMEM
- Reserve heap only for **live data values**, not page structure
- Explicit error surface for send failures

**Implementation note:**
This is a **paired rewrite** of both receiver and shared code. The receiver's current generic page is too expensive; the shared layout builder (introduced in P4.4) inherits the same problem. Rewriting both together prevents the pattern from spreading to future projects.

**Why this is not just a refactor:**
Current code builds **entire pages in memory**. The rewrite changes the **algorithm** to **chunked/streaming**. This is a different contract with HTTP layer and requires different internal structure.

**Scope impact:**
- Affects all spec pages (battery, inverter, charger, system) since they all use `generic_specs_page` 
- Affects any future projects that copy `spec_page_layout` shared code
- HTTP API surface unchanged (handler signature same, response content same)
- Memory pressure dramatically reduced under high page-serving load

---

### 3. OTA upload flow and session management — Medium-to-full rewrite
**Files:**
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_upload_handler.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_http_utils.cpp` (security/rate-limit policy portions)

**Current design problem:**
- Manual `malloc(...)` for upload buffer and temporary digest state
- Cleanup spread across success and failure branches throughout the function
- Auth/rate-limit heuristics embedded inline rather than structured
- No RAII wrapper ensuring cleanup on all exit paths
- Security/policy constants hardcoded and scattered

**Rewrite target:**
- Introduce `OtaUploadSession` object encapsulating:
  - `unique_ptr<uint8_t[]>` for upload buffer (automatic cleanup)
  - `esp_ota_handle_t` lifecycle (one error-checking finalize call)
  - Digest/hash state machine
  - Current telemetry (bytes_received, chunk count, errors)
- Restructure flow to single commit/abort exit path
- Extract security policy into `OtaSecurityPolicy` struct (attempt window, cooldown, tracked IPs, buffer sizes)
- Replace inline magic numbers with named policy fields

**Why this is not just a refactor:**
The **RAII pattern itself** requires restructuring around scoped object lifetime. You cannot layer RAII on top of scattered malloc/free without essentially rewriting the control flow.

**Scope impact:**
- Limited surface (OTA is security-critical and tightly scoped)
- HTTP API surface unchanged (handler signature, response codes same)
- Internal correctness dramatically improved (impossible to forget cleanup)
- Future changes to OTA auth policy become declarative instead of archaeological

---

### 4. Settings persistence + field validation — Medium rewrite with full validation subcomponent
**Files:**
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_field_setters.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp`

**Current design problem:**
- Multiple persistence paths write many NVS keys without consistent result validation
- Partial write failures silently leave inconsistent state
- Validation bounds embedded in setter logic, not declarative
- Inbound transport strings treated as NUL-terminated without clamp-copy first
- Version/rollback strategy unclear

**Rewrite target:**
- Introduce `SettingsSchema` descriptor table specifying all fields:
  ```cpp
  struct SettingsFieldDescriptor {
    const char* key;
    SettingsType type;
    size_t max_len;  // for strings
    double min_val, max_val;  // for numeric
    bool required;
  };
  ```
- `SettingsPersistenceManager` class handling:
  - Descriptor-driven validation on inbound
  - Canonical blob store + version header + full-blob CRC
  - Per-write verification on all `put*` operations
  - Explicit "dirty but not persisted" state flags
  - Rollback/schema-mismatch recovery
- Replace **all** string transport with clamp-copy before validation/logging

**Why this is not just a refactor:**
Validation and persistence are currently **tightly coupled with transport logic**. Decoupling them requires introducing the descriptor layer **throughout** the stack, not just patching one file.

**Scope impact:**
- High visibility (settings are used everywhere)
- Interface-level changes possible but manageable (setters already have result types)
- Provides template for other projects' settings management
- Makes configuration policy auditable in one place

---

## Rewrite Prioritization

**Highest → Lowest impact for hardening:**

1. **Shared ESP-NOW queue** (Shared, high impact on lifetime stability)
2. **OTA upload session** (Transmitter, high impact on security/correctness)
3. **Settings persistence + validation** (Transmitter, high impact on recovery/consistency)
4. **Receiver page rendering + shared layout** (Receiver + Shared, high impact on memory pressure)

All four are full rewrites; none are incremental refactors.

---

## Industry-Standard Improvement Set

Recommended standards for the next hardening phase:

### Memory and allocation
- use fixed-capacity data structures in continuous runtime paths
- avoid dynamic allocation in loops, callbacks, ISR-adjacent paths, and message pipelines
- prefer RAII wrappers around `Preferences`, OTA sessions, heap buffers, and locks
- introduce allocation-failure counters and largest-free-block telemetry

### Strings and text handling
- use bounded C buffers for protocol-facing or repeated formatting
- keep page assets/static fragments in flash where possible
- stream HTTP responses instead of concatenating large `String`s
- treat all inbound text as untrusted bytes until copied into bounded local buffers

### Configuration and policy
- move all remaining thresholds/retries/defaults into named policy structs
- group config by behavior, not by primitive type
- emit active timing/security policy in boot logs for traceability

### Defensive coding
- check every storage write result
- check every `httpd_resp_send*` result
- count and expose lock acquisition failures, dropped queue items, failed timers, and aborted saves
- prefer explicit state transition tables over implicit transition logic

### Testing and observability
- add stress tests for:
  - repeated HTTP page serving
  - OTA upload abort/retry/failure cases
  - NVS persistence partial failure handling
  - queue overflow and reconnect storms
- add runtime telemetry for heap fragmentation and long-lived web activity

---

---

## Suggested Next Implementation Order

### Phase A — Runtime-risk hardening (highest value)
1. Rewrite shared ESP-NOW queue to deterministic fixed storage
**[2026-03-25] ✅ COMPLETE** - Replaced std::queue with fixed-capacity ring buffer. Old code removed. Build: SUCCESS

2. Rewrite OTA upload flow to RAII-based session management and policy structs
**[2026-03-25] ✅ COMPLETE** - Added RAII resource owner for OTA upload buffer + SHA context, removed duplicated cleanup paths, centralized failure handling, and introduced typed OTA security policy struct. Old cleanup/policy literals removed. Build: SUCCESS

3. Rewrite settings persistence to descriptor-driven validation + centralized NVS helper
**[2026-03-25] ✅ COMPLETE** - Completed settings transport defensive hardening (bounded string clamp-copy + safe copy APIs), centralized checked NVS write/read helpers with per-key verification logging, and descriptor-driven validation coverage for Battery/Power/Inverter/CAN/Contactor field setters.

4. Rewrite receiver generic page rendering to streaming/chunked output (paired with shared layout helper)
**[2026-03-26] ✅ COMPLETE** - Spec pages and generic specs rendering are chunked; `send_rendered_page(...)` now streams directly; and large JS/CSS script/style factories were migrated to static `const char*` assets routed through the non-allocating fast path.

### Phase B — Eliminate remaining magic numbers
5. Move settings field validation bounds to descriptor tables
6. Move OTA security/rate-limit policy to typed `OtaSecurityPolicy` config
**[2026-03-25] ✅ COMPLETE (as part of A2)** - Implemented `kOtaSecurityPolicy` and migrated all OTA auth/PSK literals to typed fields in `ota_http_utils.cpp`
**[2026-03-26] ✅ FOLLOW-UP COMPLETE** - Split `kOtaSecurityPolicy` into nested PSK/rate-limit sub-policies and moved generated-PSK sizing/format values behind named policy fields in `ota_http_utils.cpp`. Build: TX (`olimex_esp32_poe2`) ✅ SUCCESS (100.3s).
**[2026-03-26] ✅ FOLLOW-UP COMPLETE** - Added typed `kOtaUploadPolicy` for OTA upload chunking/progress/timeout cadence in `ota_manager_internal.h` and removed corresponding magic literals from `ota_upload_handler.cpp`. Build: TX (`olimex_esp32_poe2`) ✅ SUCCESS (97.0s).

7. Migrate all remaining RX MQTT / ESP-NOW task literals into shared `TimingConfig`
**[2026-03-26] ✅ COMPLETE** - Receiver MQTT task delays were already migrated to `TimingConfig::MQTT_TASK_*`; this follow-up added `TimingConfig::ReceiverEspnowTiming` / `RX_ESPNOW_*` aliases and removed the remaining queue receive / stale / config-grace / queue-stats literals from `espnow_tasks.cpp`. Build: RX (`receiver_tft`) ✅ SUCCESS (61.5s), TX (`olimex_esp32_poe2`) ✅ SUCCESS (75.1s).
8. Group shared timing into typed scenario structs (`DiscoveryTiming`, `HeartbeatTiming`, etc)
**[2026-03-25] ✅ COMPLETE** - Shared timing now exposes typed scenario structs with preserved flat aliases, and both device boots emit grouped timing policy diagnostics for traceability.

### Phase C — Remove allocator-heavy String paths
9. Replace fixed-format `String` helpers with bounded buffer APIs (MAC/IP/version helpers)
**[2026-03-26] ✅ COMPLETE** - Receiver helper fixed-format paths (`transmitter_identity.*`, `transmitter_network.*`, `transmitter_state.cpp`) were migrated to caller-supplied bounded buffers with compatibility wrappers preserved.
10. Move large JS/CSS/HTML assets toward PROGMEM or streamed output builders
**[2026-03-26] ✅ COMPLETE** - Receiver page script/style factories were migrated to static `const char*` assets and routed through the non-allocating fast path in `send_rendered_page(...)`.
11. Remove `String` use from HTTP status/utility helpers where practical
**[2026-03-26] ✅ COMPLETE** - Transmitter event-message HTTP/MQTT JSON paths now use non-allocating buffer APIs; receiver rendering path gained `const char*` overload plus static page-body migration for cellmonitor.
12. Add long-duration stress tests for heap fragmentation and page-serving load
**[2026-03-25] ⏸️ DEFERRED BY REQUEST** - Stress-testing work is intentionally postponed for now and removed from immediate next-step scope. Resume when requested.

---

## Comparison to Prior Bloat-Reduction Roadmap

| Aspect | Prior Roadmap (P0-P4) | This Hardening Audit |
|--------|----------------------|----------------------|
| **Goal** | Eliminate duplication, improve discoverability, reduce lines of code | Improve correctness, determinism, defensive coding, memory safety |
| **Approach** | Consolidate, extract to shared libs, decompose monoliths | Rewrite unsafe/dynamic patterns to bounded/static alternatives |
| **Scope of change** | Interface-level (public API facades), code organization | Algorithm-level (queue structure, HTTP rendering, persistence schema) |
| **Impact on structure** | Better: separated concerns, shared utilities, clear dependencies | Better: deterministic memory, explicit policy, no hidden allocations |
| **Example P4.1** | Extracted startup tasks into `RuntimeTaskStartup` class for clarity | (N/A — clarity already achieved; this audit looks at internals) |
| **Example P4.2** | Introduced `kTransitionRules` table for state-machine visibility | (N/A — table visibility is good; queue internals are still unsafe) |
| **Example P4.3** | Replaced hardcoded nav HTML strings with config tables | (N/A — tables are good; this audit identifies webserver page-building itself is allocator-heavy) |
| **Example P4.4** | Extracted shared webserver layout utilities to common lib | (But the extraction inherited the same `String`-concatenation problem — audit calls for rewrite) |
| **Typical rewrite** | One-to-many consolidation (3 spec pages → 1 generic generator) | One-to-one replacement (current queue → ring buffer, current builder → streaming builder) |
| **Risk profile** | Low: mostly cleanup, validates against existing logic, test coverage carries over | Medium-High: fundamental algorithm changes, new patterns, increased test surface |

**Key insight:** The prior roadmap succeeded at **making code discoverable and well-organized**. This audit succeeds at **making organized code also safe and deterministic**. Both are necessary.

---

## Recommended Definition of Done for the Hardening Pass

The codebase can be considered hardened when all of the following are true:
- no policy magic numbers remain in function bodies
- all validation bounds/defaults are descriptor-driven
- no `String` remains in hot or long-lived runtime paths
- shared queue/message paths are deterministic and allocation-free
- all persistence writes and HTTP sends are checked
- all inbound text handling is bounded and explicitly terminated
- heap-fragmentation telemetry exists and stays stable under long-duration soak testing

---

## Final Assessment

The roadmap implementation successfully removed substantial bloat and improved structure, but the codebase now needs a dedicated **robustness and defensive-coding phase**.

The most important recommendation is to treat the next pass not as cleanup, but as **embedded production hardening**:
- deterministic memory use
- explicit policy/config ownership
- no hidden allocator churn in serving or messaging paths
- no silent failure in persistence or state updates

If desired, this audit can be converted next into a step-by-step implementation roadmap with file-by-file work items and validation criteria.
