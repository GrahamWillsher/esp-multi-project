# Inverter Selection Migration - Quick Implementation Checklist

**Target**: Move inverter selection from `/inverter_settings.html` to `/transmitter/inverter`  
**Pattern**: Follow battery settings at `/transmitter/battery`  
**Estimated Time**: 3-4 hours

---

## Pre-Implementation

- [ ] Read [INVERTER_SELECTION_MIGRATION_REPORT.md](INVERTER_SELECTION_MIGRATION_REPORT.md)
- [ ] Backup current codebase / commit to git
- [ ] Verify battery settings page works as reference
- [ ] Test existing `/api/get_inverter_types` endpoint

---

## Phase 1: Create New Page Files

### File 1: inverter_settings_page.h
**Location**: `lib/webserver/pages/inverter_settings_page.h`

- [ ] Create new file
- [ ] Copy header guards pattern from battery_settings_page.h
- [ ] Add `esp_err_t register_inverter_settings_page(httpd_handle_t server);` declaration
- [ ] Add documentation comment

### File 2: inverter_settings_page.cpp
**Location**: `lib/webserver/pages/inverter_settings_page.cpp`

- [ ] Create new file
- [ ] Add includes (page_generator.h, transmitter_manager.h, Arduino.h)
- [ ] Copy structure from battery_settings_page.cpp
- [ ] Modify HTML sections:
  - [ ] Update breadcrumb: Dashboard > Transmitter > Inverter Settings
  - [ ] Change title to "Inverter Settings"
  - [ ] Add "Current Configuration" info box
  - [ ] Create "Inverter Protocol Selection" card
  - [ ] Add ⚠️ reboot warning
  - [ ] Add dropdown: `<select id='inverterType'>`
  - [ ] Add protocol info display div
  - [ ] Add save button
- [ ] Modify JavaScript:
  - [ ] Change `loadBatteryTypes()` → `loadInverterTypes()`
  - [ ] Fetch from `/api/get_inverter_types`
  - [ ] Change `loadCurrentBatteryType()` → `loadCurrentInverterType()`
  - [ ] Use `data.inverter_type` instead of `data.battery_type`
  - [ ] Change `updateBatteryType()` → `updateInverterType()`
  - [ ] Change `saveBatteryType()` → `saveInverterType()`
  - [ ] POST to `/api/set_inverter_type`
  - [ ] Update "Current Protocol" display on load
- [ ] Add registration function: `register_inverter_settings_page()`
  - [ ] URI: `/transmitter/inverter`
  - [ ] Method: HTTP_GET
  - [ ] Handler: inverter_settings_handler

---

## Phase 2: Add Navigation Card

### File: transmitter_hub_page.cpp
**Location**: `lib/webserver/pages/transmitter_hub_page.cpp`

- [ ] Locate navigation cards section (after battery settings, before monitors)
- [ ] Copy battery settings card HTML
- [ ] Modify for inverter:
  - [ ] Change href: `/transmitter/battery` → `/transmitter/inverter`
  - [ ] Change icon: 🔋 → ⚡
  - [ ] Change title: "Battery Settings" → "Inverter Settings"
  - [ ] Change subtitle: "Capacity, Limits, Chemistry" → "Protocol, Communication"
- [ ] Verify grid layout remains consistent (2 columns)

**Code to Insert** (around line 120):
```cpp
<!-- Inverter Settings -->
<a href='/transmitter/inverter' style='text-decoration: none;'>
    <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #2196F3;'
         onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#42A5F5"'
         onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#2196F3"'>
        <div style='font-size: 36px; margin: 10px 0;'>⚡</div>
        <div style='font-weight: bold; color: #2196F3; font-size: 16px;'>Inverter Settings</div>
        <div style='font-size: 12px; color: #888; margin-top: 8px;'>Protocol, Communication</div>
    </div>
</a>
```

---

## Phase 3: Register Page Handler

### File: main.cpp
**Location**: `src/main.cpp`

- [ ] Find webserver initialization section
- [ ] Add include: `#include "../lib/webserver/pages/inverter_settings_page.h"`
- [ ] Locate where battery_settings_page is registered
- [ ] Add registration call after battery settings:
  ```cpp
  // Register inverter settings page
  if (register_inverter_settings_page(server) == ESP_OK) {
      Serial.println("[HTTP] Registered /transmitter/inverter");
  } else {
      Serial.println("[HTTP] Failed to register /transmitter/inverter");
  }
  ```

---

## Phase 4: Build & Test

### Compilation
- [ ] Clean build: `pio run -t clean`
- [ ] Full build: `pio run`
- [ ] Verify no compilation errors
- [ ] Check binary size (should increase ~20KB)

### Flash to Device
- [ ] Upload firmware: `pio run -t upload`
- [ ] Monitor serial output: `pio device monitor`
- [ ] Verify "[HTTP] Registered /transmitter/inverter" message appears

---

## Phase 5: Functional Testing

### Basic Navigation
- [ ] Open receiver web UI
- [ ] Navigate to `/transmitter` hub
- [ ] Verify "Inverter Settings" card appears with ⚡ icon
- [ ] Click card → redirects to `/transmitter/inverter`
- [ ] Verify breadcrumb shows: Dashboard > Transmitter > Inverter Settings
- [ ] Check page title: "Inverter Settings"

### Dropdown Functionality
- [ ] Dropdown populates with inverter types
- [ ] Verify alphabetical sorting
- [ ] Check current selection is pre-selected
- [ ] Change selection → verify save button enables
- [ ] Verify button text: "Save Inverter Type"
- [ ] Select original value → button shows "Nothing to Save" (disabled)

### Protocol Info Display
- [ ] Select different inverter type
- [ ] Verify protocol info updates below dropdown
- [ ] Check protocol name and ID display correctly

### Save Functionality
- [ ] Change to different inverter type
- [ ] Click "Save Inverter Type"
- [ ] Verify button shows "Saving..." state (orange)
- [ ] Check browser console for API call (no errors)
- [ ] Verify success alert with reboot warning
- [ ] Button shows "✓ Saved!" (green)
- [ ] Page reloads after 2 seconds

### Backend Verification
- [ ] Check serial monitor on receiver → "Inverter type X sent to transmitter"
- [ ] Check serial monitor on transmitter → "Received component type selection"
- [ ] Verify transmitter saves to NVS
- [ ] Verify transmitter reboots (30 seconds)
- [ ] After reboot, reload receiver page
- [ ] Verify new inverter type is still selected

---

## Phase 6: Edge Case Testing

### Error Handling
- [ ] Disconnect transmitter
- [ ] Try to save inverter type
- [ ] Verify warning message (receiver saves locally but transmitter unreachable)
- [ ] Reconnect transmitter
- [ ] Verify sync works after reconnection

### Network Issues
- [ ] Simulate slow network (browser DevTools)
- [ ] Verify save doesn't timeout prematurely
- [ ] Check error messages are user-friendly

### Validation
- [ ] Try to POST invalid type ID (e.g., 999)
- [ ] Verify API rejects with error message
- [ ] Try empty POST body
- [ ] Verify proper error handling

### Rapid Changes
- [ ] Change selection multiple times quickly
- [ ] Click save before previous save completes
- [ ] Verify only last selection is saved
- [ ] Check no race conditions

---

## Phase 7: Integration Testing

### Complete User Flow
- [ ] Fresh receiver boot
- [ ] Navigate: Dashboard → Transmitter Hub → Inverter Settings
- [ ] Load page → current type pre-selected
- [ ] Change to different protocol
- [ ] Save → transmitter reboots
- [ ] Wait 30 seconds
- [ ] Reload page → new type selected
- [ ] Verify inverter operates with new protocol

### Cross-Page Consistency
- [ ] Check `/inverter_settings.html` (old page) still works (if not removed)
- [ ] Verify inverter specs page shows correct protocol
- [ ] Check dashboard reflects correct inverter type

---

## Phase 8: Documentation & Cleanup

### Update Documentation
- [ ] Update README with new page location
- [ ] Add screenshot of new inverter settings card
- [ ] Document API endpoints used
- [ ] Update user guide if exists

### Code Comments
- [ ] Add file header comments
- [ ] Document JavaScript functions
- [ ] Add inline comments for complex logic

### Git Commit
- [ ] Stage all changes: `git add -A`
- [ ] Commit with message:
  ```
  Add inverter settings page to transmitter hub
  
  - Created /transmitter/inverter page with protocol selection
  - Added navigation card to transmitter hub
  - Follows battery settings pattern
  - Sends selection to transmitter via ESP-NOW
  - Stored in transmitter NVS with automatic reboot
  ```
- [ ] Push to repository

---

## Phase 9: Optional Enhancements

### Future Improvements (Not Required Now)
- [ ] Add inverter-specific configuration fields
- [ ] Display inverter capabilities per protocol
- [ ] Show connection status indicator
- [ ] Add protocol change history log
- [ ] Deprecate old `/inverter_settings.html` page

---

## Rollback Plan (If Needed)

If issues occur:
1. [ ] Remove inverter card from transmitter_hub_page.cpp
2. [ ] Comment out registration in main.cpp
3. [ ] Rebuild and reflash
4. [ ] Old functionality still works via API

---

## Success Criteria

✅ All checklist items complete  
✅ No compilation errors  
✅ Page accessible via navigation  
✅ Dropdown populates correctly  
✅ Save functionality works end-to-end  
✅ Transmitter receives and stores value  
✅ Transmitter reboots automatically  
✅ New type persists after reboot  

---

## Time Tracking

| Phase | Estimated | Actual | Notes |
|-------|-----------|--------|-------|
| Phase 1 | 1 hour | | Create page files |
| Phase 2 | 15 min | | Add nav card |
| Phase 3 | 10 min | | Register handler |
| Phase 4 | 20 min | | Build & flash |
| Phase 5 | 45 min | | Functional testing |
| Phase 6 | 30 min | | Edge case testing |
| Phase 7 | 30 min | | Integration testing |
| Phase 8 | 20 min | | Documentation |
| **Total** | **3.5 hours** | | |

---

**Status**: Ready to begin  
**Next Action**: Start Phase 1 - Create inverter_settings_page.h
