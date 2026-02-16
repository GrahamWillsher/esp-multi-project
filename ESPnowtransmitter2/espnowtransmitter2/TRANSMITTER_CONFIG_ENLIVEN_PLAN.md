# Transmitter Config Enlivening Plan (Battery, Power, CAN)

## Goal
Make the remaining sections on /transmitter/config (Battery Configuration, Power Settings, CAN Configuration, etc.) editable, persist changes in NVS, and apply them in the running firmware in a safe, backward-compatible way.

## Current State (High-Level)
- /transmitter/config already supports editable network + MQTT configuration with change tracking and a Save button.
- Battery/Power/CAN sections are currently rendered as read-only values (template placeholders) and are not editable.
- NVS storage exists for several transmitter settings, but not yet exposed for all these sections in the UI.

## Proposal Overview
We will implement a parallel “config model” that includes the remaining sections and expose:
1. **UI controls** (editable fields, checkboxes, selects) on /transmitter/config.
2. **Client-side change tracking** for those fields, unified with existing Save behavior.
3. **New API endpoint(s)** to save the additional sections in NVS.
4. **Server-side NVS persistence** and validation per section.
5. **Runtime application** (update modules without full reboot where safe, or mark for restart when required).

## Naming Conventions (Aligned to Battery Emulator)
The battery emulator codebase uses **snake_case** with **unit suffixes** for numeric fields and **_active / _enabled** for booleans. Examples from datalayer structures include:
- Voltage: `_dV` (deciVolt) and `_mV` (milliVolt)
- Current: `_dA` (deciAmpere)
- Power: `_W`
- Time: `_ms`
- Booleans: `*_active`, `*_enabled`, `*_allowed`, `*_requests_*`

To align with that convention, new fields and NVS keys should follow the same pattern. Example mapping:
- `battery_max_voltage_mV` or `max_cell_voltage_mV` (not `batteryMaxMv`)
- `max_charge_power_W`, `max_discharge_power_W`
- `precharge_duration_ms`, `max_precharge_time_ms`
- `use_static_ip` (already in use in receiver config)

If an existing battery emulator identifier already exists (e.g., datalayer fields like `max_user_set_charge_dA`, `max_design_voltage_dV`), we should **reuse that exact name** in the transmitter config model and NVS keys to avoid drift between the two codebases.

## Detailed Plan

### 1) Define a TransmitterConfig Model
Create a single struct to own all transmitter configuration across sections.

**Example** (pseudo):
```
struct TransmitterConfig {
  // Network
  bool staticIpEnabled;
  IPAddress ip, gateway, subnet, dns1, dns2;

  // MQTT
  bool mqttEnabled;
  IPAddress mqttServer;
  uint16_t mqttPort;
  String mqttUsername, mqttPassword, mqttClientId;

  // Battery
  bool doubleBattery;
  uint16_t batteryMaxMv;
  uint16_t batteryMinMv;
  uint16_t cellMaxMv;
  uint16_t cellMinMv;
  bool useEstimatedSoc;

  // Power
  uint16_t chargePowerW;
  uint16_t dischargePowerW;
  uint16_t maxPrechargeTimeMs;
  uint16_t prechargeDurationMs;

  // CAN
  uint8_t inverterCells;
  uint8_t inverterModules;
  uint8_t cellsPerModule;
  uint16_t inverterVoltage;
  uint16_t inverterCapacity;
  uint8_t inverterBatteryType;
  // etc.
};
```

### 2) NVS Storage Layer (TransmitterConfigManager)
Implement a manager with:
- `loadConfig()` (read defaults + NVS overrides)
- `saveConfig()` (validate, persist)
- `applyConfig()` (push values into runtime subsystems)

**Design suggestions:**
- Use namespaced keys that mirror battery emulator naming: `txcfg/max_cell_voltage_mV`, `txcfg/max_charge_power_W`, `txcfg/max_precharge_time_ms`, `txcfg/inverter_cells`, etc.
- Store numeric types as `uint16_t` or `uint32_t` with `Preferences` for compactness.
- Keep a `config_version` in NVS to support upgrades and migration.

### 3) UI Changes on /transmitter/config
Convert each section from read-only placeholders to editable fields:
- **Battery Configuration**: numeric inputs + checkbox.
- **Power Settings**: numeric inputs.
- **CAN Configuration**: numeric inputs + select/checkbox where appropriate.

Follow the same styling rules as current editable fields:
- `editable-field` for inputs
- Use `settings-row` layout
- Keep read-only where changes are not allowed

### 4) Client-Side Change Tracking
Extend the existing `TRANSMITTER_CONFIG_FIELDS` array to include:
- Battery fields
- Power fields
- CAN fields

Then reuse the existing `countNetworkChanges()` / `updateSaveButtonText()` logic to reflect changes across all sections.

### 5) API Endpoint for Saving
Add a POST endpoint, e.g.:
- `POST /api/save_transmitter_config`

Payload example:
```
{
  "battery": { "double_battery": true, "max_mv": 4200, ... },
  "power": { "charge_w": 3000, "discharge_w": 5000, ... },
  "can": { "inverter_cells": 96, "modules": 2, ... }
}
```

Server:
- Parse JSON
- Validate ranges
- Persist via `TransmitterConfigManager::saveConfig()`
- Respond `{ success: true }`

### 6) Runtime Apply Strategy
Not all changes should apply live:
- **Safe live-apply**: display-only, thresholds, logging intervals
- **Requires restart**: major config affecting ESP-NOW roles, bus setup, or hardware init

Proposed approach:
- Apply safe fields immediately
- Mark `requires_reboot = true` for others
- Inform UI when reboot needed

### 7) Validation Rules (Examples)
- Battery and cell voltages: enforce min/max ranges
- Power: enforce maximum plausible charge/discharge values
- CAN: ensure inverter config values are within known bounds

### 8) Backward Compatibility (Detailed)
Backward compatibility means **existing devices and stored settings keep working** even after we add new fields:

1. **Missing keys → defaults**
  - On load, if a key does not exist in NVS, use the compiled default (or datalayer default). This ensures older firmware data does not break.

2. **Config versioning → safe upgrades**
  - Maintain a `config_version` key. When the version changes, perform a migration step that fills in new keys with defaults and preserves existing values.

3. **Non‑breaking API changes**
  - Keep old endpoints intact (e.g., `/api/save_network_config`). New fields should be **optional** in the new payload so older UIs can still save without failures.

4. **Forward compatibility**
  - If a device sees unknown keys (from a newer UI), ignore them rather than rejecting the request.

5. **Runtime safety**
  - If a setting cannot be applied live, preserve it but request a reboot instead of failing (prevents “half‑applied” states).

## Suggested UX Improvements
1. **Section-level Save** buttons (optional) to reduce risk and complexity.
2. **Validation messages inline** (instead of alerts), with field highlighting.
3. **“Reboot required” banner** after saving a config that needs it.
4. **Config export/import** JSON for backup/restore.
5. **Tooltips** for complex fields (battery and CAN settings are error-prone).

## Implementation Steps
1. Add `TransmitterConfig` struct + `TransmitterConfigManager` (load/save/apply).
2. Add NVS key definitions and defaults.
3. Update /transmitter/config HTML to editable inputs for Battery/Power/CAN.
4. Extend JS change tracking and add save handler.
5. Add POST API endpoint + validation.
6. Apply changes in runtime and/or mark reboot required.
7. End-to-end test on device.

## Risks and Mitigations
- **Misconfiguration risk**: add strict validation and defaults.
- **NVS wear**: only write on explicit save; avoid auto-save on each change.
- **Partial updates**: use atomic save (validate all before writing).

## Next Actions
If you want me to proceed, I can:
1. Implement the config manager + NVS storage.
2. Update the UI + JS on /transmitter/config.
3. Add the save API endpoint and wire it to NVS.

