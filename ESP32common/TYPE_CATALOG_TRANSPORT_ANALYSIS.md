# Type Catalog Transport Analysis
## Battery & Inverter Type List Delivery: ESP-NOW (primary) + MQTT (optional backup)
**Date:** 2026-03-15  
**Branch:** feature/battery-emulator-migration  
**Revision:** 5 — full implementation verified (transmitter + receiver builds confirmed clean)

---

## 1. The Problem

The receiver's web UI must present a sorted, human-readable list of all available battery types (47 entries) and inverter protocol types (22 entries) so the user can select which hardware is connected. These lists are authoritative in the Battery Emulator firmware on the transmitter and must reach the receiver dynamically rather than being hard-coded.

**Transport priority:**
- **Primary: ESP-NOW** — always available, works with zero external infrastructure, direct P2P link to transmitter
- **Optional backup: MQTT** — richer delivery (retained messages survive TX offline periods), but requires a configured MQTT broker; `MqttClient::enabled_` defaults to `false`

This document analyses the current ESP-NOW implementation in detail, identifies the httpd stack-overflow crash and its fix, proposes protocol improvements, documents the full trigger chain for both transports, and covers the existing selection-feedback round-trip.

### Implementation status snapshot (as of 2026-03-15)

- ✅ ESP-NOW dynamic battery/inverter catalogs implemented (request/fragment/cache/API/UI).
- ✅ Cache-gated on-connect requests implemented.
- ✅ Catalog version request/response implemented (`msg_request_type_catalog_versions` / `msg_type_catalog_versions`).
- ✅ Receiver selective refresh checks implemented (announced vs applied versions).
- ✅ Dynamic inverter interfaces implemented end-to-end.
- ✅ MQTT backup catalogs implemented (TX retained publish + RX subscribe/parse/cache apply).
- ✅ MQTT backup applies version gate on receiver (skip stale/equal payload version).
- ✅ MQTT backup republish on component selection change implemented.
- ✅ Stack-overflow fixes applied in HTTP handlers and receiver MQTT task path.
- ✅ Monotonic firmware-derived catalog versions implemented (battery and inverter versions derived separately from firmware version number).
- ✅ Explicit per-catalog timeout/retry policy implemented on receiver (`rx_connection_handler` retry engine, 8 retries max, 3 s interval).
- ✅ Transmitter build verified clean (`pio run` **[SUCCESS]**).
- ✅ Receiver build verified clean (`pio run -e receiver_tft` **[SUCCESS]**, duplicate declaration fix applied).

---

## 2. Data Size Reality Check

### Catalog sizes (worst-case with 48-char name field)

| Catalog       | Entries | Wire bytes (ESP-NOW struct)   | JSON bytes (estimated) |
|--------------|---------|-------------------------------|------------------------|
| Battery      | 47      | 47 × 49 = **2,303 B**         | ~1,300 B               |
| Inverter     | 22      | 22 × 49 = **1,078 B**         | ~680 B                 |

Derived total (for airtime estimation only): 69 entries, 3,381 wire bytes, ~2,100 JSON bytes.

Both catalogs far exceed the ESP-NOW 250-byte per-frame limit, so fragmentation is unavoidable regardless of transport design.

### Fragmentation under current ESP-NOW scheme

ESP-NOW hard limit: 250 bytes/frame.  
Current design: 4 entries × 49 bytes + 6-byte header = **202 bytes per fragment** (well within limit).

| Catalog  | Fragments required          |
|---------|-----------------------------|
| Battery | ceil(47 / 4) = **12**       |
| Inverter | ceil(22 / 4) = **6**       |
| **Total** | **18 ESP-NOW sends**      |

18 sends at ~10 ms each ≈ **~180 ms** transmitter air time. Acceptable for a static dataset delivered once on connect.

**Can we fit 5 entries per fragment?** 5 × 49 + 6 = **251 bytes** — one byte over the 250-byte limit. So 4 entries/fragment is already the maximum packing. The current design cannot be made more compact without reducing the name-field length.

---

## 3. ESP-NOW Protocol Baseline (Retain Current 3.1 Configuration)

Per decision, we keep the existing protocol shape (Section 3.1) and do not redesign framing at this stage.

### 3.1 Current protocol fields (kept as-is)

The `type_catalog_fragment_t` struct carries:

| Field            | Size    | Purpose |
|-----------------|---------|---------|
| `sequence`       | uint16  | Identifies this transfer run; resets staging buffer on mismatch |
| `fragment_index` | uint8   | 0-based index of this fragment (used for bitmask tracking) |
| `fragment_total` | uint8   | Total fragments in this run |
| `entry_count`    | uint8   | Entries in this fragment (1–4) |
| `entries[]`      | 4 × 49B | The actual type entries |

The receiver maintains a `seen_fragment[64]` bitmask. When all bits are set the staging buffer is committed to the live cache.

### 3.2 Decision

- Keep the current battery and inverter catalogs as **separate flows** (clean separation retained).
- Keep current request/fragment protocol definitions.
- Focus improvements on trigger policy and freshness/version detection rather than payload-format churn.

---

## 4. Transport Trigger Mechanisms

### 4.1 ESP-NOW — primary path (cache-gated on-connect)

```
ESP-NOW connection established
  ↓
rx_connection_handler::handle_connection_event()
  ↓
rx_connection_handler::send_initialization_requests()
  ├── (existing init messages)
  ├── if !TypeCatalogCache::has_battery_entries()
  │      send_battery_types_request()   ──► msg_request_battery_types ──►
  └── if !TypeCatalogCache::has_inverter_entries()
         send_inverter_types_request()  ──► msg_request_inverter_types ──►
                     ↓ (transmitter)
            message_handler::handle_request_battery_types()
            message_handler::handle_request_inverter_types()
            message_handler::send_type_catalog_fragments()
                     ↓
            battery catalog: 12 fragments; inverter catalog: 6 fragments ──►
                     ↓ (receiver)
            TypeCatalogCache::handle_battery_fragment()
            TypeCatalogCache::handle_inverter_fragment()
                     ↓
            staging buffer → committed cache
                     ↓
            api_get_battery_types_handler() reads cache
```

**On-demand fallback in HTTP handler (keep for resilience):**
```
Browser GET /api/get_battery_types
  ↓
api_get_battery_types_handler()
  ├── TypeCatalogCache::copy_battery_entries() → count = 0?
  │       YES → send_battery_types_request()  (fire-and-forget fallback)
  │             return {"types":[], "loading":true}   ← browser polls again
  └── count > 0 → build and return sorted JSON
```

This gives deterministic fetch on connect while preserving lazy fallback if startup ordering causes a temporary cache miss.

### 4.2 MQTT — optional backup path (proposed) and how it is triggered

```
Transmitter trigger A (event-based): component selection changes
  handle_component_config() called (on ESP-NOW component config message)
  ↓
static_data::update_battery_specs()
static_data::update_inverter_specs()
  ↓
MqttManager::publish_battery_type_catalog()   → transmitter/BE/battery_type_catalog   (retained) ← NEW
MqttManager::publish_inverter_type_catalog()  → transmitter/BE/inverter_type_catalog  (retained) ← NEW

Transmitter trigger B (state-based): MQTT session becomes connected
  on MQTT CONNECTED state, publish both retained catalog topics once

Receiver: MqttClient connects to broker
  ↓
subscribe to transmitter/BE/battery_type_catalog and transmitter/BE/inverter_type_catalog
  ↓
broker delivers retained payloads immediately (if present)
  ↓
MqttClient::handleBatteryTypeCatalog()
MqttClient::handleInverterTypeCatalog()
  ↓
TypeCatalogCache::replace_battery_entries() / replace_inverter_entries()
  ↓
cache populated (or updated) independently per catalog
```

**Trigger difference summary:**
- ESP-NOW primary trigger: receiver-side connection event (`send_initialization_requests`) with cache gate.
- MQTT backup trigger: transmitter-side publish events (on MQTT connect and on catalog/version change) + retained replay on receiver subscribe.

### 4.3 Selection feedback — already implemented (round-trip complete)

The reverse direction (receiver → transmitter) is **already fully implemented**:

```
User selects battery type in browser
  ↓
Browser POST /api/set_battery_type
  ↓
api_set_battery_type_handler()
  ↓
ReceiverNetworkConfig::setBatteryType(type)
  ↓
send_component_type_selection(battery_type, inverter_type)
  ──► msg_component_config ──►
            handle_component_config() (transmitter)
              ↓
            update_battery_specs(type)
            update_inverter_specs(type)
              ↓
            publish_battery_specs()
            publish_inverter_specs()
            publish_static_specs()
```

`api_set_inverter_type_handler()` follows the identical pattern. **No work required here.** The full round-trip is working.

---

## 5. httpd Coupling Analysis

### 5.1 Why is `send_battery_types_request()` called inside an HTTP handler?

`api_get_battery_types_handler()` is an HTTP GET endpoint served by the ESP-IDF `httpd` server. It is called from the browser's settings page to fetch the type list. The design intent is:

1. Browser requests the list.
2. If the cache is empty (first access after boot), trigger an ESP-NOW request to the transmitter as a lazy-load fallback.
3. Return `{"loading":true}` immediately so the browser knows to poll again.
4. On subsequent requests, the cache will be populated and the full list is returned.

This design is **architecturally reasonable**. The httpd task is only the HTTP delivery layer; the data lives in a static global (`TypeCatalogCache`). The ESP-NOW trigger is a fire-and-forget FreeRTOS queue post, not a blocking call.

### 5.2 The stack overflow crash

The bug was not in the design — it was in the stack allocation pattern:

```cpp
// BEFORE (caused crash): both arrays on httpd stack
TypeCatalogCache::TypeEntry cache_entries[128];    // 128 × 49 = 6,272 bytes
TypeCatalogCache::TypeEntry response_entries[128]; // 128 × 49 = 6,272 bytes
// Total: 12,544 bytes on a ~6,144 byte httpd task stack → overflow
```

```cpp
// AFTER (fixed): heap-allocated
auto* cache_entries = new TypeCatalogCache::TypeEntry[128];  // heap
// ... (response_entries eliminated, sorted_entries also heap-allocated)
delete[] cache_entries;
```

**Status: fixed.** Both `api_get_battery_types_handler()` and `api_get_inverter_types_handler()` now heap-allocate. The handlers do not allocate any large structures on the stack.

### 5.3 Clarified position

With cache-gated on-connect request in place, HTTP-trigger is only fallback protection and not the primary trigger. This is acceptable and low risk.

If desired later, HTTP side-effects can still be removed, but this is no longer urgent once on-connect gating is implemented.

---

## 6. How Do We Know the Transmitter Data Changed?

Yes — use **versioning**, consistent with existing project patterns.

### 6.1 Recommended version model (separate catalogs)

Maintain separate monotonic versions on transmitter:

- `battery_type_catalog_version`
- `inverter_type_catalog_version`

Increment each only when that specific catalog changes (new name, removed type, reordered IDs, etc.).

### 6.2 Where to send version

Two practical options:

1. Include version in each catalog payload (ESP-NOW and MQTT).
2. Add lightweight version request/response messages (`msg_request_catalog_versions` / `msg_catalog_versions`).

For minimal disruption while retaining protocol 3.1, option 2 is cleaner:
- receiver checks versions at connect
- if version differs from cached version, request only that catalog
- if version matches, skip request

### 6.3 Receiver decision logic

```
On ESP-NOW connect:
  request catalog versions
  if battery_version_changed or no battery cache:
    send_battery_types_request()
  if inverter_version_changed or no inverter cache:
    send_inverter_types_request()
```

This keeps air-time low while guaranteeing freshness.

---

## 7. On-Demand vs On-Connect (Final Decision)

### Decision adopted

- Use **cache-gated on-connect request** as the default strategy.
- Keep battery and inverter as separate request paths.
- Keep HTTP on-demand request only as a fallback safety net.
- Do **not** add NVS/LittleFS persistence for this catalog data.

### Rationale

- Data is rarely changed.
- Re-requesting every reconnect is unnecessary traffic.
- No persistent storage complexity on receiver.
- Version checks provide freshness without heavy protocol redesign.

### Concrete gated logic

```cpp
void send_initialization_requests() {
    // ... existing init messages ...

  // request battery catalog only when needed
  if (!TypeCatalogCache::has_battery_entries() || battery_catalog_version_changed()) {
    send_battery_types_request();
    }

  // request inverter catalog only when needed
  if (!TypeCatalogCache::has_inverter_entries() || inverter_catalog_version_changed()) {
    send_inverter_types_request();
    }
}
```

Expected outcome: after initial sync, most reconnects send zero catalog fragments unless versions changed.

---

## 8. MQTT Infrastructure Audit (Backup Path)

### Transmitter (`ESPnowtransmitter2`)

| Asset | Status |
|-------|--------|
| `MqttManager` class with state machine | ✅ Exists |
| `publish_battery_specs()` | ✅ `transmitter/BE/battery_specs` retained |
| `publish_inverter_specs()` | ✅ `transmitter/BE/spec_data_2` retained |
| `publish_static_specs()` | ✅ `transmitter/BE/spec_data` retained |
| MQTT buffer size | ✅ **6,144 bytes** — enough for full ~2 KB catalog |
| `publish_battery_type_catalog()` | ✅ Implemented |
| `publish_inverter_type_catalog()` | ✅ Implemented |
| Publish trigger: on MQTT connect | ✅ Implemented |
| Publish trigger: on component selection change | ✅ Implemented |

### Receiver (`espnowreceiver_2`)

| Asset | Status |
|-------|--------|
| `MqttClient` class | ✅ `src/mqtt/mqtt_client.h/.cpp` |
| `task_mqtt_client` FreeRTOS task | ✅ `src/mqtt/mqtt_task.cpp` |
| Subscribes to `transmitter/BE/battery_specs` | ✅ Already |
| MQTT buffer size | ✅ **6,144 bytes** |
| MQTT is optional | ⚠️ `enabled_` starts `false`; requires broker IP configured |
| `handleBatteryTypeCatalog()` | ✅ Implemented |
| `handleInverterTypeCatalog()` | ✅ Implemented |
| `TypeCatalogCache::replace_battery_entries()` | ✅ Implemented |
| `TypeCatalogCache::replace_inverter_entries()` | ✅ Implemented |
| MQTT payload version gate on receiver | ✅ Implemented (skip stale/equal version) |

**MQTT is optional infrastructure.** If a user does not configure a broker IP, MQTT is disabled and the ESP-NOW path handles everything with no degradation.

---

## 9. MQTT Optional Backup — Implemented Scope

MQTT backup should stay **separate by catalog** for clarity:
- `transmitter/BE/battery_type_catalog` (retained)
- `transmitter/BE/inverter_type_catalog` (retained)

### Transmitter changes

Add two methods:

- `MqttManager::publish_battery_type_catalog()`
- `MqttManager::publish_inverter_type_catalog()`

Each payload includes `catalog_version`, then `types[]`.

```cpp
bool MqttManager::publish_battery_type_catalog() {
  DynamicJsonDocument doc(3072);
  doc["catalog_version"] = battery_type_catalog_version;
  JsonArray types = doc.createNestedArray("types");
  // ... battery entries ...
    String payload;
    serializeJson(doc, payload);
  return publish("transmitter/BE/battery_type_catalog", payload.c_str(), true /* retained */);
}
```

Publish triggers:
1. on MQTT connect (seed retained value)
2. when corresponding catalog version increments

### Receiver changes

1. Subscribe to both topics
2. Add `handleBatteryTypeCatalog()` and `handleInverterTypeCatalog()`
3. Parse `catalog_version`; update only when version is newer
4. Populate cache via dedicated per-catalog methods

### ESP-NOW interplay

ESP-NOW remains primary. MQTT updates cache opportunistically when available. No dependency inversion.

| Step | Change | Status |
|------|--------|--------|
| Add 2 MQTT catalog publishers on TX | `publish_battery_type_catalog()` + `publish_inverter_type_catalog()` | ✅ Done |
| Add 2 MQTT handlers on RX | `handleBatteryTypeCatalog()` + `handleInverterTypeCatalog()` | ✅ Done |
| Add per-catalog cache apply methods | `replace_battery_entries()` + `replace_inverter_entries()` | ✅ Done |
| Add catalog version checks | Receiver compares payload `catalog_version` vs applied version | ✅ Done |
| Republish on catalog-affecting config change | In transmitter `handle_component_config()` path | ✅ Done |

---

## 10. Comparison Matrix (Primary + Backup)

| Property | ESP-NOW Fragments (primary) | MQTT Retained (backup) |
|---|---|---|
| **Infrastructure dependency** | None — direct P2P | Requires MQTT broker |
| **Works without MQTT** | ✅ Always | ✅ ESP-NOW path unaffected |
| **Works if TX is offline** | ❌ No | ✅ Broker retains last catalog |
| **Works without broker** | ✅ | ❌ Silent — ESP-NOW fallback activates |
| **Payload per transfer** | battery: 12 frags, inverter: 6 frags | 2 retained topics (battery/inverter) |
| **Protocol complexity** | Existing and proven | Low, topic-based |
| **Retained delivery** | ❌ Stateless — request each connect | ✅ Broker re-delivers on subscribe |
| **Catalog refresh after TX update** | Version check on connect, request only changed catalog | Publish when version increments |
| **Latency to first catalog** | ~200 ms worst case | ~0 ms if retained copy exists |
| **Stack-safe on httpd** | ✅ (heap-alloc fix applied) | ✅ Not applicable |
| **ESP-NOW protocol IDs used** | 4 new message type IDs | 0 |
| **Implementation status** | ✅ Implemented | 🔶 Optional enhancement |

---

## 11. Recommendations (Priority Order)

### Immediate (no new code required)

| # | Action | File | Risk |
|---|--------|------|------|
| 1 | ✅ Already done — heap-allocate in `api_get_battery_types_handler()` and `api_get_inverter_types_handler()` | `api_type_selection_handlers.cpp` | Fixed |
| 2 | ✅ Implemented cache-gated on-connect requests for both catalogs (battery/inverter separately) | `rx_connection_handler.cpp` | Low |
| 3 | ✅ Keep HTTP fallback trigger for resilience during startup ordering races | `api_type_selection_handlers.cpp` | Low |

### Short-term (freshness/versioning)

| # | Action | Effort |
|---|--------|--------|
| 4 | ✅ Separate catalog versions now derive from firmware version (monotonic across firmware upgrades) | - |
| 5 | ✅ Implemented version exchange/check at connect; request only changed catalog | - |
| 6 | ✅ Implemented explicit timeout fallback/retry per catalog request path | - |

### Optional (MQTT path, for broker-equipped installations)

| # | Action | Effort |
|---|--------|--------|
| 7 | ✅ Implemented `publish_battery_type_catalog()` + `publish_inverter_type_catalog()` on TX | - |
| 8 | ✅ Implemented separate MQTT handlers on RX | - |
| 9 | ✅ `catalog_version` in MQTT payload and update only on newer version | - |
| 10 | ✅ ESP-NOW remains primary when MQTT exists | - |

---

## 12. Summary

**ESP-NOW remains primary**, using **cache-gated on-connect requests** for both battery and inverter catalogs separately.

**Catalog freshness should be handled by versioning** (separate battery and inverter catalog versions), not by persistent receiver storage.

**No receiver NVS/LittleFS storage is required** for this dataset per current design decision.

**MQTT remains optional backup**, triggered by transmitter publish events (on MQTT connect and on version change), with retained replay to receiver subscribers.

**Protocol configuration 3.1 is retained as-is** and does not need immediate structural changes.

**Current state:** all planned transport-path implementation items in this document are now integrated and **both transmitter and receiver builds have been verified clean** (2026-03-15). Remaining work is operational — runtime flash/monitor validation of catalog retry behaviour and optional tuning of retry intervals/caps if field testing reveals timing issues.
