# Transmitter NVS Key Length Audit (2026-05-21)

## Scope

Audit all transmitter-side NVS key names used via `Preferences` to ensure no key exceeds ESP32 NVS key length limits and to avoid future latent save failures.

Target project audited:
- `ESPnowtransmitter2/espnowtransmitter2`

Audit method:
1. Static scan of direct `prefs.put*()/get*()/isKey()/remove()` string literals.
2. Static scan of helper-wrapped writes (`write_*_checked(...)`) where key literals are passed indirectly.
3. Manual review of load/save logic around all over-limit keys.
4. Cross-codebase search across workspace projects to verify contract impact:
   - `ESPnowtransmitter2`
   - `espnowreceiver_2`
   - `espnowreceiver_LCD`
   - `esp32common`
   - reference tree in `Battery-Emulator-9.2.4` (read-only comparison)

---

## Implementation status (completed 2026-05-21)

Implemented in transmitter settings persistence:

- Power NVS key migrated:
   - `max_precharge_ms` -> `max_pre_ms`
- Inverter NVS key migrated:
   - `cells_per_module` -> `cells_per_mod`
- Contactor periodic BMS reset key migrated:
   - `per_bms` -> `per_bms_reset`

Legacy/old key fallback removed for these migrated keys:

- Removed nested fallback read using long key `periodic_bms_reset`.
- Removed all `settings_persistence.cpp` NVS literals `max_precharge_ms` and `cells_per_module`.

Validation after implementation:

- Transmitter build: **SUCCESS** (`pio run -j 12`).
- Post-change grep check: no remaining legacy NVS literals/fallback pattern for these keys in:
   - `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`

---

## Constraint

ESP32 NVS key names are limited (practically 15 chars max for key name). Any longer key may fail at runtime (e.g., `KEY_TOO_LONG`) during write.

Preferred key validation:
- `cells_per_mod` length = 13 (safe)
- `per_bms_reset` length = 13 (safe)

---

## Findings Summary

### Over-limit keys detected

1. `max_precharge_ms` (len 16)
   - Read path: `getUShort("max_precharge_ms", ...)`
   - Write path: `write_u16_checked(..., "max_precharge_ms", ...)`
   - Namespace: `power`
   - Status: **active write failure observed** in terminal logs (`KEY_TOO_LONG`).

2. `cells_per_module` (len 16)
   - Read path: `getUChar("cells_per_module", ...)`
   - Write path: `write_u8_checked(..., "cells_per_module", ...)`
   - Namespace: `inverter`
   - Status: **latent bug** (will fail when inverter settings are persisted).

3. `periodic_bms_reset` (len 18)
   - Read fallback path only: `getBool("periodic_bms_reset", ...)`
   - Namespace: `contactor`
   - Status: **read-only fallback**; current writes already use short key `per_bms`.

### Namespaces

Namespaces in this file are all safe (`battery`, `power`, `inverter`, `can`, `contactor`). No namespace-length issue found.

---

## Cross-codebase verification findings

### 1) Where these keys are actually used

For the settings system under investigation, the affected NVS keys are read/written in one place:
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`

No receiver code reads these NVS keys directly.

### 2) Receiver/API/MQTT contracts are unaffected

Receiver projects use semantic field names over MQTT/JSON (for example `max_precharge_ms`, `cells_per_module`, `periodic_bms_reset`) and settings category/field IDs, not transmitter NVS key literals.

Therefore renaming transmitter NVS storage keys to:
- `max_pre_ms`
- `cells_per_mod`
- `per_bms_reset`

does **not** require protocol/schema/UI changes in:
- `espnowreceiver_2`
- `espnowreceiver_LCD`
- `esp32common` settings enums/contracts

### 3) Legacy/parallel NVM code does not block these changes

The older comm/nvm path uses uppercase legacy keys such as `PERBMSRESET` and `MAXPRETIME` in a different settings path. This does not conflict with the lowercase `Preferences` keys in `settings_persistence.cpp`.

### 4) Collision check for proposed key names

No conflicting existing usage of the new proposed NVS literals was found in transmitter settings persistence paths.

Validated lengths:
- `max_pre_ms` = 10
- `cells_per_mod` = 13
- `per_bms_reset` = 13

All are within practical ESP32 NVS key limits.

---

## Exact locations (current code)

- `max_precharge_ms`
  - `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`
  - Read fallback near load power settings (`prefs.getUShort("max_precharge_ms", ...)`)
  - Write in `save_power_settings()` (`write_u16_checked(..., "max_precharge_ms", ...)`)

- `cells_per_module`
  - `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`
  - Read fallback near load inverter settings (`prefs.getUChar("cells_per_module", ...)`)
  - Write in `save_inverter_settings()` (`write_u8_checked(..., "cells_per_module", ...)`)

- `periodic_bms_reset`
  - `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`
  - Read fallback in `load_contactor_settings()` (`prefs.getBool("periodic_bms_reset", ...)`)

---

## Proposed key changes

Use NVS-safe short key aliases consistently for both read and write.

### Power
- Replace key `max_precharge_ms` -> `max_pre_ms`
- Keep `precharge_ms` unchanged (len 12, safe)
- Keep `eq_stop_type` unchanged (len 12, safe)

### Inverter
- Replace key `cells_per_module` -> `cells_per_mod`

### Contactor
- Replace canonical short key `per_bms` -> `per_bms_reset` (read/write)
- Keep fallback read of legacy long key `periodic_bms_reset` only if required for compatibility.

---

## Compatibility / migration strategy

Because writes currently also target over-limit names for two settings, there is effectively no reliable old data to migrate from those long keys.

Recommended strategy:
1. Switch writes to short keys immediately.
2. Reads should use short keys as primary.
3. Optional transitional fallback for old long keys can be kept only if you want conservative compatibility, but it is expected to be non-functional on ESP32 NVS due to key length limits.
4. Blob path remains authoritative when present and valid; legacy keys are fallback only.

---

## Files that need changes

### Required

1. `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`
   - Replace over-limit write keys in:
     - `save_power_settings()`
     - `save_inverter_settings()`
   - Replace over-limit read keys in:
     - `load_power_settings()`
     - `load_inverter_settings()`
   - Remove/adjust long fallback read in:
     - `load_contactor_settings()`

2. `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`
   - Update diagnostic key labels passed to `set_last_apply_failure(...)` to match new key names where applicable. ✅ Completed

### Not required for this key migration

- `espnowreceiver_2` (no NVS key dependency)
- `espnowreceiver_LCD` (no NVS key dependency)
- `esp32common` transport/settings contracts (unchanged)

### Strongly recommended hardening

2. `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`
   - Introduce centralized key constants (e.g., `constexpr const char*`) and enforce max length with compile-time checks where practical.

3. (Optional new file) `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_nvs_keys.h`
   - Define all namespace/key constants in one place.
   - Add `static_assert` checks for key length where represented as string literals.

---

## Proposed implementation checklist

1. Replace:
   - `"max_precharge_ms"` -> `"max_pre_ms"`
   - `"cells_per_module"` -> `"cells_per_mod"`
2. Update matching read fallback keys accordingly.
3. Update contactor canonical key for periodic reset:
   - `"per_bms"` -> `"per_bms_reset"`
   - keep/remove `"periodic_bms_reset"` fallback per compatibility choice.
4. Build and test transmitter:
   - Save power field 4 (`equipment_stop_type`) should no longer fail at `max_precharge_*` key write.
   - Save inverter fields should persist without `KEY_TOO_LONG`.
5. Verify persisted values survive reboot.

### Additional verification pass (recommended)

6. Run save/readback checks for each affected category:
   - Power (`POWER_MAX_PRECHARGE_MS`, `POWER_EQUIPMENT_STOP_TYPE`)
   - Inverter (`INVERTER_CELLS_PER_MODULE`)
   - Contactor (`CONTACTOR_PERIODIC_BMS_RESET`)
7. Confirm no new `KEY_TOO_LONG` messages appear in transmitter monitor logs.

### Completion notes

- Checklist items for key migration and build verification are complete.
- Remaining runtime confirmation is on-device save/readback of the affected settings categories to verify no stale NVS data assumptions in deployed units.

---

## Expected outcome after change

- No `KEY_TOO_LONG` failures in power or inverter persistence paths.
- Settings ACK failures will represent genuine validation/NVS issues, not key-name format issues.
- Reduced risk of future regressions by centralizing and constraining key naming.

---

## Notes

This audit found no additional over-limit transmitter NVS key literals beyond the three listed above. The immediate operational blocker is `power/max_precharge_ms`; `inverter/cells_per_module` is the next likely failure if left unchanged.
