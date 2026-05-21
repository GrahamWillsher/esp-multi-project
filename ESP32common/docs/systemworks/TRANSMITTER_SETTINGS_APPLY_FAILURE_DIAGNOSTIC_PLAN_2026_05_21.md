# Transmitter Settings `APPLY_FAILED` Diagnostic Plan (2026-05-21)

## Scope

This document proposes **transmitter-side code changes only** to make `APPLY_FAILED` root causes explicit for settings updates (especially `SETTINGS_POWER`, `field=4`, `POWER_EQUIPMENT_STOP_TYPE`).

Current behavior confirms transport works, but failure reason is too generic (`"Invalid value or NVS write failed"`).

---

## Confirmed Current Path

1. Receiver publishes `rx/cmd/update/battery` with `category`, `field`, `value`.
2. Transmitter `MqttManager::handle_settings_command()` parses command and calls:
   - `SettingsManager::apply_settings_update(...)`
3. If `apply_settings_update()` returns false, transmitter publishes ACK:
   - `code="APPLY_FAILED"`
   - `message=<generic>`

### Problem

The current ACK message cannot distinguish:
- field-level validation failure,
- cross-field/category validation failure,
- NVS namespace open failure,
- NVS key write failure,
- blob write failure,
- post-save validation failure.

---

## Design Goal

For each failed settings update, return an ACK message that identifies **exact stage + exact reason**.

Target outcomes:
- One failed update gives immediate actionable diagnosis from ACK + transmitter logs.
- No guesswork on whether failure is in parse, validation, or persistence.

---

## Recommended Code Changes

## 1) Add structured failure metadata in `SettingsManager`

### File
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_manager.h`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_manager.cpp`

### Change
Add a diagnostic result struct and expose last failure details.

Suggested struct:
- `category`
- `field_id`
- `stage` (enum/string)
- `reason_code` (enum/string)
- `detail` (short text)
- `nvs_key` (optional)

Suggested stages:
- `FIELD_VALIDATE`
- `CATEGORY_VALIDATE_PRE_SAVE`
- `NVS_OPEN`
- `NVS_WRITE_KEY`
- `NVS_WRITE_BLOB`
- `CATEGORY_VALIDATE_POST_SAVE`
- `UNKNOWN`

Suggested reason codes:
- `OUT_OF_RANGE`
- `DEPENDENCY_INVALID`
- `NVS_OPEN_FAILED`
- `NVS_KEY_WRITE_FAILED`
- `NVS_BLOB_WRITE_FAILED`
- `VALIDATION_FAILED`
- `UNSUPPORTED_FIELD`

Add APIs:
- `void clear_last_apply_failure();`
- `void set_last_apply_failure(...);`
- `ApplyFailureInfo get_last_apply_failure() const;`

---

## 2) Preserve exact failure reason in `apply_settings_update()`

### File
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp`

### Change
Replace generic fallback error mapping (`"Invalid value or NVS write failed"`) with:
- field-specific or stage-specific detail sourced from `last_apply_failure`.

Behavior:
- On success: clear failure info.
- On failure: populate `out_error_msg` with compact machine-readable message, e.g.
  - `"stage=CATEGORY_VALIDATE_PRE_SAVE;reason=DEPENDENCY_INVALID;detail=precharge_duration>max_precharge"`

This message is returned in transmitter ACK and visible immediately on receiver terminal.

---

## 3) Instrument all save paths with stage-aware reporting

### File
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`

### Change
In each `save_*_settings()` method (`power`, `can`, `inverter`, `contactor`, `battery`):

1. Before each early `return false`, call `set_last_apply_failure(...)` with exact stage and reason.
2. For key writes, split writes so failure key is identifiable.
   - Instead of only accumulating `writes_ok &= ...`, record the first failed key name.
3. For blob write failure, report explicitly.

Example for power save failure reporting:
- `validate_power_settings()` fails => `CATEGORY_VALIDATE_PRE_SAVE`
- `prefs.begin("power", false)` fails => `NVS_OPEN`
- `write_u8_checked(..."eq_stop_type"...)` fails => `NVS_WRITE_KEY` + `nvs_key=eq_stop_type`
- blob write fails => `NVS_WRITE_BLOB`

This is the highest-value change for your current symptom.

---

## 4) Add correlation logging in MQTT command handler

### File
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp`

### Change
In `handle_settings_command()`:
- log entry with `request_id`, `category`, `field`, parsed value type/value.
- on failure, pull `ApplyFailureInfo` from `SettingsManager` and include it in:
  1. transmitter log line,
  2. ACK `message` field.

Suggested failure ACK format:
- `code: "APPLY_FAILED"`
- `message: "cat=6 field=4 stage=NVS_WRITE_KEY reason=NVS_KEY_WRITE_FAILED key=eq_stop_type"`

This keeps protocol stable while making diagnostics deterministic.

---

## 5) Add optional debug-only verbose mode (compile flag)

### Files
- `ESPnowtransmitter2/espnowtransmitter2/platformio.ini`
- logging config headers/cpp where appropriate

### Change
Add a build flag for temporary deep diagnostics:
- `-DSETTINGS_APPLY_DIAGNOSTICS=1`

Guard verbose logs under this flag so production verbosity remains unchanged.

Recommended verbose logs:
- before/after value snapshot for affected category,
- exact validation inputs (`power_max_precharge_ms_`, `power_precharge_duration_ms_`, `power_equipment_stop_type_`),
- NVS namespace/key write result codes.

---

## 6) Ensure receiver surfaces full transmitter message

(Receiver already reports ACK code; keep this as verification)

### Files to verify only
- `espnowreceiver_LCD/src/mqtt/mqtt_ack_tracker.cpp`
- `espnowreceiver_LCD/lib/webserver_lcd/api/api_settings_handlers.cpp`

Expected:
- `code` and full `message` passed through unchanged from transmitter.

No protocol change required.

---

## Minimal Implementation Order

1. Add `ApplyFailureInfo` storage and getter/setter (`SettingsManager`).
2. Instrument `save_power_settings()` first (current failing path).
3. Wire `apply_settings_update()` to return precise failure message.
4. Include failure metadata in `publish_settings_ack()` caller log and message.
5. Extend same pattern to `save_can_settings()`, `save_inverter_settings()`, `save_contactor_settings()`, `save_battery_settings()`.

---

## Acceptance Criteria

A failed update for `category=6`, `field=4` should produce a single ACK/log pair that uniquely identifies root cause.

Examples:
- `stage=CATEGORY_VALIDATE_PRE_SAVE reason=DEPENDENCY_INVALID detail=precharge_duration>max_precharge`
- `stage=NVS_OPEN reason=NVS_OPEN_FAILED detail=namespace=power`
- `stage=NVS_WRITE_KEY reason=NVS_KEY_WRITE_FAILED key=eq_stop_type`
- `stage=NVS_WRITE_BLOB reason=NVS_BLOB_WRITE_FAILED detail=power_blob`

If this information appears, diagnosis moves from guesswork to deterministic.

---

## Why this is necessary

Your observed sequence (`category 7` succeeds, `category 6 field 4` fails with `APPLY_FAILED`) already proves transport and request routing are functioning. Remaining uncertainty is entirely inside transmitter apply/persist path. The above changes instrument exactly that boundary.
