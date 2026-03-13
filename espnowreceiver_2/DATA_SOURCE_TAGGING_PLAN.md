# Data Source Tagging Plan (Dummy vs Live vs Live‑Simulated)

## Purpose
Ensure the /cellmonitor page always displays the correct Data Source label:
- **dummy** → test mode ON (generated test data)
- **live** → test mode OFF + fresh CAN data available
- **live_simulated** → test mode OFF but CAN data is stale/missing and fallback dummy values are used

This document specifies **exact work items** for both transmitter and receiver. No code is changed here.

---

## Current Behavior Summary
1) The receiver **always shows “live”** because the SSE handler hardcodes `"mode":"live"` whenever any cell data exists.
2) The transmitter **generates dummy cell voltages** whenever it detects no real cell data, **regardless of test mode**, and publishes these values without tagging the source.

Impact: The UI cannot accurately differentiate dummy vs live vs live‑simulated.

---

## Current CAN Freshness Signals (as implemented)
- `CAN_battery_still_alive` is a **counter** that is reset on each CAN message, not a timestamp. It is defined in the datalayer status struct and updated by the CAN driver. See [ESPnowtransmitter2/espnowtransmitter2/src/datalayer/datalayer.h](ESPnowtransmitter2/espnowtransmitter2/src/datalayer/datalayer.h#L101-L123) and [ESPnowtransmitter2/espnowtransmitter2/src/communication/can/can_driver.cpp](ESPnowtransmitter2/espnowtransmitter2/src/communication/can/can_driver.cpp#L70-L93).
- The CAN driver also writes `datalayer.last_can_message_timestamp` on each received CAN message, but no other references are present in the codebase and it is not used by cell_data serialization. See [ESPnowtransmitter2/espnowtransmitter2/src/communication/can/can_driver.cpp](ESPnowtransmitter2/espnowtransmitter2/src/communication/can/can_driver.cpp#L70-L93).
- The current cell_data JSON has **no timestamp or source tag**. See [ESPnowtransmitter2/espnowtransmitter2/src/datalayer/static_data.cpp](ESPnowtransmitter2/espnowtransmitter2/src/datalayer/static_data.cpp#L130-L255).

---

## Transmitter – Required Changes

### T1) Add `data_source` to the MQTT cell_data payload
**Location:** ESPnowtransmitter2/espnowtransmitter2/src/datalayer/static_data.cpp (cell data serialization)

Add a string field in the JSON payload:
```
"data_source": "dummy" | "live" | "live_simulated"
```

#### Source Selection Rules
- **dummy**
  - Test mode ON (TestDataGenerator enabled).
- **live**
  - Test mode OFF and CAN cell data is “fresh.”
- **live_simulated**
  - Test mode OFF but CAN cell data is stale/missing, so fallback dummy values are used.

### T2) Track last valid CAN update time
**Location:** Wherever CAN updates populate `datalayer.battery.status.cell_voltages_mV[]`.

Add a timestamp (e.g., `last_cell_update_ms`) and update it whenever **valid real cell data** is written.

#### Freshness Rule
- If `now - last_cell_update_ms` > threshold (e.g., **2000–5000 ms**), data is stale.

### T3) Ensure test mode state is observable
Use one of:
- `TestDataConfig::get_config().mode`
- `TestDataGenerator::is_enabled()`

This must be read during cell_data serialization to decide `data_source`.

---

## Receiver – Required Changes

### R1) Parse `data_source` from MQTT cell_data payload
**Location:** espnowreceiver_2/src/mqtt/mqtt_client.cpp (handleCellData)

Read the `data_source` field and pass to the TransmitterManager cache.

### R2) Store `data_source` in TransmitterManager
**Location:** espnowreceiver_2/lib/webserver/utils/transmitter_manager.cpp

Add:
- Cached string/enum for `cell_data_source_`
- Getter `getCellDataSource()`

### R3) Include `data_source` in SSE output
**Location:** espnowreceiver_2/lib/webserver/api/api_handlers.cpp (SSE handler)

Replace the hardcoded `"mode":"live"` with the stored `data_source` value from TransmitterManager.

### R4) UI already displays `data.mode`
**Location:** espnowreceiver_2/lib/webserver/pages/cellmonitor_page.cpp

No changes needed once SSE includes correct `mode` value.

---

## Additional Recommendations (Optional)

### O1) Publish a `data_valid` flag
Add `"data_valid":true/false` so the UI can display “stale” state explicitly.

### O2) Publish a `source_reason`
Examples:
- `"source_reason":"test_mode"`
- `"source_reason":"no_can"`
- `"source_reason":"normal"`

Useful for debugging and logs.

### O3) Log transitions
Log whenever `data_source` changes (e.g., dummy → live, live → live_simulated). This helps verify behavior in the field.

---

## Verification Checklist

1) Test mode = **SOC/POWER** or **FULL**
   - `data_source` = `dummy`
   - /cellmonitor shows **dummy**

2) Test mode = **OFF**, CAN active
   - `data_source` = `live`
   - /cellmonitor shows **live**

3) Test mode = **OFF**, CAN inactive or stale
   - `data_source` = `live_simulated`
   - /cellmonitor shows **live_simulated**

---

## Notes
- Do **not** attempt to infer dummy/live based on voltage values; fallback dummy voltages are non‑zero and appear valid.
- The authoritative source should be computed by the transmitter at publish time.

