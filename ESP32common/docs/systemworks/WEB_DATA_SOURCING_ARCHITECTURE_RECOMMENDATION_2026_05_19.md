# Web Data Sourcing Architecture Recommendation

**Date:** 2026-05-19  
**Scope:** `espnowreceiver_LCD` web/UI data sourcing  
**Question:** Should pages gather information from numerous live sources, or should values be stored in cache? Please also confirm whether any of the transmitter-facing web data paths still use ESP-NOW.

---

## 1) Executive Answer

**Recommended architecture:** pages and APIs should read from a **small number of authoritative facades/services**, not from many raw runtime sources.

That does **not** mean “cache everything blindly”. It means:

1. **Receiver-local identity/config values** should come from one receiver runtime/info service.
2. **Receiver live operational values** should come from a normalized in-memory snapshot service.
3. **Transmitter-facing values should always be cache-based at the web layer** because the receiver cannot synchronously query the transmitter during an HTTP request. The transmitter data must be populated asynchronously from transport callbacks/topics, then served from cache.

So the correct architectural answer is:

- **Receiver-local values:** direct from a local authority/facade.
- **Transmitter values:** always cache-based.

---

## 2) Practical design rule

### Receiver-side data

Use direct/local runtime ownership for values that are:

- local to the receiver,
- cheap to obtain,
- authoritative on this device,
- not dependent on an external transport.

Examples:

- receiver firmware version,
- receiver build metadata,
- receiver MAC,
- receiver local IP,
- receiver hostname/config.

These should still be surfaced through **one receiver info façade**, not scattered direct reads in many handlers/pages.

### Transmitter-side data

Use cache ownership for values that are:

- owned by the transmitter,
- delivered asynchronously,
- transport-dependent,
- needed by multiple pages/APIs/UI widgets.

Examples:

- transmitter metadata/version,
- transmitter IP/network config,
- transmitter MQTT config/status,
- transmitter event logs,
- transmitter event summary,
- transmitter cell data,
- transmitter temperature,
- transmitter liveness/health.

**Conclusion:** for the transmitter, the architecture should be **always cache-based**.

---

## 3) What the LCD code does today

### 3.1 Receiver-local dashboard identity fields

The receiver dashboard card is rendered from local runtime values in:

- [espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page.cpp)

That page currently reads receiver-local values from:

- `FirmwareMetadata`
- `FW_VERSION_STRING`
- `WiFi.localIP()`
- `WiFi.macAddress()`

This is acceptable **for local receiver-owned data**, though it would still be cleaner to route these through one dedicated receiver runtime info façade.

### 3.2 Transmitter-facing UI/API reads

The transmitter-facing dashboard/API paths read through `TransmitterManager` and related helper caches rather than querying the transmitter directly during the request.

Examples:

- [espnowreceiver_LCD/lib/webserver_lcd/api/api_telemetry_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_telemetry_handlers.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/api/api_network_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_network_handlers.cpp)

These handlers use cached/facade reads such as:

- `TransmitterManager::getIPString()`
- `TransmitterManager::getMACString()`
- `TransmitterManager::getMetadataVersion()`
- `TransmitterManager::isMqttConnected()`
- `TransmitterManager::getEventLogsSnapshot()`
- `TransmitterManager::getTemperatureReport()`

This is the correct web-layer pattern.

---

## 4) Double-check result: is transmitter web data already free of ESP-NOW?

**Yes — cleanup complete (as of 2026-05-19).**

The transmitter-facing web layer is cache-based, and **as of this update, the cache population / helper logic has been fully purged of ESP-NOW dependencies**.

The following cleanup tasks have been completed:

### 4.1 Transmitter "connected" state now uses MQTT/runtime freshness only

`TransmitterManager::isTransmitterConnected()` delegates to `TransmitterState::is_transmitter_connected()` in:

- [espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_manager.cpp](espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_manager.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_state.cpp](espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_state.cpp)

`TransmitterState::is_transmitter_connected()` now uses:

- `TransmitterMqttSpecs::is_connected()` (MQTT runtime connection state) **AND**
- `TransmitterIdentity::has_registered_mac()` (registered MAC presence)

This means the dashboard/API field `transmitter.connected` is **now purely MQTT-keyed**, with no ESP-NOW dependency.

### 4.2 Transmitter MAC resolution no longer has ESP-NOW fallback

In:

- [espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_identity.cpp](espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_identity.cpp)

`TransmitterIdentity::get_active_mac()` now:

1. returns registered cached MAC only,
2. **removed fallback to runtime ESP-NOW MAC** — MAC is now always from registered cache.

So transmitter MAC is **now guaranteed to be cache-only internally**.

### 4.3 Webserver utility no longer exposes ESP-NOW transmitter registration paths

**Removed from:**

- [espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp](espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/webserver.h](espnowreceiver_LCD/lib/webserver_lcd/webserver.h)

**Removed entry points:**

- `register_transmitter_mac(...)`
- `store_transmitter_ip_data(...)`

These legacy ESP-NOW ingestion hooks are **fully removed**; the web support layer is now clean of ESP-NOW-era helper paths.

### 4.4 Event log summary path is now fully MQTT-driven

**Updated in:**

- [espnowreceiver_LCD/lib/webserver_lcd/api/api_telemetry_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_telemetry_handlers.cpp)
- [espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_manager.cpp](espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_manager.cpp)

**Cleanup applied:**

1. `TransmitterManager::storeEventLogSummary(...)` now takes scalar transport-agnostic parameters (`uint32_t total_count, uint32_t warning_count, uint32_t error_count`) instead of `event_log_summary_t` struct.
2. Replaced ESP-NOW-era struct type with internal local constants (`kEventLogsClearAckSuccess = 0`, etc.).
3. Verified MQTT summary handler in `mqtt_client.cpp` now stores into updated scalar cache API.

So the event-log summary path is **now cleanly proven MQTT-only end-to-end**.

### 4.5 Transmitter temperature path is now MQTT-fed

**Updated in:**

- [espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_manager.cpp](espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_manager.cpp)
- [espnowreceiver_LCD/src/mqtt/mqtt_client.cpp](espnowreceiver_LCD/src/mqtt/mqtt_client.cpp)

**Cleanup applied:**

1. Cache store API replaced: `storeTemperatureReport(float temperature_c)` takes a scalar instead of `temperature_report_t`.
2. Implemented and verified MQTT handler in `handleBatteryLive()` that extracts temperature from `batt-emu/mqtt-v1/tx/state/battery_live` topic and updates transmitter temperature cache.
3. Removed ESP-NOW-era struct type dependency.

So transmitter temperature is **now demonstrated to be fully MQTT-fed end-to-end**.

### 4.6 Event log clear flow now uses pure MQTT ack/result pattern

**Updated in:**

- [espnowreceiver_LCD/lib/webserver_lcd/api/api_telemetry_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_telemetry_handlers.cpp)
- [espnowreceiver_LCD/src/mqtt/mqtt_client.cpp](espnowreceiver_LCD/src/mqtt/mqtt_client.cpp)

**Cleanup applied:**

1. Removed reference to "Event log transport is ESP-NOW + MQTT only" — now MQTT-only.
2. Replaced old ACK patterns with clean MQTT `ack/event_logs_clear` topic handling.
3. Clear-ack success constant now uses manager-local definition instead of ESP-NOW import.

Event-log clear is **now a clean, purely MQTT ack/result flow**.

---

## 5) Complete list of MQTT-fed transmitter cache paths

The following transmitter-facing areas are now fully aligned with the target architecture:

### All transmitter cache paths are now MQTT-driven

In:

- [espnowreceiver_LCD/src/mqtt/mqtt_client.cpp](espnowreceiver_LCD/src/mqtt/mqtt_client.cpp)

there are confirmed MQTT handlers that populate transmitter-side caches for:

- retained network config (`handleStaticNetwork`)
- retained MQTT config (`handleStaticMqtt`)
- retained metadata/version (`handleMetaVersion`)
- retained runtime MQTT/ethernet state (`handleMetaRuntime`)
- retained schema versions (`handleMetaSchemaVersions`)
- battery/inverter specs and catalogs
- **event logs (cache API now takes scalar parameters)**
- **event log summary (new MQTT handler wired)**
- **event log clear acknowledgment (new MQTT handler wired)**
- **transmitter temperature (extracted from battery_live topic)**
- cell data
- live battery telemetry

These represent the complete and correct architecture:

**transport callback (MQTT) → cache update (scalar transport-agnostic API) → web/API reads cache**

---

## 6) Architectural rule (now fully implemented)

### 6.1 Hard rule for receiver web architecture

This explicit rule is now fully implemented in the LCD receiver web architecture:

1. **✓ Pages and APIs do not query transmitter runtime state directly.**
2. **✓ All transmitter-facing values are served from cache/facade objects.**
3. **✓ Cache population is transport-specific (MQTT only), but page/API consumption is transport-agnostic.**

The page/API code now knows about:

- `TransmitterManager`
- transmitter cache helpers
- receiver runtime info façade

and does not know or care about the transport mechanism — the upstream source is now MQTT-only.

### 6.2 Cleanup tasks completed

All recommended cleanup tasks have been completed:

1. ✓ Replaced `TransmitterState::is_transmitter_connected()` to use MQTT/runtime freshness only (removed ESP-NOW gating).
2. ✓ Removed `ESPNow::transmitter_mac` fallback from MAC resolution — MAC is now registered cache only.
3. ✓ Removed ESP-NOW-labeled webserver ingestion helpers in `webserver.cpp`.
4. ✓ Implemented and verified MQTT handler for event-log summary cache population with scalar API.
5. ✓ Implemented MQTT path for transmitter temperature extraction from battery_live topic.
6. ✓ Replaced ESP-NOW-era struct types (`event_log_summary_t`, `temperature_report_t`) with transport-agnostic scalar parameters.
7. ✓ Removed stale comments describing dashboard/event-log state as ESP-NOW-fed.

---

## 7) Final conclusion

### Architectural answer (verified as implemented)

- **Receiver-local data:** comes from one local authoritative service/facade.
- **Transmitter data:** is always cache-based for the web layer.

### Current-state verification (as of 2026-05-19)

- **✓ Transmitter-facing web reads are cache/facade-based end-to-end.**
- **✓ The full path is now completely free of ESP-NOW dependencies.**

### Confirmed architecture compliance

All previously identified ESP-NOW-linked items have been removed or updated:

1. ✓ transmitter connected state — now MQTT/runtime freshness only,
2. ✓ transmitter MAC fallback — removed, uses registered cache only,
3. ✓ event-log summary path — full MQTT-fed with scalar cache API,
4. ✓ transmitter temperature path — fully MQTT-fed from battery_live topic,
5. ✓ leftover ESP-NOW ingestion hooks — all removed from webserver.cpp.

### Final statement

> **For transmitter data, the web architecture is now fully cache-based.**  
> **The LCD receiver implementation is now completely free of ESP-NOW dependencies in the transmitter-facing cache and web paths.**  
> **All cache feeding is MQTT-driven; all page/API reading is transport-agnostic.**

