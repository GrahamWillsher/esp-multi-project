# espnowreceiver_2 vs espnowreceiver_LCD — Mechanics Parity Audit
**Date:** 2026-04-16  
**Author:** GitHub Copilot  
**Goal:** Identify where `_2` and `_lcd` differ, excluding physical display control, and provide recommendations so core mechanics remain hardware-abstract.

---

## 1) Scope and method

I compared both codebases with emphasis on **non-display mechanics**:
- ESP-NOW connection/message/runtime behavior
- Settings sync/apply flow
- Webserver/API behavior
- Network boot/config behavior
- Data model/cache behavior
- MQTT ingest behavior

Display-only areas (expected differences) were treated as out-of-scope:
- `_2`: `src/display/**`, `src/hal/display/**`, `src/hal/tft_*`
- `_lcd`: `src/ui/**`, `src/hal/lgfx_waveshare_7.h`, LVGL/LGFX renderer specifics

### Structural comparison snapshots
- Webserver trees (`lib/webserver` vs `lib/webserver_lcd`):
  - Common files by relative path: **170**
  - Byte-identical (SHA-256): **166**
  - Different: **4**
  - `_lcd`-only extras: **8**
- ESP-NOW trees (`src/espnow`):
  - Common files by relative path: **21**
  - Byte-identical: **5**
  - Different: **16**
  - `_2`-only files: **12**
  - `_lcd`-only files: **4**

---

## 2) High-confidence parity (good)

## 2.1 Webserver parity is very high
Most page/API/utils mechanics are aligned between:
- `_2`: `lib/webserver/**`
- `_lcd`: `lib/webserver_lcd/**`

Only 4 common-path files differ materially:
- `api/api_telemetry_handlers.cpp`
- `pages/dashboard_page_script.cpp`
- `pages/dashboard_page.cpp`
- `webserver.cpp`

These deltas are mostly policy-level (see section 4), not architecture-level.

## 2.2 MQTT ingestion mechanics are functionally aligned
- `src/mqtt/mqtt_client.cpp` logic is effectively the same (topic routing, subscriptions, parsing, caching).
- `src/mqtt/mqtt_task.cpp` loop behavior is effectively the same.

Differences are mostly include-path and logging/header style.

## 2.3 Core state machine behavior is aligned
`src/espnow/rx_state_machine.cpp` implementations are near-equivalent in lock/transition/stale detection behavior.

---

## 3) Expected differences (display/hardware-facing)

These are expected and acceptable under hardware abstraction:
- `_2` uses TFT-centric display stack and related globals (`common.h`, `globals.cpp`, TFT object/mutex patterns).
- `_lcd` uses LGFX/LVGL runtime split (`common_lcd.h`, `runtime/common_lcd.cpp`, `ui/runtime/*`).

This part is consistent with your exception requirement.

---

## 4) Non-display mechanics that currently diverge

These are the key findings where mechanics are **not fully equivalent**.

## 4.1 ESP-NOW runtime architecture differs significantly
### `_2` model
- Split pipeline with callback/task internals and explicit handlers:
  - `espnow_callbacks.*`
  - `espnow_tasks.*`
  - `espnow_message_handlers.cpp`
  - `espnow_tasks_internal.h`
  - `espnow/handlers/*.cpp`

### `_lcd` model
- Consolidated runtime:
  - `espnow_runtime.*`
  - no `espnow_callbacks.*` / `espnow_tasks.*` equivalents

### Impact
Mechanics are similar at a high level, but internal behavior is not identical by construction. This increases drift risk and makes parity harder to prove over time.

---

## 4.2 Connection handler capability gap (`rx_connection_handler`)
`_2` has additional non-display mechanics absent in `_lcd`:
- Deferred `PEER_REGISTERED` latching/flush with TTL
- Duplicate suppression for peer-registration event posting
- Type-catalog retry policy (versions + battery/inverter/interface bounded retries)
- LED sync bounded retry state machine
- `on_config_update_sent()` hook for stale grace handling

`_lcd` version is simpler and lacks these mechanisms.

### Impact
Potential behavioral differences during noisy link transitions/reconnect windows/config sync windows.

---

## 4.3 Settings sync behavior diverges
- `_2` (`espnow_settings_sync.cpp`) performs:
  - category-aware ACK handling
  - granular re-fetch requests after success/failure
  - component apply tracker integration (`ComponentApplyTracker` path)
- `_lcd` (`espnow_settings_sync.cpp/.h`) currently stores ACK snapshot state but does not implement equivalent granular re-fetch orchestration.

### Impact
`_lcd` can lag transmitter-side truth after setting updates compared with `_2` behavior.

---

## 4.4 Battery data-store model diverges (non-display)
- `_2` still carries legacy global telemetry compatibility fields in `battery_data_store.*`.
- `_lcd` uses a cleaner snapshot-first model and includes component config section metadata in snapshot.

### Impact
Mechanics are not symmetrical; APIs consuming battery data can behave differently (especially legacy callers).

---

## 4.5 WiFi/network bootstrap policy differs
- `_2` startup path is STA/static-IP oriented (`src/config/wifi_setup.cpp`) with no AP fallback in that path.
- `_lcd` has explicit AP fallback + mDNS startup (`WiFiSetup::start_ap_fallback`, AP/APSTA acceptance in webserver init).

### Impact
Boot networking behavior differs materially despite being non-display mechanics.

---

## 4.6 Dashboard/telemetry policy differences
Notable differences in shared web mechanics:
- `_2` still exposes test-mode globals in telemetry API path.
- `_lcd` hard-removes runtime test-mode usage (always live in telemetry endpoints).
- Receiver IP mode labeling on dashboard is dynamic in `_lcd` (`ReceiverNetworkConfig::useStaticIP()`), static-assumed in `_2` in at least one dashboard path.

### Impact
User-visible behavior and API semantics diverge even though pages are mostly shared.

---

## 4.7 Receiver config library is close but not byte-identical
`lib/receiver_config/receiver_config_manager.*` differs slightly between projects (mostly comments/style/minor includes), but this is still a separate maintenance surface and can drift.

---

## 4.8 MAC ABI mismatch issue was addressed
During this investigation/fix cycle, `_lcd` now matches `_2` export shape for transmitter MAC compatibility:
- `ESPNow::transmitter_mac` is now a fixed `uint8_t[6]` form in `_lcd` (with `peer_mac` alias pointer retained).

This resolved a prior type-shape mismatch risk.

---

## 5) Additional noteworthy risk (outside display abstraction)

In `_2`, `src/globals.cpp` contains hardcoded WiFi credentials in `Config` constants.  
This is not a display issue and should be removed for parity, security, and portability.

---

## 6) Recommendations

## Priority A — Unify core mechanics by extracting shared receiver-core modules
Move non-display mechanics into shared `esp32common` modules and consume from both projects:
1. ESP-NOW RX runtime pipeline (queue/callback/router/task orchestration)
2. Connection handler state policies (deferred peer events, retry windows)
3. Settings sync orchestration (ACK + re-fetch + component apply tracker hooks)
4. Battery snapshot/cache API contract
5. WiFi bootstrap policy abstraction (STA-only vs AP fallback as configurable policy)

This is the strongest way to enforce “same mechanics, different display.”

## Priority B — Immediate parity ports from `_2` to `_lcd`
If not extracting immediately, port these `_2` mechanics to `_lcd` now:
- `rx_connection_handler` deferred peer-registration and retry policies
- Type catalog retry orchestration
- `on_config_update_sent()` integration
- Settings ACK-triggered granular refresh behavior
- Component apply tracker parity

## Priority C — Policy alignment decisions
Decide and enforce one policy set for both projects:
- Test mode enabled in both, or removed from both (recommended: capability-gated and off by default)
- AP fallback behavior enabled in both, or capability-flagged
- Receiver IP mode reporting should be dynamic in both dashboard/API paths

## Priority D — Security/operability cleanup
- Remove hardcoded WiFi credentials from `_2` globals and rely on NVS config path only.

---

## 7) ESP-NOW architecture robustness analysis

> **Updated 2026-04-16** — in response to question: *which version is more robust, and why do they diverge so much given `_lcd` was supposed to be a port of `_2`?*

---

### 7.1 Verdict: `_2` is more robust in connection mechanics; `_lcd` has better code hygiene

Neither codebase is unconditionally superior. They are strong in different dimensions:

| Dimension | `_2` | `_lcd` |
|-----------|------|--------|
| **Connection resilience** | ✅ Stronger — deferred peer, catalog retry, LED sync bounded retry | ⚠️ Weaker — single-shot init, no retry orchestration |
| **Init completeness** | ✅ Full (config sections × 4, version announce, power profile, LED, catalog) | ⚠️ Partial (power profile + 2 specific message types + LED, no version announce, no catalog) |
| **Settings sync** | ✅ Granular re-fetch after ACK/change, `BatterySettingsCache` version tracking | ⚠️ Snapshot stored but no re-fetch; receiver can lag TX state |
| **Code hygiene** | ⚠️ Legacy globals, verbose style | ✅ Mutex-protected snapshot, `bool` returns, namespace isolation, no legacy globals |
| **Type safety / API clarity** | ⚠️ Free functions, no return status on sync handlers | ✅ Namespaced, handlers return `bool`, `Snapshot` struct for consumers |
| **Discovery lifecycle** | ⚠️ No explicit `EspnowDiscovery` suspend/resume in state callbacks | ✅ `EspnowDiscovery::instance().suspend()` on CONNECTED, `resume()` on IDLE/CONNECTING |
| **Peer cleanup on disconnect** | ✅ Explicit `EspnowPeerManager::remove_peer()` on connection loss | ⚠️ No explicit peer cleanup on disconnect |

---

### 7.2 `_2` connection resilience mechanisms absent in `_lcd`

#### a) Deferred `PEER_REGISTERED` with TTL (`rx_connection_handler`)
`_2` handles a race condition where `PEER_REGISTERED` arrives before the state machine is in `CONNECTING` state. It latches the event with a TTL (`DEFERRED_PEER_TTL_MS`) and flushes it when `CONNECTING` is next entered. Stale events are dropped after TTL expiry.

`_lcd` silently drops any `PEER_REGISTERED` that arrives outside `CONNECTING`, meaning a fast transmitter can permanently miss triggering the state transition without a retry.

#### b) Type-catalog retry engine
`_2` `tick()` runs a per-category bounded retry loop for:
- Type catalog versions request (until acknowledged)
- Battery types catalog (until cache populated or `CATALOG_MAX_RETRIES` hit)
- Inverter types catalog (same)
- Inverter interface catalog (same)

`_lcd` `tick()` only retries `REQUEST_DATA` for power-profile stream. No catalog retry exists.

#### c) LED sync bounded retry state machine
`_2` sends LED state requests in a bounded retry loop (`LED_SYNC_MAX_ATTEMPTS`, `LED_SYNC_RETRY_INTERVAL_MS`) and sets `led_sync_pending_ = false` only when a response arrives or retries are exhausted.

`_lcd` sends one LED state request fire-and-forget with no retry state tracking. `on_led_state_received()` is a no-op log.

#### d) `send_initialization_requests` completeness gap
`_2` sends on connection establishment:
1. `config_section_request_t` for MQTT, NETWORK, METADATA, BATTERY sections
2. `request_data_t` for `subtype_power_profile`
3. `version_announce_t` with firmware/protocol/device/build metadata
4. LED state request
5. Type catalog versions request
6. Battery/inverter/interface catalogs (if cache empty)

`_lcd` sends on connection establishment:
1. `request_data_t` for `subtype_power_profile`
2. `network_config_request_t`
3. `mqtt_config_request_t`
4. LED state request (single shot)

**Missing in `_lcd`:** version announcement to transmitter, metadata/battery config sections, catalog population.

#### e) `on_config_update_sent()` stale grace hook
`_2` has `on_config_update_sent()` which tells the state machine to extend its stale-detection grace window during config sync windows. `_lcd` has no equivalent; the state machine can falsely time out a connection during a settings sync operation.

---

### 7.3 `_lcd` improvements absent in `_2`

#### a) Settings sync is architecturally cleaner
`_lcd` `EspnowSettingsSync::Snapshot` is mutex-protected, returned to callers by value via `read_snapshot()`, and handlers return `bool` for error propagation. `_2` uses free functions with `void` return and a separate `BatterySettingsCache` coupling.

#### b) `EspnowDiscovery` lifecycle managed in connection callbacks
`_lcd` suspends discovery on `CONNECTED` and resumes on `IDLE`/`CONNECTING`. `_2` does not call `EspnowDiscovery::instance().suspend()`/`resume()` in state callbacks, so discovery can interfere with an established connection.

#### c) No peer cleanup regression risk
`_lcd` does not call `EspnowPeerManager::remove_peer()` on disconnect — this avoids the double-remove path that `_2` guards against (`is_peer_registered` check before remove). However `_2`'s explicit cleanup is the more correct behavior and just needs the guard.

---

### 7.4 Why do they diverge so much?

The divergence is explained by **one root cause**: `_lcd` was not a live port from `_2`. It was written from an earlier, simpler snapshot of `_2` (or written independently to match its high-level API surface), while `_2` continued receiving substantial production hardening that was never backported.

Evidence from the code:

| Indicator | Interpretation |
|-----------|----------------|
| `_2` `rx_connection_handler.cpp` is **545 lines**, `_lcd` is **225 lines** | `_2` accumulated 300+ lines of resilience mechanisms post-divergence |
| `_lcd` uses `send_network_config_request_message` / `send_mqtt_config_request_message` (two new message types) where `_2` uses `config_section_t` with a range of sections | Different design decisions were made at the time of authoring each — not a simple port |
| `_lcd` `espnow_settings_sync` uses a mutex-protected `Snapshot` struct and namespace — `_2` uses free functions with `BatterySettingsCache` coupling | `_lcd` author made an independent design improvement while writing; `_2` author did not refactor back |
| `_2` has `TypeCatalogCache`, `ComponentApplyTracker`, `BatterySettingsCache`, `espnow_tasks_internal.h` — `_lcd` has none of these | These are `_2` features added after divergence; `_lcd` has no equivalent |
| `_lcd` ESP-NOW runtime is `espnow_runtime.cpp` (consolidated); `_2` has `espnow_callbacks.*` + `espnow_tasks.*` + `espnow_message_handlers.cpp` + `espnow_tasks_internal.h` | Separate structural decisions made at different times |

**In short:** both codebases have evolved independently since `_lcd` was started. `_2` received production resilience improvements. `_lcd` received code quality improvements. Neither set of changes was mirrored to the other project.

---

### 7.5 Recommended resolution path (updated)

The dual-improvement situation makes a simple "port `_2` to `_lcd`" inadequate. The correct path is:

1. **Extract the resilience mechanisms from `_2` into `esp32common`** as shared, reusable policy classes:
   - `DeferredPeerRegistrationPolicy` (TTL latch/flush logic)
   - `CatalogRetryPolicy` (per-category bounded retry with cache integration)
   - `LedSyncRetryPolicy` (bounded retry + acknowledgement)
   - `InitializationRequestSuite` (the full init sequence as a shared orchestrator)

2. **Adopt `_lcd`'s cleaner code patterns as the canonical style** for both:
   - Mutex-protected snapshot for settings sync
   - `bool` return from handlers
   - Namespaced sync API
   - Explicit `EspnowDiscovery` lifecycle in connection state callbacks

3. **Then refactor both `rx_connection_handler.cpp` implementations** to consume the shared policies, leaving only device-specific glue in each project.

This approach resolves the divergence without choosing one project's code wholesale, and produces a shared layer that cannot silently re-diverge.

---

## 8) Implementation — 7.5 executed (2026-04-16)

All three steps of section 7.5 were carried out in the same session. Both projects build cleanly after these changes.

### 8.1 New shared policy headers in `esp32common`

Three header-only policy classes were created in `esp32common/espnow_common_utils/` with stable public stubs in `include/esp32common/espnow/`:

| File | Responsibility |
|------|---------------|
| `rx_deferred_peer_policy.h` | TTL-based latch of PEER_REGISTERED when not yet CONNECTING. Includes flush, deduplication, and stale-event expiry. |
| `rx_led_sync_policy.h` | Bounded retry for LED state sync (max 3 attempts, 500ms interval). `arm()`, `tick()`, `mark_sent()`, `on_response_received()`, `reset()`. |
| `rx_catalog_retry_policy.h` | Per-category bounded catalog retry using raw function pointers. `is_due()`, `tick_item()`, `mark_ticked()`, `on_versions_received()`, `reset()`. Per-item retry counters are public for zero-overhead access. |

All three are header-only (no .cpp), no dynamic memory, no FreeRTOS dependency, compatible with embedded targets.

### 8.2 Both `rx_connection_handler` files refactored

Both `espnowreceiver_2/src/espnow/rx_connection_handler.h/.cpp` and `espnowreceiver_LCD/src/espnow/rx_connection_handler.h/.cpp` were rewritten to be structurally identical except for `#include` paths:

- Replaced all inline policy state (`deferred_peer_mac_`, `led_sync_pending_`, `catalog_versions_received_`, `versions_retry_count_`, etc.) with three policy member instances: `deferred_peer_`, `led_sync_`, `catalog_retry_`.
- Both now declare the same public API: `on_type_catalog_versions_received()`, `on_config_update_sent()`, `on_led_state_received()` (now correctly uses `led_sync_.on_response_received()`).
- `_lcd` `send_initialization_requests()` enriched to match `_2`:
  - Added full config section requests (MQTT, NETWORK, METADATA, BATTERY via `config_section_request_t`)
  - Added `version_announce_t` firmware/protocol/device announcement
  - Added catalog version check + selective battery/inverter/interface catalog population
- `_lcd` `tick()` now includes the full catalog retry engine (matching `_2`)
- Both now call `EspnowDiscovery::instance().suspend()` on CONNECTED and `resume()` on IDLE/CONNECTING
- Both now call `EspnowPeerManager::remove_peer()` on connection loss (with `is_peer_registered` guard)

### 8.3 Settings sync aligned

**`_2` (`espnow_settings_sync`):**
- Created `espnow_settings_sync.h` with `EspnowSettingsSync` namespace and `Snapshot` struct matching the `_lcd` API surface.
- Rewrote `espnow_settings_sync.cpp` in `EspnowSettingsSync` namespace with mutex-protected `Snapshot` + `read_snapshot()`.
- Kept `_2`-specific granular re-fetch (`request_category_refresh()`) and `BatterySettingsCache`/`ComponentApplyTracker` side effects.
- All handlers now return `bool`.
- Updated `espnow_tasks_internal.h` and call sites in `espnow_tasks.cpp` to use `EspnowSettingsSync::` namespace.

**`_lcd` (`espnow_settings_sync`):**
- Added `request_category_refresh()` with the same category-switch logic as `_2`.
- `handle_settings_update_ack()` now triggers granular re-fetch after success/failure.
- `handle_settings_changed()` now calls `BatterySettingsCache::instance().mark_updated()`.
- `handle_component_apply_ack()` now calls `ComponentApplyTracker::on_ack()`.

### 8.4 Post-implementation parity status

| Dimension | `_2` | `_lcd` | Status |
|-----------|------|--------|--------|
| Deferred PEER_REGISTERED | ✅ | ✅ | **Aligned** |
| Catalog retry engine | ✅ | ✅ | **Aligned** |
| LED sync bounded retry | ✅ | ✅ | **Aligned** |
| Full init sequence | ✅ | ✅ | **Aligned** |
| `on_config_update_sent()` | ✅ | ✅ | **Aligned** |
| `on_type_catalog_versions_received()` | ✅ | ✅ | **Aligned** |
| EspnowDiscovery lifecycle | ✅ | ✅ | **Aligned** |
| Peer cleanup on disconnect | ✅ | ✅ | **Aligned** |
| Settings sync namespace/Snapshot | ✅ | ✅ | **Aligned** |
| Settings sync granular re-fetch | ✅ | ✅ | **Aligned** |
| BatterySettingsCache version track | ✅ | ✅ | **Aligned** |
| ComponentApplyTracker forwarding | ✅ | ✅ | **Aligned** |
| Build verification | ✅ SUCCESS | ✅ SUCCESS | **Both clean** |

### 8.5 Remaining items (not addressed in this session)

- Section **4.5** (WiFi bootstrap policy — AP fallback absent in `_2`) — deferred; requires policy decision.
- Section **4.6** (test-mode globals, hardcoded receiver IP mode in `_2` dashboard) — deferred; requires policy decision.
- Section **5** (hardcoded WiFi credentials in `_2/src/globals.cpp`) — deferred; security cleanup.

---

## 9) Practical parity status (post 7.5 execution)

If physical display is excluded, parity is now materially improved in ESP-NOW connection/settings mechanics due to section 7.5 implementation.

- ESP-NOW resilience and settings-sync behavior are now aligned at the policy level.
- Remaining non-display divergence is concentrated in **webserver policy details**, **network bootstrap policy**, and **legacy test/security decisions**.

**Bottom line:** architecture parity is now strong in ESP-NOW mechanics, but webserver consolidation into shared modules is still incomplete.

---

## 10) Webserver deep analysis (`_2` vs `_lcd`)

> **Updated 2026-04-16** — requested deep review of whether the webserver code should be fully common.

### 10.1 Quantitative result

Within the active webserver trees:

- `_2`: `lib/webserver/**`
- `_lcd`: `lib/webserver_lcd/**`

comparison shows:

- Common relative-path files: **170**
- Byte-identical (SHA-256): **165**
- Different content: **5**
- `_lcd`-only extras: **8**
- `_2`-only extras: **0**

This means the webserver implementations are already **~97.1% identical** on shared paths (`165 / 170`).

### 10.2 The 4 material differences on shared paths

1. `webserver.cpp`
  - `_2` starts only when `WiFi.status() == WL_CONNECTED`.
  - `_lcd` accepts `WL_CONNECTED` **or** AP/APSTA mode, so webserver is available in AP fallback setup mode.
  - `_2` retains legacy test-mode extern declarations; `_lcd` intentionally has none.

2. `api/api_telemetry_handlers.cpp`
  - `_2` supports simulated telemetry path (`test_mode_enabled`, `g_test_soc`, etc.) for `/api/monitor`.
  - `_lcd` always reports live telemetry.
  - `_2` reports receiver network mode as static in dashboard data path (`receiver["is_static"] = true`), while `_lcd` uses dynamic config (`ReceiverNetworkConfig::useStaticIP()`).

3. `pages/dashboard_page.cpp`
  - `_2` hardcodes receiver IP mode text as static (`" (S)"`).
  - `_lcd` derives receiver IP mode dynamically from config.
  - `_lcd` also updates transmitter IP/mode display from cached identity even when ethernet link is down; `_2` updates those fields only when connected.

4. `pages/dashboard_page_script.cpp`
  - `_lcd` script updates receiver IP mode badge dynamically from `/api/dashboard_data` (`rx.is_static`).
  - `_2` script does not dynamically update receiver IP mode.
  - `_lcd` script keeps transmitter identity fields (IP/mode/version/MAC) refreshed independent of live link state; `_2` ties most of those updates to connected state.

### 10.3 `_lcd`-only extra webserver files (8)

The additional files in `_lcd` are:

- `webserver_lcd.h`, `webserver_lcd.cpp` (compatibility wrapper)
- `api/api_network.h`, `api/api_network.cpp`
- `api/api_utils.h`
- `pages/network_page.h`, `pages/network_page.cpp`
- `library.json`

Findings:

- `webserver_lcd.h/.cpp` is a thin compatibility shim and is used by LCD `main.cpp` (`WebserverLcd::init()` forwarding to `init_webserver()`).
- `api_network.*` is now explicitly wired in `_lcd` active registration flow via `ApiNetwork::register_handlers(server)` (GET+POST `/api/v1/network`).
- `pages/network_page.*` is now explicitly wired in `_lcd` via `NetworkPage::register_handler(server)` at `/config` (root redirect is intentionally not registered to avoid conflict with dashboard `/`).
- `api_utils.h` is helper glue for `api_network.*` and is now runtime-relevant due the explicit registration.
- `library.json` is metadata only.

### 10.4 Shared code already extracted to `esp32common`

Both webservers already consume common modules from `esp32common/webserver_common_utils`:

- `http_json_utils`
- `http_sse_utils`
- `ota_auth_utils`
- `ota_session_utils`
- `spec_page_layout`

This confirms the intended direction is correct, but extraction is still partial.

### 10.5 Conclusion: should webserver be common?

**Yes.** Based on current evidence, the webserver is sufficiently similar to be treated as shared core code with small policy hooks.

The current split (`lib/webserver` vs `lib/webserver_lcd`) mostly duplicates code and increases drift risk for no runtime benefit.

### 10.6 Recommended follow-up (webserver)

1. Create a shared receiver webserver core in `esp32common` (pages/API/utils registration + handlers).
2. Keep only a tiny per-project adapter layer for:
  - startup policy (`STA-only` vs `AP/APSTA-allowed`),
  - telemetry mode policy (`live-only` vs `test-capable`),
  - entrypoint naming compatibility (`init_webserver()` vs wrapper namespace).
3. Remove or explicitly wire `api_network.*` / `network_page.*` in `_lcd` to avoid dead-path confusion.
4. Standardize dashboard/network semantics across both projects:
  - dynamic receiver IP mode,
  - consistent transmitter identity refresh behavior,
  - explicit policy decision on test-mode exposure.

### 10.7 Execution status of 10.6 (implemented now)

Implemented in this session:

1. **Adapter policy alignment (partial execution of 10.6.2 + 10.6.4)**
  - `_2` `webserver.cpp` now accepts STA **or** AP/APSTA readiness (aligned with `_lcd`).
  - `_2` dashboard and telemetry behavior aligned to dynamic network semantics:
    - `/api/dashboard_data` now reports receiver `is_static` via `ReceiverNetworkConfig::useStaticIP()`.
    - `pages/dashboard_page.cpp` now shows dynamic receiver IP mode `(S)/(D)`.
    - `pages/dashboard_page_script.cpp` now updates receiver IP mode dynamically and refreshes transmitter identity fields independent of live ethernet status.

2. **Policy decision on telemetry mode (10.6.4, explicit)**
  - `/api/monitor` in `_2` was switched to **live-only** telemetry path (matching `_lcd`).
  - Legacy test-mode globals were removed from `_2` webserver telemetry path.

3. **Dead-path cleanup by explicit wiring (10.6.3)**
  - `_lcd` now explicitly registers:
    - `ApiNetwork::register_handlers(server)` for `/api/v1/network` (GET/POST)
    - `NetworkPage::register_handler(server)` for `/config`
  - `NetworkPage::register_root(server)` intentionally remains unused to avoid conflict with dashboard root route.

4. **Verification**
  - `_2` build: **SUCCESS**
  - `_lcd` build: **SUCCESS**

Remaining from 10.6:

- **10.6.1 full shared webserver core extraction into `esp32common`** is not yet completed. Current state remains dual trees with high overlap and aligned policy behavior, but not a single shared-core implementation yet.
