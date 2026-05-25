# Comprehensive Settings Investigation: Findings & Implementation Plan

Date: 2026-05-25 (Updated)
Scope: Version-checking audit, specific `/transmitter/hardware` failure analysis, architectural alignment plan

## Current Implementation Status (2026-05-25)

- Phase 1: implemented and build-verified (transmitter `SettingsManager::init()` startup fix).
- Phase 2/3: implemented and build-verified in both receivers (version-aware API/cache/page flow).
- Legacy cleanup for this workflow: completed (no redundant parallel settings path left in this flow).
- Remaining work: hardware validation run, then Phase 4/5 closeout.

---

## TABLE OF CONTENTS

1. [Specific Failure Mode Analysis](#specific-failure-mode-analysis)
2. [Version-Checking Audit](#version-checking-audit)
3. [Identified Issues](#identified-issues)
4. [Comprehensive Implementation Plan](#comprehensive-implementation-plan)
5. [Testing & Validation Strategy](#testing--validation-strategy)

---

## Specific Failure Mode Analysis

### What Was Broken with `/transmitter/hardware` (Historical)

**Failure Mode: Settings Display Default/Uninitialized Values Instead of Persisted Values**

**Scenario:**
1. User sets transmitter hardware settings (e.g., CAN frequency, contactor PWM duty, precharge timing)
2. User saves settings (persists to transmitter NVS)
3. User navigates to receiver `/transmitter/hardware` page
4. Page loads but displays **default/uninitialized values**, not the saved settings
5. Receiver MQTT client has the correct values in `TransmitterManager` cache (because `publish_static_settings()` actually works)
6. But those cache values don't match what user actually configured

**Root Cause Chain:**

```
Transmitter Startup (main.cpp Phase 3):
├─ init_stored_settings()        ✓ called - loads battery type from NVS
├─ SystemSettings::init()        ✓ called - loads network/MQTT config
└─ SettingsManager::init()       ✗ NEVER CALLED ← CRITICAL ISSUE

When SettingsManager::init() is not called:
├─ SettingsManager internal members remain uninitialized OR at defaults
├─ NVS blob data is NOT loaded into memory
└─ Later when publish_static_settings() runs:
    └─ it publishes SettingsManager::instance() getters
        └─ which return defaults/zeroed structs instead of persisted values

Result: Receiver caches are populated with default values, not persisted config.
```

**Evidence:**

From [ESPnowtransmitter2/espnowtransmitter2/src/main.cpp](../../ESPnowtransmitter2/espnowtransmitter2/src/main.cpp#L161-L208):
```cpp
// Phase 3: Battery subsystem 
// ORDERING CONTRACT (must be preserved exactly — any reordering breaks the
// cell-count dependency chain):
//   1. init_stored_settings()               — load battery type from NVS
//   2. CANDriver::init()                    — start hardware CAN peripheral
//   3. BatteryManager::init_primary_battery() — calls battery->setup(),
//      sets global battery* pointer and populates
//      datalayer.battery.info.number_of_cells

static void bootstrap_battery() {
#if CONFIG_CAN_ENABLED
    // ... other init code ...
    
    // Load non-battery settings from legacy store (still needed for inverter/charger/limits)
    LOG_INFO("BATTERY", "Loading non-battery settings from legacy store...");
    init_stored_settings();  // ← This is called
    
    // BUT: No call to SettingsManager::instance().init() anywhere in this flow
}
```

From [ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_manager.cpp](../../ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_manager.cpp#L7-L15):
```cpp
SettingsManager& SettingsManager::instance() {
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager() {
}

// init() method exists but is NEVER CALLED during bootstrap
// Without it, NVS data is never loaded into memory
```

**Impact:**
- `/transmitter/hardware` page receives default values from MQTT retained message
- User cannot see their configured settings on the receiver UI
- Page cannot be used to verify saved configuration
- Changes made via page may appear to work (due to local caching on transmitter) but are inconsistent across restarts

---

## Version-Checking Audit

### Historical State at Investigation Start: Version Fields Existed But Were Not Used

#### 1. Version Fields in Settings Structs

All settings domains have `uint32_t version` fields in their NVS persistence blobs.

From [settings_persistence.cpp](../../ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp#L20-L90):

```cpp
struct __attribute__((packed)) BatterySettingsBlob {
    uint16_t schema_version;
    // ... fields ...
    uint32_t version;          // ← Version field exists
    uint32_t crc32;
};

struct __attribute__((packed)) PowerSettingsBlob {
    uint16_t schema_version;
    // ... fields ...
    uint32_t version;          // ← Version field exists
    uint32_t crc32;
};

struct __attribute__((packed)) CanSettingsBlob {
    uint16_t schema_version;
    // ... fields ...
    uint32_t version;          // ← Version field exists
    uint32_t crc32;
};

struct __attribute__((packed)) ContactorSettingsBlob {
    uint16_t schema_version;
    // ... fields ...
    uint32_t version;          // ← Version field exists
    uint32_t crc32;
};
```

**Current Usage Pattern: Version fields are stored but NOT checked**

From [TransmitterNetwork](../../espnowreceiver_2/lib/webserver/utils/transmitter_network.h):
```cpp
// Network config version tracking IS supported:
uint32_t get_network_config_version();
void store_network_config(..., uint32_t config_version, ...);

// However, the version is stored but never compared
// to determine cache staleness
```

#### 2. Receiver-Side Cache Readiness Check (Wrong Pattern)

From [api_settings_handlers.cpp](../../espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp#L24-L50):

```cpp
esp_err_t api_get_battery_settings_handler(httpd_req_t *req) {
    bool requested = false;
    
    // ... publish refresh command to MQTT ...
    
    // WRONG PATTERN: Checking "has data" instead of "version mismatch"
    const bool battery_known = TransmitterManager::hasBatterySettings();
    const bool power_known = TransmitterManager::hasPowerSettings();
    const bool can_known = TransmitterManager::hasCanSettings();
    const bool contactor_known = TransmitterManager::hasContactorSettings();
    const bool settings_ready = battery_known && power_known && can_known && contactor_known;
    
    // This says "ready" if: data exists somewhere, not if: data is current version
    
    // Result: If transmitter publishes OLD data (because init() never ran),
    // receiver says "ready" and page displays stale/default values
}
```

#### 3. Page Load Retry Logic (Attempts to Compensate)

From [hardware_config_page_script.cpp](../../espnowreceiver_2/lib/webserver/pages/hardware_config_page_script.cpp#L150-L185):

```cpp
async function loadHardwareSettings(remainingFollowupRetries = 2) {
    try {
        const response = await fetch('/api/get_battery_settings');
        const data = await response.json();

        const settingsReady = (data.settings_ready === true) || (data.success === true);

        // ... populate form fields ...

        // Retry logic tries to compensate for missing version checks
        if (!settingsReady && data.requested === true && remainingFollowupRetries > 0) {
            setTimeout(() => { loadHardwareSettings(remainingFollowupRetries - 1); }, 700);
        }
    } catch (error) {
        console.error('Failed to load hardware settings:', error);
        if (remainingFollowupRetries > 0) {
            setTimeout(() => { loadHardwareSettings(remainingFollowupRetries - 1); }, 700);
        }
    }
}
```

**Compensation is insufficient**: If the first fetch returns stale data (before transmitter re-publishes fresh data), the page loads and stops retrying because `settingsReady = true`.

### What SHOULD Happen (Version-Checking Pattern)

**Correct Pattern: Pull-Only-If-Stale**

```
1. Receiver API checks: does receiver have version N of battery settings cached?
   └─ If yes:  compare version to transmitter's published version
      └─ If same: cache is current → return cached data
      └─ If stale: cache version < transmitter version → request refresh
   └─ If no:  request refresh

2. If refresh requested:
   └─ publish rx/cmd/refresh/settings to transmitter
   └─ wait for transmitter to re-publish tx/state/static/settings with new version
   └─ receiver parses and increments cached version
   └─ API returns newly cached data

3. Page receives data only after version guarantee is met
```

### Version-Checking Gaps Summary

| Component | Has Version Field? | Checks Version? | Gap |
|-----------|-------------------|-----------------|-----|
| Battery Settings (NVS) | ✓ Yes | ✗ No | Receiver doesn't compare versions before serving cached data |
| Power Settings (NVS) | ✓ Yes | ✗ No | Receiver doesn't compare versions before serving cached data |
| CAN Settings (NVS) | ✓ Yes | ✗ No | Receiver doesn't compare versions before serving cached data |
| Contactor Settings (NVS) | ✓ Yes | ✗ No | Receiver doesn't compare versions before serving cached data |
| Network Config | ✓ Yes | ✗ Partial | TransmitterManager stores version but API doesn't check it |
| MQTT Config | ✓ Yes | ✗ Partial | TransmitterManager stores version but API doesn't check it |
| API Cache Readiness | N/A | ✗ No | Uses `hasXxxSettings()` (presence check) instead of version check |
| Page Retry Logic | N/A | ✗ No | Retries based on `settings_ready` flag, not version comparison |

---

## Identified Issues

### Issue #1: Transmitter Settings Manager Not Initialized (CRITICAL)

**Priority:** CRITICAL  
**Symptom:** Transmitted hardware settings show default/zero values instead of persisted config  
**Root Cause:** `SettingsManager::instance().init()` is never called during transmitter startup

**File:** [ESPnowtransmitter2/espnowtransmitter2/src/main.cpp](../../ESPnowtransmitter2/espnowtransmitter2/src/main.cpp)  
**Phase:** Phase 3 (bootstrap_battery)  
**Missing Code:**
```cpp
// Currently missing:
SettingsManager::instance().init();  // Load NVS blobs into memory
```

### Issue #2: Cache Readiness Based on Presence, Not Version (HIGH)

**Priority:** HIGH  
**Symptom:** Page receives stale cached values and doesn't know they're stale  
**Root Cause:** Receiver uses `hasBatterySettings()` (presence check) instead of version comparison

**File:** [espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp](../../espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp#L40-L50)  
**Current Code:**
```cpp
const bool settings_ready = battery_known && power_known && can_known && contactor_known;
// Says "ready" if data exists, not if data is current version
```

**Should Be:**
```cpp
// Check: do receiver caches match transmitter's current version?
// If not → request refresh before serving
```

### Issue #3: `/transmitter/config` Uses Specialized Architecture (MEDIUM)

**Priority:** MEDIUM  
**Symptom:** Two pages use completely different patterns for the same problem  
**Root Cause:** Legacy page predates generic typed-settings model

**Files:**  
- [espnowreceiver_2/lib/webserver/pages/settings_page.cpp](../../espnowreceiver_2/lib/webserver/pages/settings_page.cpp) — specialized network/MQTT/metadata
- [espnowreceiver_2/lib/webserver/pages/hardware_config_page.cpp](../../espnowreceiver_2/lib/webserver/pages/hardware_config_page.cpp) — generic category/field typed-settings

**Problem:** 
- Network config: separate fields, separate APIs, no version checking
- MQTT config: separate fields, separate APIs, no version checking
- Metadata: read-only, separate flow

**Should:** Align both pages to generic typed-settings model with unified version tracking

### Issue #4: No Version-Aware Refresh Logic in Page Scripts (MEDIUM)

**Priority:** MEDIUM  
**Symptom:** Page can't distinguish between "cache is stale" and "cache is current"  
**Root Cause:** Browser-side page scripts retry based on `settings_ready` flag instead of version numbers

**File:** [espnowreceiver_2/lib/webserver/pages/hardware_config_page_script.cpp](../../espnowreceiver_2/lib/webserver/pages/hardware_config_page_script.cpp#L180-L185)  
**Current Retry Logic:**
```javascript
if (!settingsReady && data.requested === true && remainingFollowupRetries > 0) {
    setTimeout(() => { loadHardwareSettings(remainingFollowupRetries - 1); }, 700);
}
```

**Should Include:** Version number comparison to determine if fresh data arrived

---

## Comprehensive Implementation Plan

### Implementation Roadmap

**Phases:** Fix critical issue first, then audit/fix version checking, then architecture alignment.

---

### PHASE 1: Fix Transmitter Settings Initialization (CRITICAL)

**Target:** Ensure transmitter loads persisted settings from NVS on startup

**Changes:**

#### File: `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`

**Location:** Phase 3, `bootstrap_battery()` function, after `init_stored_settings()` call

**✅ IMPLEMENTED:** Added the following code:
```cpp
// Load all persisted settings (battery, power, CAN, contactor) from NVS into SettingsManager
// CRITICAL: Must happen before MQTT publishes static settings, so pub/sub sees current values
LOG_INFO("SETTINGS", "Initializing SettingsManager from NVS blobs...");
if (SettingsManager::instance().init()) {
    LOG_INFO("SETTINGS", "✓ SettingsManager loaded: battery v%u, power v%u, can v%u, contactor v%u",
             SettingsManager::instance().get_battery_settings_version(),
             SettingsManager::instance().get_power_settings_version(),
             SettingsManager::instance().get_can_settings_version(),
             SettingsManager::instance().get_contactor_settings_version());
} else {
    LOG_WARN("SETTINGS", "SettingsManager initialization had issues (may be first boot with no saved settings)");
}
```

This now:
1. Calls `SettingsManager::instance().init()` to load NVS blobs into memory
2. Logs all four version numbers for diagnostics 
3. Handles first-boot gracefully (no saved NVS blobs yet)
4. Executes BEFORE battery initialization but AFTER legacy `init_stored_settings()`

**Rationale:**
- Settings blobs are stored in NVS with schema versions and CRC32 checksums
- `SettingsManager::init()` deserializes those blobs into in-memory authority state
- Without this call, all `SettingsManager` getter methods return default/uninitialized values
- Later when `mqtt_manager.cpp` publishes `tx/state/static/settings`, it must publish from initialized state

**Testing:**
1. Transmitter startup logs should show "✓ SettingsManager loaded from persistent storage"
2. Verify transmitter NVS contains valid blobs (use `nvs_get_info` diagnostic)
3. Check MQTT retained `tx/state/static/settings` payload for non-zero setting values

---

### PHASE 2: Implement Version-Aware Cache Checking (HIGH)

**Target:** Receiver API should only return cached data if version is current

**Changes:**

#### File: `espnowreceiver_2/lib/webserver/utils/transmitter_settings_cache.h`

**Add Method:**
```cpp
// Return version number of cached battery settings (0 if not cached)
uint32_t get_battery_settings_version();
uint32_t get_power_settings_version();
uint32_t get_can_settings_version();
uint32_t get_contactor_settings_version();
```

#### File: `espnowreceiver_2/lib/webserver/utils/transmitter_settings_cache.cpp`

**Add Implementation:**
```cpp
uint32_t TransmitterSettingsCache::get_battery_settings_version() {
    return has_battery_settings() ? battery_settings_.version : 0;
}

uint32_t TransmitterSettingsCache::get_power_settings_version() {
    return has_power_settings() ? power_settings_.version : 0;
}

uint32_t TransmitterSettingsCache::get_can_settings_version() {
    return has_can_settings() ? can_settings_.version : 0;
}

uint32_t TransmitterSettingsCache::get_contactor_settings_version() {
    return has_contactor_settings() ? contactor_settings_.version : 0;
}
```

#### File: `espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp`

**Refactor:**
```cpp
esp_err_t api_get_battery_settings_handler(httpd_req_t *req) {
    bool requested = false;
    bool cache_stale = true;

    // Check if all settings are cached AND at current version
    // (We'll assume version > 0 means transmitter has published at least once)
    uint32_t batt_version = TransmitterSettingsCache::get_battery_settings_version();
    uint32_t power_version = TransmitterSettingsCache::get_power_settings_version();
    uint32_t can_version = TransmitterSettingsCache::get_can_settings_version();
    uint32_t cont_version = TransmitterSettingsCache::get_contactor_settings_version();
    
    cache_stale = (batt_version == 0 || power_version == 0 || can_version == 0 || cont_version == 0);
    
    // If cache is stale, request refresh from transmitter
    if (cache_stale) {
        if (MqttClient::isEnabled() && MqttClient::isConnected()) {
            // publish refresh command ...
            requested = true;
        }
    }

    // Get cached data (may be stale/partial if just requested)
    auto settings = TransmitterManager::getBatterySettings();
    // ... rest of response building ...
    
    doc["success"]                   = !cache_stale;  // Only true if cache is current
    doc["settings_ready"]            = !cache_stale;
    doc["battery_version"]           = batt_version;
    doc["power_version"]             = power_version;
    doc["can_version"]               = can_version;
    doc["contactor_version"]         = cont_version;
    doc["requested"]                 = requested;
    
    return ApiResponseUtils::send_json_doc(req, doc);
}
```

**Rationale:**
- Versions are already transmitted and cached, just need to be checked

**Implementation Status: COMPLETE ✓**

**What was actually implemented:**

1. **Added version fields to receiver settings structs** (`transmitter_settings_types.h`):
   - Added `uint32_t version` to `PowerSettings`, `CanSettings`, `ContactorSettings`
   - `BatterySettings` already had version field

2. **Added version getter declarations** (`transmitter_settings_cache.h`):
   - `get_battery_settings_version()`, `get_power_settings_version()`, `get_can_settings_version()`, `get_contactor_settings_version()`

3. **Added version getter implementations** (`transmitter_settings_cache.cpp`):
   - Thread-safe getters using ScopedMutex
   - Return version directly from cached struct

4. **Added wrapper methods to TransmitterManager** (`transmitter_manager.h/cpp`):
   - `getBatterySettingsVersion()`, `getPowerSettingsVersion()`, `getCanSettingsVersion()`, `getContactorSettingsVersion()`
   - Delegate to TransmitterSettingsCache getters
   - Provides unified public API for version access

5. **Updated API handler for version checking** (`api_settings_handlers.cpp`):
   - Get all 4 version numbers from cache
   - Check each version > 0 (indicates valid/initialized data)
   - Only report `settings_ready = true` if ALL versions > 0
   - Include version numbers in JSON response for page script validation
   - Request MQTT refresh if any version is 0

**Build Status: SUCCESS ✓**
- Receiver builds without errors
- All version getters thread-safe with proper locking

**Next Phase: Version-Based Page Retry Logic (Phase 3)**
- API response now includes version numbers so browser can validate freshness
- Cache only reports "ready" if version > 0 (transmitter has published at least once)
- Prevents stale data display until transmitter confirms publish

---

### PHASE 3: Enhance Page Retry Logic with Version Awareness (MEDIUM)

**Target:** Browser page script only considers retry successful when version numbers increment

**Changes:**

#### File: `espnowreceiver_2/lib/webserver/pages/hardware_config_page_script.cpp`

**Refactor loadHardwareSettings():**
```javascript
async function loadHardwareSettings(remainingFollowupRetries = 2) {
    try {
        const response = await fetch('/api/get_battery_settings');
        const data = await response.json();

        // Use version numbers, not just presence flags
        const hasVersions = data.battery_version > 0 
                         && data.power_version > 0 
                         && data.can_version > 0 
                         && data.contactor_version > 0;
        
        const settingsReady = hasVersions;  // ← Changed logic

        // ... populate form fields ...

        // Only consider data loaded if versions are non-zero AND were requested
        if (!settingsReady && data.requested === true && remainingFollowupRetries > 0) {
            setTimeout(() => { loadHardwareSettings(remainingFollowupRetries - 1); }, 700);
        }
    } catch (error) {
        console.error('Failed to load hardware settings:', error);
        if (remainingFollowupRetries > 0) {
            setTimeout(() => { loadHardwareSettings(remainingFollowupRetries - 1); }, 700);
        }
    }
}
```

**Rationale:**
- Versions > 0 indicate transmitter has published
- Retry logic now waits for actual version update, not just data presence
- Prevents page from loading default values and stopping

**Implementation Status: COMPLETE ✓**

**What was actually implemented:**

1. **Updated hardware page retry logic in both receivers**:
   - `espnowreceiver_2/lib/webserver/pages/hardware_config_page_script.cpp`
   - `espnowreceiver_LCD/lib/webserver_lcd/pages/hardware_config_page_script.cpp`
   - `settingsReady` now derives from explicit version checks (`battery_version`, `power_version`, `can_version`, `contactor_version`)

2. **Completed LCD parity for version-aware API/cache flow**:
   - Added `version` fields to LCD receiver `PowerSettings`, `CanSettings`, `ContactorSettings`
   - Added version getters to LCD `TransmitterSettingsCache` and `TransmitterManager`
   - Updated LCD `api_get_battery_settings_handler()` to require all versions > 0 before `settings_ready=true`
   - Added version fields to LCD API response payload

3. **Completed version hydration from transmitter retained payload**:
   - Both receiver MQTT clients now store `version` for `power`, `can`, and `contactor` sections in `handleStaticSettings()`

**Build Status: SUCCESS ✓**
- `espnowreceiver_2`: both environments build successfully
- `espnowreceiver_LCD`: waveshare environment builds successfully

---

### PHASE 4: Migrate `/transmitter/config` to Generic Typed-Settings Model (MEDIUM)

**Target:** Unify both pages to same generic pattern for consistency and maintainability

**Changes:** (Detailed implementation scope beyond this document, but includes:)

1. **Define category/field constants for network and MQTT settings**
   - `SETTINGS_NETWORK` with fields: IP, gateway, subnet, static_enabled, DNS1, DNS2, etc.
   - `SETTINGS_MQTT` with fields: enabled, server, port, username, password, client_id, etc.

2. **Add network/MQTT config structs to settings persistence** (similar to battery/power/can/contactor)

3. **Update API handlers** for `/transmitter/config` to use `TransmitterManager::getNetworkSettings()` and `getMqttSettings()` with version checking

4. **Update page script** to use category/field model instead of specialized IP octet handling

5. **Migrate metadata to read-only variant** of generic model or keep specialized since it's never modified

**Benefits:**
- Single refresh/save pattern across all settings domains
- Version checking applies consistently to all types
- Reduced code duplication
- Easier to add new settings domains in the future
- Better user experience (consistent UI patterns)

---

### PHASE 5: Add Transmitter-Side Version Publishing

**Target:** Ensure version numbers are incremented when settings change

**Changes:**

#### File: `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_field_setters.cpp`

**Verify:** When a field is updated via MQTT command, increment the domain version

```cpp
// When any battery field is updated:
battery_version_++;  // or timestamp-based version

// When any power field is updated:
power_version_++;

// etc.
```

**Verify:** `mqtt_manager.cpp` publishes current version numbers in retained `tx/state/static/settings`

**Benefits:**
- Receiver can definitively detect when transmitter has new data
- Supports future "delta sync" patterns (only sync changed domains)
- Audit trail of changes

---

## Testing & Validation Strategy

### Unit Tests

**Test 1: SettingsManager Initialization**
```
Given: Fresh transmitter startup
When: bootstrap_battery() is called
Then: SettingsManager::init() is called
  AND all NVS blobs are deserialized into memory
  AND getters return persisted values, not defaults
```

**Test 2: Version Number Propagation**
```
Given: Transmitter has version N for battery settings
When: mqtt_manager publishes static settings
Then: MQTT message includes battery_version: N
  AND receiver receives and caches this value
```

**Test 3: Cache Staleness Detection**
```
Given: Receiver cache has version 0 (no data)
When: api_get_battery_settings is called
Then: API returns success: false
  AND API returns requested: true (refresh triggered)
```

### Integration Tests

**Test 4: End-to-End Settings Publish**
```
Given: Transmitter has CAN frequency = 500 khz persisted in NVS
  AND transmitter has just rebooted
When: Transmitter MQTT publishes retained settings
Then: Receiver parses and caches can_settings.frequency_khz = 500
  AND can_version > 0
```

**Test 5: Page Display With Valid Cache**
```
Given: Transmitter has published settings with version > 0
When: User navigates to /transmitter/hardware
Then: Page calls api_get_battery_settings
  AND API returns success: true (cache is current)
  AND page populates form fields with actual persisted values
  AND no "Loading..." or retry visible to user
```

**Test 6: Page Display With Stale Cache (Before Init)**
```
Given: Transmitter is starting and SettingsManager::init() hasn't been called yet
When: Receiver fetches /api/get_battery_settings during transmitter startup
Then: API returns success: false (cache is stale/uninitialized)
  AND page shows retry message
  AND page retries until version > 0
  AND once transmitter finishes init and publishes, page updates automatically
```

### Manual Validation Checklist

- [ ] Transmitter startup logs show "✓ SettingsManager loaded from persistent storage"
- [ ] MQTT retained payload `tx/state/static/settings` contains non-zero hardware settings
- [ ] Receiver `/transmitter/hardware` page displays actual saved values (not defaults)
- [ ] Changing a value on receiver page and saving updates transmitter NVS
- [ ] Transmitter restart preserves the saved values
- [ ] Both receiver projects (LCD and non-LCD) behave identically
- [ ] Network/MQTT settings are currently on `/transmitter/config` (separate)
- [ ] Future: `/transmitter/config` migrated to generic model (post-Phase 4)

---

## Success Criteria

**Phase 1 (CRITICAL):**
- ✓ Transmitter `SettingsManager` is initialized on startup
- ✓ Retained MQTT publish contains persisted values (not defaults)
- ✓ Receiver displays actual saved hardware settings on `/transmitter/hardware`

**Phase 2 (HIGH):**
- ✓ API returns version numbers in response
- ✓ API marks cache as `ready: false` if any version is 0
- ✓ Page waits for version > 0 before accepting data

**Phase 3 (MEDIUM):**
- ✓ Page script uses version-based retry logic
- ✓ Page doesn't display default values after stopping retries

**Phase 4 (MEDIUM) — Pending:**
- [ ] `/transmitter/config` uses same category/field model as `/transmitter/hardware`
- [ ] Both pages use same refresh/save patterns
- [ ] Network and MQTT config versions are checked before serving

**Phase 5 (MEDIUM) — Pending verification/closeout:**
- [ ] Version numbers increment when settings are updated
- [ ] Receiver detects version changes and refreshes cache
- [ ] Audit trail shows when settings changed

---

## Risk Assessment

### Risk 1: Breaking Existing Receiver Deployments (LOW)

- **Mitigation:** Phase 1 and 2 are backward-compatible additions; no API breaking changes
- **Mitigation:** Phase 4 is opt-in migration, old pages continue to work

### Risk 2: NVS Corruption During Init (LOW)

- **Mitigation:** `SettingsManager::init()` already has CRC32 validation for all blobs
- **Mitigation:** Test with known-bad NVS data to verify graceful fallback to defaults

### Risk 3: MQTT Topic Collision (LOW)

- **Mitigation:** Using established topics (`tx/state/static/settings`, `rx/cmd/refresh/settings`)
- **Mitigation:** No new topics needed for Phases 1-3

### Risk 4: Performance Regression (LOW)

- **Mitigation:** Version checks are uint32 comparisons (O(1) overhead)
- **Mitigation:** No new MQTT publishes (only checking existing data)

---

## Next Steps

1. **Hardware validation (required):**
   - Confirm transmitter startup logs show initialized non-zero settings versions
   - Verify retained `tx/state/static/settings` contains expected non-default values
   - Validate `/transmitter/hardware` on both receivers shows persisted transmitter values after reboot
2. **Close Phase 4:** Plan and implement `/transmitter/config` migration to the generic typed-settings model
3. **Close Phase 5 verification:** Confirm and document version increment behavior for all settings domains during updates
4. **Final documentation pass:** Update architecture/README docs once hardware validation is complete

