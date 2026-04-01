# Battery Emulator Parity Audit (Battery / Charger / Inverter)

**Date:** 2026-04-01  
**Scope:** Compare transmitter migration at `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/{battery,charger,inverter}` against original `Battery-Emulator-9.2.4/Software/src/{battery,charger,inverter}`.

---

## Executive Summary

Parity is **high** at source-file level for battery/charger/inverter protocol logic.

- **Battery:** Core protocol sources are present. Missing items are UI HTML helper files only.
- **Charger:** Full parity.
- **Inverter:** Source files are present and **BYD Modbus + Kostal RS485 are now enabled and built**.

Implementation status:

1. **Phase 1 (BYD Modbus): completed**.
2. **Phase 2 (Kostal RS485): completed**.
3. Legacy build exclusions that blocked these protocols were removed.

---

## Method

1. Inventory compared by file name within `battery`, `charger`, `inverter` folders (original vs transmitter migration).
2. Runtime availability validated by reviewing transmitter feature flags and source filters:
	- `include/inverter_config.h`
	- `platformio.ini`
	- `src/battery_emulator/inverter/INVERTERS.cpp`

---

## Detailed Findings

## 1) Battery folder

### Missing in transmitter migration

These are present in original but not in transmitter `battery` folder:

- `BMW-I3-HTML.cpp`
- `BMW-I3-HTML.h`
- `BMW-IX-HTML.cpp`
- `BMW-IX-HTML.h`
- `BMW-PHEV-HTML.h`
- `BOLT-AMPERA-HTML.h`
- `BYD-ATTO-3-HTML.h`
- `CELLPOWER-HTML.h`
- `CHADEMO-BATTERY-HTML.h`
- `CMFA-EV-HTML.h`
- `CMP-SMART-CAR-BATTERY-HTML.h`
- `ECMP-HTML.h`
- `GEELY-GEOMETRY-C-HTML.h`
- `HYUNDAI-IONIQ-28-BATTERY-HTML.cpp`
- `HYUNDAI-IONIQ-28-BATTERY-HTML.h`
- `KIA-E-GMP-HTML.cpp`
- `KIA-E-GMP-HTML.h`
- `KIA-HYUNDAI-64-HTML.h`
- `MEB-HTML.h`
- `NISSAN-LEAF-HTML.h`
- `RENAULT-ZOE-GEN1-HTML.h`
- `RENAULT-ZOE-GEN2-HTML.h`
- `TESLA-HTML.h`
- `VOLVO-SPA-HTML.h`
- `VOLVO-SPA-HYBRID-HTML.h`

### Assessment

- These are UI/template helper assets, not core CAN/protocol control logic.
- No battery protocol `.cpp/.h` logic files were found missing.
- **Impact:** Potential missing model-specific web/UI detail pages/options, but not core battery protocol execution.

---

## 2) Charger folder

- **No missing files detected.**
- **Assessment:** Charger parity is complete.

---

## 3) Inverter folder

- **No source-file name gaps detected** (all original inverter files exist in transmitter migration folder).
- Phase 1 and Phase 2 have now been implemented and validated.

### Completed changes

1. **Enabled BYD Modbus**
	- `SUPPORT_BYD_MODBUS` set to `1` in `include/inverter_config.h`.
	- Removed `BYD-MODBUS.cpp` exclusion from `platformio.ini` `build_src_filter`.

2. **Enabled Kostal RS485**
	- `SUPPORT_KOSTAL_RS485` set to `1` in `include/inverter_config.h`.
	- Removed `KOSTAL-RS485.cpp` exclusion from `platformio.ini` `build_src_filter`.

3. **Enabled Modbus base implementation**
	- Removed `ModbusInverterProtocol.cpp` exclusion from `platformio.ini` `build_src_filter`.

4. **Added missing Modbus dependency source**
	- Vendored `src/battery_emulator/lib/eModbus-eModbus` from the upstream Battery Emulator source tree.

### Validation

- Transmitter build succeeded with BYD Modbus/Kostal RS485 enabled:
	- `pio run -j 12` (env `olimex_esp32_poe2`) ✅

### Legacy/redundant cleanup completed

- Removed outdated `platformio.ini` exclusions that blocked Modbus inverter compilation.
- Removed stale comments in inverter config that described Modbus as out-of-scope.

---

## Remaining Plan

## Phase 3 — UI parity pass for battery HTML helpers (completed)

Outcome:

1. Legacy upstream `*-HTML.*` battery helper path is now treated as intentionally deprecated in this architecture.
2. Receiver battery settings UI now surfaces a model-specific parity note for selected battery types that historically had upstream HTML helpers.
3. Canonical UI/status presentation remains receiver-hosted pages + APIs; no legacy transmitter HTML helper migration was performed.

Legacy/redundant cleanup completed in this phase:

- Removed legacy inline `onchange` handlers from battery type/interface selects; retained single listener-driven change handling path.
- Removed stale phase-comment wording from battery settings page handler.
- Removed unused type-catalog API dead code (`inverter_interface_defaults` and an unused disabled-label helper in receiver type-selection API).

---

## Recommended Priority

1. **Completed:** Phase 1 (BYD Modbus).
2. **Completed:** Phase 2 (Kostal RS485).
3. **Completed:** Phase 3 UI parity + legacy helper deprecation documentation.

---

## Conclusion

Battery/charger/inverter migration is now complete for the requested functional scope. BYD Modbus and Kostal RS485 are enabled, compiled, legacy blocking exclusions were removed, and the battery HTML-helper parity item is now closed via receiver-side consolidated UI handling (with legacy helper path intentionally retired).

