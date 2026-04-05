# CAN Event Logging and Reporting Investigation (Transmitter vs Original Battery-Emulator)

**Date:** 2026-04-03  
**Repository:** `esp-multi-project`  
**Branch:** `feature/battery-emulator-migration`

---

## 1) Executive summary

I performed a full trace of the CAN-missing event path in both:

- **Original Battery-Emulator 9.2.4**
- **ESPnowtransmitter2 migrated transmitter**

### Final conclusion

There are **two separate causes** behind “no CAN errors after 3+ minutes”:

1. **Previously missing periodic safety call** (already fixed in transmitter):
   - `update_machineryprotection()` was not being called in transmitter `loop()`.
   - This function is where `EVENT_CAN_BATTERY_MISSING` is raised.

2. **Persistent configuration split / source-of-truth mismatch** (still present):
   - Battery initialization at boot reads Battery-Emulator legacy NVS key `batterySettings/BATTTYPE`.
   - Runtime config updates from receiver are persisted to **SystemSettings** (`battery_sys/bat_profile`) and do **not** write legacy `BATTTYPE`.
   - After reboot, battery type can resolve to `None`, leaving global `battery == nullptr`.
   - In that case the CAN watchdog block is skipped (`if (battery) { ... }`), so `EVENT_CAN_BATTERY_MISSING` never fires.

This exactly explains why behavior can look correct in-session, then fail after reboot.

---

## 2) What the original Battery-Emulator does

## 2.1 Event generation timing

In original `Software.cpp`, `update_machineryprotection()` is called in the 1-second update block:

- `Software/Software.cpp` (original): periodic 1s block calls:
  - `battery->update_values()`
  - `update_calculated_values(currentMillis)`
  - `update_machineryprotection()`

So the CAN alive watchdog decrements once per second.

## 2.2 CAN missing watchdog logic

In `safety.cpp` (same logic in original and migrated copy):

- If `battery` exists:
  - if `CAN_battery_still_alive == 0`: `set_event(EVENT_CAN_BATTERY_MISSING, ...)`
  - else decrement counter and clear event

`CAN_battery_still_alive` starts at `CAN_STILL_ALIVE` (60), and each valid CAN RX resets it to 60.

Therefore with no incoming CAN frames, event should trigger in ~60 seconds.

## 2.3 Why this works in original after reboot

Original webserver persists battery selection directly to legacy key:

- `settings.saveUInt("BATTTYPE", ...)`

So reboot restores a concrete battery type, `battery` is initialized, and watchdog logic runs.

---

## 3) What happens in ESPnowtransmitter2

## 3.1 Safety scheduler status

Transmitter now has a 1-second call in `loop()` to `update_machineryprotection()` (recently added).  
Build compiles successfully.

So timing/scheduler parity with original is restored.

## 3.2 Boot-time battery initialization path

Boot still uses Battery-Emulator NVS loader:

- `init_stored_settings()` reads `batterySettings/BATTTYPE`
- then `BatteryManager::init_primary_battery(user_selected_battery_type)`

If `BATTTYPE` is missing/None/invalid, `battery` stays null and watchdog battery block never executes.

## 3.3 Runtime config path mismatch

Runtime component updates from receiver go through `SystemSettings` and persist to `battery_sys/*` keys (e.g. `bat_profile`), not to `batterySettings/BATTTYPE`.

After reboot:

- SystemSettings may still show valid battery profile
- but boot battery init depends on legacy key `BATTTYPE`
- result: possible `battery == nullptr`
- result: no `EVENT_CAN_BATTERY_MISSING`

This is the major parity break vs original behavior.

---

## 4) Evidence map (key code points)

- **CAN watchdog location:**
  - `ESPnowtransmitter2/.../battery_emulator/devboard/safety/safety.cpp`
  - guarded by `if (battery)`

- **Original periodic caller (1s):**
  - `Battery-Emulator-9.2.4/Software/Software.cpp`

- **Transmitter boot battery init:**
  - `ESPnowtransmitter2/.../src/main.cpp` -> `bootstrap_battery()`
  - calls `init_stored_settings()` then `init_primary_battery(user_selected_battery_type)`

- **Legacy key read:**
  - `.../battery_emulator/communication/nvm/comm_nvm.cpp`
  - reads `settings.getUInt("BATTTYPE", BatteryType::None)`

- **Original persistence path:**
  - `Battery-Emulator-9.2.4/.../webserver.cpp`
  - writes `BATTTYPE`

- **Transmitter runtime persistence path:**
  - `.../espnow/component_catalog_handlers.cpp`
  - updates `SystemSettings` battery profile and runtime globals
  - no writeback to `BATTTYPE`

---

## 5) Why no CAN event after 3 minutes can still occur

Even with 1Hz safety call fixed, event will still not appear if any of these are true:

1. `battery == nullptr` (most likely after reboot due persistence split)
2. CAN build flag disabled (not the case here)
3. Device not running updated firmware image (deployment issue)

Given observed “after reboot no event,” #1 is the strongest code-path explanation.

---

## 6) Recommended fix plan

## Phase A (immediate, low risk)

1. **Bridge persistence on config updates:**
   - When receiver applies battery type, also write legacy `batterySettings/BATTTYPE`.
   - Keep `SystemSettings` write too.

2. **Boot diagnostics:**
   - Log explicit warning if `CONFIG_CAN_ENABLED` and `battery == nullptr` after bootstrap.
   - Include current `BATTTYPE` and `SystemSettings` battery profile values in log.

3. **Optional diagnostic event:**
   - Emit a warning event like “Battery type not configured” when CAN enabled but battery object absent.

## Phase B: Unified Source of Truth (architectural fix)

The correct fix unifies battery initialization to consume `SystemSettings` directly at boot, eliminating the divergent NVS read paths.

### Phase B.1: Analysis of current bootstrap flow

**Current order (Phase 2 → Phase 3):**

1. `bootstrap_persistence()` (Phase 2)
   - Initializes `SystemSettings::instance().init()`
   - Loads from `battery_sys/*` namespace ✓
   
2. `bootstrap_battery()` (Phase 3)
   - Calls `init_stored_settings()` 
   - Reads from legacy `batterySettings/*` namespace ← **DIVERGENCE**
   - Sets global `user_selected_battery_type`
   - Calls `BatteryManager::init_primary_battery(user_selected_battery_type)`

**Problem:** `SystemSettings` is already loaded in Phase 2, but Phase 3 ignores it and re-loads battery type from legacy schema.

### Phase B.2: Implementation step-by-step

#### Step 1: Reorder bootstrap phases (no code changes needed, just reordering)
- Phase 2 already loads `SystemSettings` first ✓
- Phase 3 is already battery init
- Order is actually correct; the problem is **Phase 3 ignores Phase 2**.

#### Step 2: Modify `bootstrap_battery()` to consume `SystemSettings`

**Current code flow:**
```cpp
void bootstrap_battery() {
    init_stored_settings();  // ← Reads legacy batterySettings/BATTTYPE
    user_selected_battery_type = ...;  // From legacy read
    BatteryManager::init_primary_battery(user_selected_battery_type);
}
```

**New code flow:**
```cpp
void bootstrap_battery() {
    // Get battery type from already-initialized SystemSettings (Phase 2)
    SystemSettings& settings = SystemSettings::instance();
    uint8_t battery_type = settings.get_battery_profile_type();
    
    // For other non-battery settings, still call init_stored_settings()
    // but ignore/override the battery type it sets
    init_stored_settings();  // Still needed for non-battery config
    
    // Override with SystemSettings value (source of truth)
    user_selected_battery_type = static_cast<BatteryType>(battery_type);
    
    BatteryManager::init_primary_battery(user_selected_battery_type);
}
```

**Key insight:** `init_stored_settings()` handles many non-battery settings needed by Battery Emulator:
- voltage limits (`BATTPVMAX`, `BATTPVMIN`)
- temperature limits
- charger/inverter settings
- CAN interface selection (`BATTCOMM`, `INVCOMM`)
These must still be loaded from legacy store for compatibility.

#### Step 3: Add legacy→new migration on first boot

In `SystemSettings::load_from_nvs()`, add a one-time migration:

```cpp
bool SystemSettings::load_from_nvs() {
    // ... existing load code ...
    
    // MIGRATION: First-boot legacy key consolidation
    // If new schema empty but legacy key exists, migrate it
    if (battery_profile_type_ == static_cast<uint8_t>(BatteryType::None)) {
        Preferences legacySettings;
        if (legacySettings.begin("batterySettings", true)) {
            uint32_t legacyBattType = legacySettings.getUInt("BATTTYPE", 
                                                         static_cast<uint32_t>(BatteryType::None));
            legacySettings.end();
            
            if (legacyBattType != static_cast<uint32_t>(BatteryType::None)) {
                LOG_INFO("SETTINGS", "Migrating legacy BATTTYPE=%u to new schema", legacyBattType);
                battery_profile_type_ = static_cast<uint8_t>(legacyBattType);
                save_to_nvs();  // Persist in new location
            }
        }
    }
    
    return true;
}
```

#### Step 4: Ensure receiver config persists correctly

When receiver sends component config (already in `component_catalog_handlers.cpp`):

```cpp
if (settings.set_battery_profile_type(config->battery_type)) {
    battery_updated = true;
}
```

This **already writes to SystemSettings** ✓

The key: we're now using SystemSettings at boot, so no need for write-through to legacy key anymore. *(Previously required because boot ignored SystemSettings)*

#### Step 5: Add diagnostic logging

In `bootstrap_battery()`, add explicit diagnostics:

```cpp
#if CONFIG_CAN_ENABLED
    LOG_INFO("BATTERY", "Boot battery init source of truth check:");
    LOG_INFO("BATTERY", "  SystemSettings.battery_profile_type = %u", 
             SystemSettings::instance().get_battery_profile_type());
    LOG_INFO("BATTERY", "  global user_selected_battery_type = %u", 
             static_cast<uint32_t>(user_selected_battery_type));
    LOG_INFO("BATTERY", "  battery object = %s", battery ? "INITIALIZED" : "NULL");
    LOG_INFO("BATTERY", "  CAN watchdog will %s when idle", 
             battery ? "FIRE after 60s" : "NOT FIRE (battery == nullptr)");
#endif
```

### Phase B.3: What this solves

| Problem | Before Phase B | After Phase B |
|---------|---|---|
| Battery persisted by receiver | Stored in `battery_sys` | Same, read at boot ✓ |
| Battery persisted by legacy webserver | Stored in `batterySettings` | Migrated once, then unified |
| Reboot consistency | Diverges if receiver config applied | Single source of truth ✓ |
| CAN watchdog after reboot | Unreliable (battery may be null) | Guaranteed if profile set ✓ |
| Event generation | Skipped if `battery == nullptr` | Works reliably after reboot ✓ |
| Settings UI/backend parity | Split (`battery_sys` vs `batterySettings`) | Unified (`battery_sys` only) |

### Phase B.4: Backwards compatibility

- Legacy `batterySettings/BATTTYPE` key is read once during first-boot migration
- If user has old NVS data, it is migrated to new schema automatically
- Subsequent boots use new schema only
- Receiver config writes to new schema; no changes needed

### Phase B.5: Code changes summary

**Files to modify:**

1. `src/system_settings.cpp`
   - Add migration block to `load_from_nvs()` (≈15 lines)
   - No other changes needed

2. `src/main.cpp` → `bootstrap_battery()` 
   - Change `user_selected_battery_type` assignment to pull from `SystemSettings` (2-3 lines)
   - Add diagnostic logging (5-10 lines)
   - Total: ≈15 lines

3. `src/battery_emulator/communication/nvm/comm_nvm.cpp` → `init_stored_settings()`
   - Keep battery type line but ensure it doesn't override SystemSettings
   - Or: remove battery type assignment, let bootstrap_battery() handle it
   - Total: 0 lines if we just change bootstrap_battery() strategy

4. (Optional) `src/espnow/component_catalog_handlers.cpp`
   - Already correct; receiver config → SystemSettings → persisted ✓
   - No changes needed

**Total code impact: ≈30 lines, minimal, low risk.**

### Phase B.6: Why this fixes CAN event logging

**Current failure mode:**
```
Receiver config → SystemSettings (battery_sys/bat_profile) ✓
Transmitter reboots
  → bootstrap_battery() reads legacy batterySettings/BATTTYPE
  → Legacy key not updated by receiver
  → user_selected_battery_type = None
  → battery = nullptr
  → update_machineryprotection() skips CAN watchdog block
  → NO EVENT after 60s idle
```

**After Phase B:**
```
Receiver config → SystemSettings (battery_sys/bat_profile) ✓
Transmitter reboots
  → SystemSettings::load_from_nvs() checks legacy key, migrates if needed
  → bootstrap_battery() reads from SystemSettings.get_battery_profile_type()
  → user_selected_battery_type = correct value
  → battery = initialized ✓
  → update_machineryprotection() runs CAN watchdog logic ✓
  → EVENT_CAN_BATTERY_MISSING fires after 60s idle ✓
```

### Phase B.7: Testing strategy

1. **Fresh transmitter (no legacy NVS):**
   - Set battery via receiver → verify boots with correct battery ✓
   - Keep CAN idle 70s → verify EVENT_CAN_BATTERY_MISSING ✓

2. **Legacy transmitter (old NVS with BATTTYPE):**
   - Boot with legacy key present → verify migration log ✓
   - Reboot again → verify no migration (already done) ✓
   - Keep CAN idle 70s → verify EVENT_CAN_BATTERY_MISSING ✓

3. **Config change after receiver updates:**
   - Receiver applies new battery type → SystemSettings updates ✓
   - Reboot → verify new battery initialized ✓
   - Keep CAN idle 70s → verify EVENT_CAN_BATTERY_MISSING ✓

## Phase C: Implementation execution

With Phase B analysis complete, the implementation is straightforward and low-risk:

### C.1: SystemSettings migration (system_settings.cpp)

Add to `load_from_nvs()` after normal NVS loads complete:

```cpp
// If new battery_profile_type is None but legacy BATTTYPE exists, migrate it
if (battery_profile_type_ == static_cast<uint8_t>(BatteryType::None)) {
    Preferences legacyPrefs;
    if (legacyPrefs.begin("batterySettings", true)) {  // read-only
        uint32_t legacyBattType = legacyPrefs.getUInt("BATTTYPE", 
                                               static_cast<uint32_t>(BatteryType::None));
        legacyPrefs.end();
        
        if (legacyBattType != static_cast<uint32_t>(BatteryType::None)) {
            LOG_INFO("SETTINGS", "MIGRATION: Importing legacy BATTTYPE=%u to new schema", 
                     legacyBattType);
            battery_profile_type_ = static_cast<uint8_t>(legacyBattType);
            // Persist in new location so migration doesn't repeat
            save_to_nvs();
        }
    }
}
```

### C.2: Bootstrap battery rewiring (main.cpp)

Replace battery type loading in `bootstrap_battery()`:

```cpp
#if CONFIG_CAN_ENABLED
    // Get battery type from SystemSettings (Phase 2 already initialized it)
    SystemSettings& settings_ref = SystemSettings::instance();
    uint8_t battery_profile = settings_ref.get_battery_profile_type();
    
    // Diagnostics BEFORE any init
    LOG_INFO("BATTERY", "┌─────────────────────────────────────────────────────┐");
    LOG_INFO("BATTERY", "│ Boot Battery Init - Source of Truth Verification      │");
    LOG_INFO("BATTERY", "├─────────────────────────────────────────────────────┤");
    LOG_INFO("BATTERY", "│ SystemSettings.battery_profile = %u (BatteryType)    │", battery_profile);
    LOG_INFO("BATTERY", "│ CAN_ENABLED = yes, watchdog will check 1x/sec        │");
    LOG_INFO("BATTERY", "└─────────────────────────────────────────────────────┘");
    
    // Initialize events system (MUST be first)
    init_events();
    
    // ... record reset reason as before ...
    
    // Load non-battery settings from legacy store (still needed for inverter/charger/limits)
    LOG_INFO("BATTERY", "Loading non-battery settings from legacy store...");
    init_stored_settings();
    
    // OVERRIDE battery type with SystemSettings value (source of truth)
    user_selected_battery_type = static_cast<BatteryType>(battery_profile);
    LOG_INFO("BATTERY", "Battery type set from SystemSettings: %u", static_cast<uint32_t>(user_selected_battery_type));
    
    // Initialize CAN driver
    LOG_INFO("CAN", "Initializing CAN driver...");
    if (!CANDriver::instance().init()) {
        LOG_ERROR("CAN", "CAN initialization failed!");
    } else {
        LOG_INFO("CAN", "✓ CAN driver ready");
    }
    
    // Initialize battery with unified source-of-truth type
    LOG_INFO("BATTERY", "Initializing battery (type: %d)...", static_cast<int>(user_selected_battery_type));
    if (BatteryManager::instance().init_primary_battery(user_selected_battery_type)) {
        LOG_INFO("BATTERY", "✓ Battery initialized: %u cells configured",
                 datalayer.battery.info.number_of_cells);
        LOG_INFO("BATTERY", "✓ CAN watchdog is ACTIVE (will fire EVENT_CAN_BATTERY_MISSING after 60s idle)");
    } else {
        LOG_WARN("BATTERY", "Battery initialization returned false (may be None type)");
        if (user_selected_battery_type == BatteryType::None) {
            LOG_WARN("BATTERY", "⚠ Battery type is NONE - CAN watchdog will NOT fire");
            LOG_WARN("BATTERY", "  Configure battery via receiver UI or set BATTTYPE in NVS");
        }
    }
    
    LOG_INFO("DATALAYER", "✓ Datalayer initialized");
#endif
```

### C.3: Verification endpoints (optional)

Add a diagnostic HTTP endpoint for verification:

```cpp
// In ota_status_handlers.cpp
esp_err_t OtaManager::diagnostic_battery_config_handler(httpd_req_t *req) {
    StaticJsonDocument<256> doc;
    
    SystemSettings& settings = SystemSettings::instance();
    doc["system_settings_battery_type"] = settings.get_battery_profile_type();
    doc["global_user_selected_battery_type"] = static_cast<uint32_t>(user_selected_battery_type);
    doc["battery_initialized"] = (battery != nullptr);
    doc["battery_cells"] = battery ? datalayer.battery.info.number_of_cells : 0;
    doc["can_battery_still_alive"] = datalayer.battery.status.CAN_battery_still_alive;
    doc["can_battery_missing_event_active"] = (get_event_pointer(EVENT_CAN_BATTERY_MISSING)->state != EVENT_STATE_INACTIVE);
    
    char response[512];
    size_t len = serializeJson(doc, response, sizeof(response));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, len);
}
```

Register at: `/api/diag/battery_config`

### C.4: Updated testing protocol

Same as Phase B.7 above, now executable.

---

## 7) Additional note on event log display

`/api/get_event_logs` currently includes events with `occurences > 0`, which means historical events may appear even after they are no longer active. This is not the root cause of the missing CAN event, but worth documenting for UI semantics.

## 8.5) Phase B implementation complexity summary

| Aspect | Complexity | Risk | Lines of Code |
|--------|-----------|------|---|
| SystemSettings migration logic | Low | Very Low | ~15 |
| bootstrap_battery() rewiring | Low | Low | ~15 |
| Diagnostic logging | Low | None | ~10 |
| Changes to receive/network layer | None | None | 0 |
| Breaking changes | None | None | N/A |
| **Total** | **Low** | **Low** | **~40** |

**Backwards compatibility:** ✓ Automatic (one-time legacy→new migration)  
**Rollback plan:** Trivial (revert 2 files, old NVS keys still exist)  
**Testing time:** ~15 minutes (boot + 70s idle + log verification)

---

## 9) Additional note on event log display

## 8) Practical next implementation steps

1. Add write-through for receiver-applied battery type to `batterySettings/BATTTYPE`.
2. Add startup assertion/log for `battery == nullptr` in CAN-enabled mode.
3. Run reboot + 3-minute no-CAN validation and capture logs.
4. (Optional) add a self-test endpoint exposing:
   - selected battery type (SystemSettings)
   - legacy BATTTYPE
   - `battery` initialized yes/no
   - current `CAN_battery_still_alive`
   - current `EVENT_CAN_BATTERY_MISSING` state.

---

## 9) Bottom line

The transmitter now has the missing 1-second safety scheduler call ✓, but **reboot persistence parity with original is still broken** due to split settings stores (`battery_sys` vs `batterySettings`).

**Phase B solution:** Unify source-of-truth by having `bootstrap_battery()` consume `SystemSettings` directly (instead of legacy `batterySettings/BATTTYPE`). This is a **30-line, low-risk change** with:

- **One-time migration** of any legacy keys to new schema (automatic on first boot)
- **No receiver changes** needed (already writes to SystemSettings)
- **Complete parity** with original after reboot
- **CAN-missing event guaranteed** if battery is configured

**Result after Phase B:**
- Battery type is single source of truth → `SystemSettings` only
- Reboot consistency restored
- Event logging works reliably across reboot cycles
- Ready for deprecation of legacy schema in future versions
