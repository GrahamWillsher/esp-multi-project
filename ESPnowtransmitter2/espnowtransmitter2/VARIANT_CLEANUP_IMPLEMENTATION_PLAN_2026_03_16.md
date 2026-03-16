# Battery/Inverter Variant Cleanup — Full Implementation Plan

Date: 2026-03-16  
Project: ESP-NOW Transmitter (`ESPnowtransmitter2/espnowtransmitter2`)  
Goal: Preserve runtime user selection in one FULL build while introducing cleaner, safer variant management and dependency hygiene.

---

## 1) Executive Intent

This implementation is designed to achieve all of the following simultaneously:

1. Keep the default firmware behavior unchanged: all runtime-selectable battery/inverter variants remain available.
2. Make the codebase easier to maintain by reducing unguarded include/switch sprawl.
3. Keep exactly one build profile (FULL) and simplify implementation complexity.
4. Prevent linker failures by ensuring includes and switch branches are guarded together.
5. Improve build speed and dependency hygiene without sacrificing current functionality.

### Implementation Progress (Live)

- [x] Phase 1 started.
- [x] Added compile-time guarded inverter includes in [src/battery_emulator/inverter/INVERTERS.h](src/battery_emulator/inverter/INVERTERS.h).
- [x] Added compile-time guarded inverter supported-list/name/factory logic in [src/battery_emulator/inverter/INVERTERS.cpp](src/battery_emulator/inverter/INVERTERS.cpp).
- [x] Verified FULL build success (`pio run`, `olimex_esp32_poe2`).
- [x] Added compile-time guarded battery includes in [src/battery_emulator/battery/BATTERIES.h](src/battery_emulator/battery/BATTERIES.h).
- [x] Added compile-time guarded battery supported-list/name/factory logic in [src/battery_emulator/battery/BATTERIES.cpp](src/battery_emulator/battery/BATTERIES.cpp).
- [x] Verified FULL build success after battery-side changes (`pio run`, `olimex_esp32_poe2`).
- [x] Added invalid-selection fallback and self-healing persistence in [src/battery_emulator/communication/nvm/comm_nvm.cpp](src/battery_emulator/communication/nvm/comm_nvm.cpp).
- [x] Added matching invalid-selection validation in [src/system_settings.cpp](src/system_settings.cpp).
- [x] Verified FULL build success after runtime validation changes (`pio run`, `olimex_esp32_poe2`).
- [x] Aligned ESP-NOW component selection handling with supported battery/inverter catalogs in [src/espnow/component_catalog_handlers.cpp](src/espnow/component_catalog_handlers.cpp).
- [x] Added matching runtime guard checks in [src/battery/battery_manager.cpp](src/battery/battery_manager.cpp).
- [x] Verified FULL build success after runtime selection path alignment (`pio run`, `olimex_esp32_poe2`).
- [ ] Phase 4 on-device smoke validation pending.

---

## 2) Current Constraints and Realities

- Runtime selection is currently centralized through `create_battery()` and `setup_inverter()`.
- Variant classes are directly referenced in large switch statements.
- Naive source exclusion causes unresolved symbol/linker errors if switch branches still reference excluded classes.
- Current static-IP cleanup already demonstrated the required approach: migrate dependency ownership first, then exclude safely.

Key integration points:
- Battery factory/mapping: [src/battery_emulator/battery/BATTERIES.cpp](src/battery_emulator/battery/BATTERIES.cpp)
- Battery include surface: [src/battery_emulator/battery/BATTERIES.h](src/battery_emulator/battery/BATTERIES.h)
- Inverter factory/mapping: [src/battery_emulator/inverter/INVERTERS.cpp](src/battery_emulator/inverter/INVERTERS.cpp)
- Inverter include surface: [src/battery_emulator/inverter/INVERTERS.h](src/battery_emulator/inverter/INVERTERS.h)
- Existing inverter flags: [include/inverter_config.h](include/inverter_config.h)
- Build filters: [platformio.ini](platformio.ini)

---

## 3) Target End State

### 3.1 Build Profile

Single supported profile:

- **FULL (only)**
   - All battery/inverter runtime options available.
   - Existing behavior preserved.
   - No profile switching logic in build config.

### 3.2 Technical Guarantees

- Build shall not contain stale references that can trigger linker failures.
- `supported_battery_types()` and `supported_inverter_protocols()` shall remain aligned with what FULL build exposes at runtime.
- Existing NVS selections shall remain compatible with FULL build behavior.

---

## 4) Implementation Architecture

## 4.1 Configuration Model

Use current FULL configuration as the single source of truth.

- Keep: [include/inverter_config.h](include/inverter_config.h)
- Avoid adding profile-selection macros to [platformio.ini](platformio.ini)
- Focus cleanup on code structure and guard consistency, not profile permutations.

This reduces complexity and minimizes regression risk.

## 4.2 Guarding Rule (Critical)

For each variant, guard all three together:

1. Header include in `BATTERIES.h` / `INVERTERS.h`
2. Name mapping branch (`name_for_*`)
3. Factory/setup branch (`create_battery()` / `setup_inverter()`)

If any of the three is left unguarded, compile/link instability is likely.

## 4.3 Runtime Catalog Alignment

Update:
- `supported_battery_types()`
- `supported_inverter_protocols()`

So these remain accurate for FULL runtime selection and do not drift from factories.

## 4.4 NVS Compatibility Layer

Because FULL remains the only profile, existing NVS behavior should remain unchanged.  
Any validation work here is limited to defensive checks for corrupt/invalid values, not profile mismatch handling.

---

## 5) Phase-by-Phase Execution Plan

## Phase 0 — Baseline and Safety Nets (1 session)

1. Record baseline build metrics for current FULL build:
   - wall build time,
   - firmware size,
   - free RAM/flash report.
2. Confirm current runtime selector behavior unchanged.

Exit criteria:
- Baseline numbers captured and committed to doc.

## Phase 1 — Inverter Guarding First (lower risk, 1–2 sessions)

1. Normalize inverter flags in [include/inverter_config.h](include/inverter_config.h).
2. Apply guarding rule in [src/battery_emulator/inverter/INVERTERS.h](src/battery_emulator/inverter/INVERTERS.h).
3. Apply matching branch guards in [src/battery_emulator/inverter/INVERTERS.cpp](src/battery_emulator/inverter/INVERTERS.cpp).
4. Update `supported_inverter_protocols()`.
5. Build FULL and verify no behavior change.

Exit criteria:
- FULL passes unchanged.

Status:
- Completed.
- FULL build validated after inverter guard alignment.

## Phase 2 — Battery Guarding (higher complexity, 2–4 sessions)

1. Keep battery configuration in current FULL model (no extra profile header).
2. Apply guarding rule in [src/battery_emulator/battery/BATTERIES.h](src/battery_emulator/battery/BATTERIES.h).
3. Apply matching branch guards in [src/battery_emulator/battery/BATTERIES.cpp](src/battery_emulator/battery/BATTERIES.cpp).
4. Update `supported_battery_types()`.
5. Add selection validation/fallback after NVS load.
6. Validate FULL.

Exit criteria:
- FULL retains all runtime selections.

Status:
- Include guards, supported-list alignment, name mapping, factory creation, and secondary-battery setup guards are implemented.
- Defensive invalid-selection validation is implemented in both active settings load paths.
- FULL build validated successfully after battery-side and runtime-safety refactors.

## Phase 3 — PlatformIO and Build Hygiene (1 session)

1. Keep `default_envs` unchanged (FULL only).
2. Ensure `build_src_filter` excludes only proven-unused sources.
3. Ensure no new profile-selection complexity is introduced.

Exit criteria:
- Build config remains simple and deterministic.

Status:
- Completed earlier in this implementation stream.
- `platformio.ini` remains FULL-only and source exclusions are limited to validated unused sources.

## Phase 4 — Validation Matrix and Documentation (1–2 sessions)

1. Validate FULL runtime matrix:
   - runtime battery/inverter selection works,
   - supported-type lists and factories stay aligned.
2. Verify no stale references in factories.
3. Update user/developer docs:
   - FULL-only strategy,
   - guard rules,
   - validation checklist.

Exit criteria:
- Repeatable build+runtime validation checklist complete.

Status:
- Code-side runtime selection validation is complete across NVS load, ESP-NOW config handling, and Battery Manager initialization.
- Build validation is complete.
- Remaining work is physical on-device smoke validation and final closeout notes.

---

## 6) Detailed Work Breakdown Structure (WBS)

## WBS-A: Config and Guard Hygiene
- A1. Normalize existing FULL config flags.
- A2. Add compile-time sanity checks for consistency.
- A3. Keep all variant policy in one place without profile branching.

## WBS-B: Inverter Refactor
- B1. Guard includes.
- B2. Guard `name_for_inverter_type()`.
- B3. Guard `setup_inverter()`.
- B4. Guard supported-type enumeration.

## WBS-C: Battery Refactor
- C1. Guard includes.
- C2. Guard `name_for_battery_type()`.
- C3. Guard `create_battery()`.
- C4. Guard supported-type enumeration.

## WBS-D: Runtime Safety
- D1. Add defensive validation for invalid enum values from storage.
- D2. Keep fallback behavior safe (`None` + warning) for corrupt data.
- D3. Confirm no runtime drift between factory and supported lists.

## WBS-E: Build Integration
- E1. Keep [platformio.ini](platformio.ini) focused on FULL build.
- E2. Keep source exclusions minimal, validated, and documented.

## WBS-F: QA and Docs
- F1. Build matrix verification.
- F2. Runtime selection smoke tests.
- F3. Update implementation and operations docs.

---

## 7) Risk Register and Mitigations

1. **Risk:** Link errors from partial guard coverage.  
   **Mitigation:** Enforce 3-point guard rule (include + name + factory).

2. **Risk:** Runtime UI drifts from factory behavior.  
   **Mitigation:** Keep `supported_*` and factory switches aligned and tested.

3. **Risk:** Invalid/corrupt stored type values.  
   **Mitigation:** Startup validation and explicit fallback to `None`.

4. **Risk:** Cleanup introduces unnecessary architecture complexity.  
   **Mitigation:** FULL-only strategy and no profile branching.

5. **Risk:** Behavioral drift in FULL profile.  
   **Mitigation:** FULL profile as default, compare against baseline metrics and behavior.

---

## 8) Validation Strategy

## 8.1 Build Validation

For FULL build:
- clean build,
- verify binary size report,
- verify no undefined references.

## 8.2 Runtime Validation

- Startup with valid battery/inverter selection.
- Startup with intentionally invalid stored selection.
- Verify fallback and warning logs.
- Verify settings/config exchange remains stable.

## 8.3 Regression Validation

- ESP-NOW core operation unchanged.
- Ethernet/MQTT/OTA baseline unaffected.
- Existing watchdog and heartbeat behavior unchanged.

## 8.4 On-Device Smoke Checklist

Use this sequence on the transmitter/receiver pair to close Phase 4:

1. **Baseline boot**
   - Flash current FULL transmitter build.
   - Confirm normal boot with no new warnings.
   - Confirm ESP-NOW links, Ethernet comes up, and receiver sees live data.

2. **Valid battery selection**
   - Select a known-good battery type from the receiver UI.
   - Reboot/apply if prompted.
   - Confirm transmitter logs show the expected battery type and battery initialization succeeds.
   - Confirm receiver catalog still matches the selected type.

3. **Valid inverter selection**
   - Select a known-good inverter type from the receiver UI.
   - Reboot/apply if prompted.
   - Confirm transmitter logs show the expected inverter type and no rejection warning.
   - Confirm static specs and MQTT/ESP-NOW metadata remain stable.

4. **Invalid stored battery selection**
   - Inject an out-of-range or unsupported `BATTTYPE` into `batterySettings`.
   - Reboot transmitter.
   - Confirm warning log indicates fallback to `None`.
   - Confirm corrected value is persisted and warning does not repeat on next boot.

5. **Invalid stored inverter selection**
   - Inject an out-of-range or unsupported `INVTYPE` into NVS.
   - Reboot transmitter.
   - Confirm warning log indicates fallback to `None`.
   - Confirm corrected value is persisted and warning does not repeat on next boot.

6. **Invalid double-battery state**
   - Force `DBLBTR=true` while `BATTTYPE=None`.
   - Reboot transmitter.
   - Confirm double-battery mode is cleared automatically.

7. **Regression pass**
   - Confirm periodic battery data, 10s heartbeat, and 30s version beacon continue normally.
   - Confirm Ethernet services and receiver UI remain responsive after selection changes.

---

## 9) Deliverables

1. Updated `include/inverter_config.h` (if needed for consistency)
2. Guarded battery include/factory files
3. Guarded inverter include/factory files
4. Runtime validation helpers for invalid stored values
5. Updated [platformio.ini](platformio.ini) build hygiene notes
6. Updated review and implementation docs

---

## 10) Recommended Rollout Order

1. Merge Phase 1 (inverter guarding).
2. Validate FULL.
3. Merge Phase 2 (battery guarding).
4. Finalize FULL-only docs and validation evidence.

---

## 11) What This Does *Not* Change

- It does not remove runtime selection in FULL build.
- It does not introduce additional build profiles.
- It does not remove variant source files from the repository.

---

## 12) Success Criteria

The implementation is complete when all are true:

1. FULL build exposes all intended runtime battery/inverter options.
2. No stale variant references remain in includes/factory/name mappings.
3. Invalid stored types fail safe and log clearly.
4. Build remains stable with cleaner dependency and source hygiene.
5. Operational docs clearly define FULL-only strategy.
