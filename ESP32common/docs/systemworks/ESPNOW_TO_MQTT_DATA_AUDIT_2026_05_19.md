# ESP-NOW → MQTT Data Migration Audit (Transmitter/Receiver)

Date: 2026-05-19  
Scope: `ESPnowtransmitter2`, `espnowreceiver_2`, `espnowreceiver_LCD`, shared protocol in `esp32common/espnow_transmitter/espnow_common.h`

## 1) Executive summary

This audit compared:
- legacy ESP-NOW data contracts and active handlers, and
- current MQTT publish/subscribe contracts and payloads.

### Key findings

1. **Core retained config/spec migration is in place** (battery/inverter/power/network/mqtt/meta/catalog/event logs/cell data).
2. **Simulated LED runtime state is not fully migrated to MQTT**:
   - ESP-NOW had explicit runtime LED state (`msg_flash_led`: `color`, `effect`),
   - MQTT `tx/state/static/led` currently carries `serialize_system_specs()` (capability/spec fields), not runtime `color/effect`.
3. **Relay/contactor runtime status is not migrated to MQTT**:
   - legacy ESP-NOW `msg_system_status` exposed `contactor_state`, `error_flags`, `warning_flags`, `uptime_seconds`,
   - no MQTT runtime topic currently publishes equivalent fields.
4. **Transmitter chip temperature has partial migration**:
   - ESP-NOW sent explicit `msg_temperature_report`,
   - MQTT receiver handlers can parse temperature from `battery_live`, but transmitter `publish_data()` does not include temperature fields today.
5. **`espnowreceiver_2` MQTT ingest is weaker than `espnowreceiver_LCD`**:
   - `_2` `handleBatteryLive()` currently only stores temperature (if present), not SOC/power/voltage into `BatteryData`.

---

## 2) Legacy ESP-NOW signals vs MQTT coverage matrix

## Legend
- **Covered**: equivalent data currently published and consumed via MQTT.
- **Partial**: some path exists, but fields/semantics are incomplete.
- **Missing**: no equivalent MQTT publication today.

| Legacy ESP-NOW data | Legacy payload fields | MQTT equivalent now | Status | Notes |
|---|---|---|---|---|
| `msg_data` (`espnow_payload_t`) | `soc`, `power` | `tx/state/battery_live` (`soc`, `power`) | Covered | Basic SOC/power replacement exists. |
| `msg_flash_led` (`flash_led_t`) | `color`, `effect` | `tx/state/static/led` | **Missing** (runtime), only static/spec | MQTT LED topic currently serializes system specs, not runtime LED state. |
| `msg_battery_status` (`battery_status_msg_t`) | SOC, voltage, current, temp, power, charge/discharge limits, BMS status | `tx/state/battery_live` (+ optional parsing fallbacks on receiver) | **Partial** | Current publish includes only SOC/power/timestamps/eth flag; voltage/current/temp/limits/BMS status absent. |
| `msg_charger_status` (`charger_status_msg_t`) | charger HV/LV/AC status, power, state | none (runtime) / `tx/state/static/power` (static charger specs) | **Missing** (runtime) | Static charger capabilities exist; live charger runtime does not. |
| `msg_inverter_status` (`inverter_status_msg_t`) | AC voltage/frequency/current, power, inverter state | none (runtime) / `tx/state/static/inverter` (static specs) | **Missing** (runtime) | Static inverter specs exist; live inverter runtime does not. |
| `msg_system_status` (`system_status_msg_t`) | `contactor_state`, `error_flags`, `warning_flags`, `uptime_seconds` | none | **Missing** | This is the likely source for simulated relay/contactor runtime state in old flow. |
| `msg_temperature_report` (`temperature_report_t`) | tx chip temp, validity, seq, uptime | none dedicated; receivers can parse temp from `battery_live` | **Partial** | No dedicated MQTT temperature topic and no temperature in transmitter `publish_data()` today. |
| `msg_event_log_summary` | summary counters | `tx/state/summary/event_logs` | Covered | Present and consumed. |
| event logs stream (`subtype_logs`/ESP-NOW packets) | event pages/chunks | `tx/state/event_logs/chunk` + stream command control | Covered | Chunking/reassembly implemented. |
| cell info (`subtype_cell_info`) | cell voltages/balancing | `tx/state/cell_data/chunk` | Covered | Chunking/reassembly implemented. |
| `msg_network_config_ack` | current/static network config | `tx/state/static/network` + `tx/ack/network` | Covered | Static retained + command ack path present. |
| `msg_mqtt_config_ack` | mqtt config + connected/version | `tx/state/static/mqtt` + `tx/ack/mqtt` + `tx/meta/runtime` | Covered | Equivalent split by retained static + runtime/meta. |
| `msg_battery_info` / settings section | battery settings/config block | `tx/state/static/battery` + `tx/ack/settings/update` | Partial-to-Covered | Settings update/ack path is present; runtime battery status still partial. |
| type catalogs / versions | battery/inverter catalogs | `tx/state/static/catalog_*` + `tx/meta/schema_versions` | Covered | Present in both receiver MQTT clients. |

---

## 3) Simulated LED status audit (explicit)

## Legacy behavior
- Transmitter computes runtime LED state and sends `msg_flash_led` (`color`, `effect`) via `led_publish_current_state(...)`.
- Receiver uses this runtime state for LED behavior/UI state.

## MQTT behavior now
- Topic exists: `batt-emu/mqtt-v1/tx/state/static/led` (retained).
- Refresh command exists: `batt-emu/mqtt-v1/rx/cmd/refresh/led`.
- But payload source is `StaticData::serialize_system_specs(...)` (hardware/spec metadata), not runtime LED `color/effect`.

## Net result
- **Runtime simulated LED status is not actually migrated to MQTT payloads**.
- Current “LED static topic” is semantically a system-spec topic.

---

## 4) Simulated relays / GPIO contactor state audit (explicit)

User concern is valid.

Legacy runtime status for relay/contactor state is represented by `msg_system_status.contactor_state` (bit flags), plus `error_flags`/`warning_flags`.

Current MQTT topic set has no runtime equivalent carrying these fields.

### Result
- **Contactor/relay runtime state migration to MQTT is currently missing**.

---

## 5) Receiver-specific parity notes

## `espnowreceiver_LCD`
- MQTT ingest is broadly complete for static/meta/cell/event logs/live basics.
- `handleBatteryLive()` updates SOC/power/voltage and can ingest temperature if present.
- LED runtime API still references `ESPNow::current_led_color/current_led_effect`; no MQTT handler currently updates these runtime globals.

## `espnowreceiver_2`
- MQTT `handleBatteryLive()` currently stores only temperature (if present), not SOC/power/voltage into `BatteryData`.
- This creates telemetry parity risk for APIs reading `BatteryData` snapshots.

---

## 6) Required MQTT additions to reach parity

Priority P0 (functional parity):
1. **Add runtime LED state topic** (or repurpose existing LED topic semantics clearly):
   - Proposed topic: `batt-emu/mqtt-v1/tx/state/runtime/led`
   - Payload: `{"color":<0..3>,"effect":<0..2>,"status":"...","seq":N,"ts_ms":...}`
2. **Add runtime system status topic (relay/contactor):**
   - Proposed topic: `batt-emu/mqtt-v1/tx/state/runtime/system`
   - Payload includes at minimum: `contactor_state`, `error_flags`, `warning_flags`, `uptime_seconds`.

Priority P1 (telemetry completeness):
3. Expand `battery_live` to include missing battery runtime fields:
   - `voltage_mv`, `current_ma`, `temperature_centi_c`, `max_charge_power_w`, `max_discharge_power_w`, `bms_status`.
4. Add optional runtime topics for charger/inverter status if UI/features require old parity.
5. Add explicit transmitter temperature topic (or include guaranteed temperature fields in `battery_live`).

Priority P2 (receiver consistency):
6. Align `espnowreceiver_2` MQTT `handleBatteryLive()` with LCD behavior (update SOC/power/voltage, not temperature-only).
7. Update LED runtime API backing store on LCD to be MQTT-fed rather than ESP-NOW globals only.

---

## 7) Suggested acceptance criteria for completion

- LED runtime endpoint returns values driven by MQTT runtime LED payloads.
- Relay/contactor UI/API reflects MQTT `runtime/system` fields.
- `battery_live` provides full runtime set required by prior ESP-NOW parity.
- Both receivers show identical SOC/power/voltage behavior under MQTT-only operation.
- No transmitter-facing web/API path requires ESP-NOW message side effects.

---

## 8) Confidence and methodology notes

- This audit used both protocol definitions and active implementation paths.
- Some ESP-NOW structs are protocol-defined but not actively emitted in current transmitter source; those were treated cautiously in status assignment.
- Findings above focus on practical parity for currently active receiver features and user-observed behavior.