# Inverter Specs Source-of-Truth Review (2026-03-24)

## Implementation Progress (2026-03-24)

The following implementation steps from this review have now been applied in `espnowreceiver_2`:

- ✅ **Step 1 completed**
  - Removed hardcoded inverter protocol lookup table from
    `lib/webserver/pages/inverter_specs_display_page.cpp`.
  - Removed receiver-local fallback (`ReceiverNetworkConfig::getInverterType()`) from inverter specs page handler.
  - Handler now treats missing protocol string as explicit `"Unknown"` rather than deriving a local authoritative name.

- ✅ **Step 2 completed**
  - Added `id="inverterProtocolValue"` to protocol value element in
    `lib/webserver/pages/inverter_specs_display_page_content.cpp`.
  - Added catalog-based protocol label resolution in
    `lib/webserver/pages/inverter_specs_display_page_script.cpp` via
    `CatalogLoader.loadCatalogLabel(...)` for `/api/get_inverter_types` + `/api/get_selected_types`.
  - Protocol label replacement is constrained to unknown state (`replaceIfCurrentIn: ['Unknown']`) so transmitter-provided non-unknown protocol text remains primary.

### Remaining recommended work

- ✅ **Step 3 completed**
  - Updated transmitter inverter spec serialization in
    `ESPnowtransmitter2/espnowtransmitter2/src/datalayer/static_data.cpp`
    and `.../static_data.h` to include:
    - `inverter_type_id` (canonical numeric identifier)
    - `inverter_protocol_name` (canonical protocol name)
    - `spec_schema` and `schema_version` markers
    - legacy `inverter_protocol` retained for compatibility.
  - Increased MQTT publish buffer for `spec_data_2` in
    `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp`
    to safely accommodate the expanded payload.

- ✅ **Step 4 completed (targeted runtime checks + rendering path hardening)**
  - Receiver inverter page now prefers canonical
    `inverter_protocol_name`, then falls back to legacy `inverter_protocol`:
    `espnowreceiver_2/lib/webserver/pages/inverter_specs_display_page.cpp`.
  - Added hidden transmitter-provided `inverter_type_id` element and script
    logic to resolve protocol label directly from catalog by transmitted ID
    before selected-type fallback:
    - `espnowreceiver_2/lib/webserver/pages/inverter_specs_display_page_content.cpp`
    - `espnowreceiver_2/lib/webserver/pages/inverter_specs_display_page_script.cpp`
  - Added MQTT-side validation warnings for missing identity/schema fields in
    `spec_data_2`:
    `espnowreceiver_2/src/mqtt/mqtt_client.cpp`.

## Scope
Requested review of source-of-truth consistency between:

- `espnowreceiver_2/lib/webserver/pages/inverter_specs_display_page.cpp`
- `espnowreceiver_2/lib/webserver/pages/battery_specs_display_page.cpp`

and related page modules.

## Files Reviewed

- `espnowreceiver_2/lib/webserver/pages/inverter_specs_display_page.cpp`
- `espnowreceiver_2/lib/webserver/pages/inverter_specs_display_page_content.cpp`
- `espnowreceiver_2/lib/webserver/pages/inverter_specs_display_page_script.cpp`
- `espnowreceiver_2/lib/webserver/pages/battery_specs_display_page.cpp`
- `espnowreceiver_2/lib/webserver/pages/battery_specs_display_page_content.cpp`
- `espnowreceiver_2/lib/webserver/pages/battery_specs_display_page_script.cpp`

## Findings

## 1) Inverter page contains an embedded hardcoded protocol catalog
`inverter_specs_display_page.cpp` includes `get_inverter_protocol_name()` with a large static ID→name map.

**Impact:**
- Duplicates catalog knowledge in page code.
- Creates drift risk vs transmitter/catalog endpoints.
- Violates single-source-of-truth intent.

## 2) Inverter fallback source is receiver local config, not transmitter spec payload
When `inverter_protocol` is missing/unknown in MQTT-derived spec JSON, the handler falls back to:
- `ReceiverNetworkConfig::getInverterType()`
- then local table lookup (`get_inverter_protocol_name`).

**Impact:**
- Display can reflect receiver-selected intent rather than actual transmitter-reported runtime spec.
- Receiver becomes a second authority for naming.

## 3) Battery page does not embed a hardcoded type-name catalog table
`battery_specs_display_page.cpp` parses battery spec JSON and renders values directly. It does not keep a large local type-name map in C++ page handler.

**Note:** battery page still uses hardcoded numeric fallback defaults for several scalar fields (capacity/voltages/currents), but it does not duplicate a large type catalog in the page handler.

## 4) Inverter page script only resolves interface label, not protocol label
`inverter_specs_display_page_script.cpp` uses `CatalogLoader.loadCatalogLabel(...)` for `inverter_interface` only.

**Impact:**
- Protocol naming path remains partially tied to C++ hardcoded fallback.
- Battery page is more aligned with catalog-loader pattern (type + interface).

## 5) Current behavior mixes three potential sources for protocol display
1. Transmitter MQTT spec (`inverter_protocol`)
2. Receiver local selected type (`ReceiverNetworkConfig`)
3. Hardcoded C++ table in page

**Impact:**
- Ambiguity in “truth” source.
- Harder debugging when displayed protocol differs from transmitter behavior.

## Recommendations

## Priority A (high, low-to-medium effort)
1. **Remove embedded protocol-name table from `inverter_specs_display_page.cpp`.**
2. If MQTT spec lacks protocol name, prefer explicit placeholder (`"Unknown"` / `"Unavailable"`) rather than local remapping.
3. Extend inverter page client script to resolve and render protocol label using `CatalogLoader.loadCatalogLabel(...)` (same pattern as battery page), if a selected type id is available.

Result: no duplicated catalog in page handler; clearer source boundaries.

## Priority B (high, medium effort)
4. Add explicit transmitter-owned identifier in inverter spec payload (e.g., `inverter_type_id`) and optionally a canonical `inverter_protocol_name`.
5. Receiver display should use transmitter payload first, and catalog endpoint only as a view-layer label helper.

Result: transmitter remains authoritative; receiver only formats/presents.

## Priority C (medium, medium effort)
6. Standardize battery/inverter spec handlers with a shared helper policy for:
   - required vs optional fields
   - fallback behavior
   - “unknown” rendering semantics
7. Replace silent magic defaults for key fields with explicit “not reported” state where appropriate.

Result: consistent behavior and fewer misleading defaults.

## Suggested Improvement Plan (incremental)

### Step 1
- Delete `get_inverter_protocol_name()` from inverter page.
- Remove `ReceiverNetworkConfig` fallback path from inverter specs page.
- Keep direct transmitter spec rendering.

### Step 2
- Add protocol label element id in inverter spec content (if needed).
- Add `CatalogLoader.loadCatalogLabel(...)` for inverter type in inverter page script.

### Step 3
- Update transmitter spec publisher to always include either `inverter_protocol_name` or `inverter_type_id` + versioned schema marker.

### Step 4
- Add lightweight tests/checks for:
  - “spec present” rendering
  - “spec missing” rendering
  - no local hardcoded catalog fallback

## Proposed Acceptance Criteria

- No large hardcoded inverter ID→name catalog in receiver page handler.
- Inverter protocol display can be traced to transmitter payload (or explicit unknown state).
- Battery and inverter spec pages follow the same source-of-truth policy.
- Receiver does not infer authoritative protocol names from local config when transmitter data is missing.

## Summary
The inverter spec page currently diverges from single-source-of-truth design by embedding a protocol catalog and falling back to receiver local config. The battery page is closer to the desired model in this specific area. Recommended next improvement is to remove inverter hardcoded mapping and make the display strictly transmitter-driven (with optional catalog-assisted label rendering in the UI layer).