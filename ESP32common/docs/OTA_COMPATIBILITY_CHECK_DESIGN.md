# OTA Firmware Compatibility Check - Current Status & Required Changes

**Date:** February 4, 2026  
**Project:** ESP-NOW Dual-Device System (Receiver + Transmitter)

---

## 1. Current Implementation Status

### 1.1 Runtime Compatibility Check (✅ IMPLEMENTED)

**Location:** `firmware_version.h`

**Function:**
```cpp
inline bool isVersionCompatible(uint32_t otherVersion) {
    return (otherVersion >= MIN_COMPATIBLE_VERSION);
}
```

**How it works:**
- Each device compares the **running firmware** version of the other device
- Checks if `otherVersion >= MIN_COMPATIBLE_VERSION` (currently set to 10000 = v1.0.0)
- Used for ESP-NOW protocol compatibility
- Displayed on OTA page as "✅ Compatible" or "⚠️ Version mismatch"

**Current Behavior:**
- **Receiver** checks transmitter's **running** version via `/api/version` endpoint
- **OTA Page** displays compatibility status every 5 seconds (polling)
- **No check** is performed on firmware **files** before upload

### 1.2 OTA Page File Selection (⚠️ INCOMPLETE)

**Current State:**
- Users can select **one file at a time** for each device
- Each device can be updated **independently**
- **No validation** of firmware file version before upload
- **No requirement** to select both files before updating
- Upload button activates **immediately** when any file is selected

**Missing Features:**
1. ❌ No extraction of version from selected firmware files
2. ❌ No pre-upload compatibility validation (minimum version check)
3. ❌ No cross-device compatibility check when both files selected
4. ❌ No clear indication of version compatibility status before upload

---

## 2. Required Changes

### 2.1 Goals

1. **Extract version from firmware files** before upload
2. **Validate each firmware** meets minimum compatibility requirements
3. **Allow single-device updates** when only one firmware is selected
4. **Validate cross-device compatibility** when both firmwares are selected
5. **Prevent incompatible updates** with clear user warnings

### 2.2 Implementation Strategy

#### Option A: Client-Side Validation (JavaScript - Recommended)

**Pros:**
- Immediate feedback to user
- No server processing required
- Better UX (instant validation)

**Cons:**
- Requires parsing binary firmware file in JavaScript
- ESP32 firmware format knowledge needed
- More complex client-side code

**How it works:**
1. User selects receiver firmware file → extract version from binary
   - Validate version >= MIN_COMPATIBLE_VERSION
   - Enable "Update Receiver" button if valid
2. User selects transmitter firmware file → extract version from binary
   - Validate version >= MIN_COMPATIBLE_VERSION
   - Enable "Update Transmitter" button if valid
3. If BOTH files selected → additional validation:
   - Cross-compatibility check (major version match, etc.)
   - Enable individual buttons only if cross-compatible
   - Optionally enable "Update Both Devices" button
4. Display compatibility status and warnings clearly

#### Option B: Server-Side Validation (Backend API)

**Pros:**
- Simpler client code
- More reliable version extraction
- Can use existing C++ firmware_version.h logic

**Cons:**
- Requires file upload before validation
- Slower user feedback
- More server resources

**How it works:**
1. User selects files
2. JavaScript uploads files to `/api/validate_firmware` endpoint
3. Server parses ESP32 app descriptor to extract version
4. Server returns validation result
5. Client enables/disables upload based on result

#### Option C: Hybrid Approach (Recommended)

**Combination:**
1. **Filename convention** - Require versioned filenames (e.g., `firmware_1_2_3.bin`)
2. **Parse filename** in JavaScript to extract version quickly
3. **Server-side verification** during actual upload as safety check
4. **User override option** for advanced users (with warning)

---

## 3. Proposed Implementation Plan

### Phase 1: Filename-Based Validation (Quick Win)

**Changes Required:**

1. **Update build script** to create versioned filenames
   - ✅ Already done: `version_firmware.py` creates `firmware_1_0_0.bin`

2. **Update OTA Page JavaScript:**
```javascript
function parseVersionFromFilename(filename) {
    // Match: firmware_X_Y_Z.bin or receiver_X_Y_Z.bin
    const match = filename.match(/(\d+)_(\d+)_(\d+)\.bin$/);
    if (match) {
        return {
            major: parseInt(match[1]),
            minor: parseInt(match[2]),
            patch: parseInt(match[3]),
            number: (parseInt(match[1]) * 10000) + 
                    (parseInt(match[2]) * 100) + 
                    parseInt(match[3])
        };
    }
    return null;
}

function validateCompatibility(receiverVer, transmitterVer) {
    const MIN_COMPATIBLE = 10000; // v1.0.0
    
    // Both must meet minimum
    if (receiverVer.number < MIN_COMPATIBLE) {
        return { valid: false, reason: 'Receiver version too old' };
    }
    if (transmitterVer.number < MIN_COMPATIBLE) {
        return { valid: false, reason: 'Transmitter version too old' };
    }
    
    // Major versions must match (optional - depends on protocol)
    if (receiverVer.major !== transmitterVer.major) {
        return { 
            valid: false, 
            reason: 'Major version mismatch (Receiver: ' + 
                    receiverVer.major + ', Transmitter: ' + 
                    transmitterVer.major + ')' 
        };
    }
    
    return { valid: true };
}
```

3. **UI Changes:**
   - Add **single "Update Both Devices"** button (disabled by default)
   - Show **compatibility status** for selected files
   - Display **parsed versions** from filenames
   - Enable button only when both files selected and compatible

**Example UI (Both Files Selected):**
```
┌─────────────────────────────────────────────┐
│ 📊 Selected Firmware Files                  │
├─────────────────────────────────────────────┤
│ Receiver: firmware_1_2_0.bin (v1.2.0) ✅    │
│ Transmitter: firmware_1_2_1.bin (v1.2.1) ✅ │
│                                             │
│ Cross-Compatibility: ✅ Compatible (Major v1)│
└─────────────────────────────────────────────┘

[Update Receiver] [Update Transmitter] [Update Both] ← All enabled
```

**Example UI (Single File Selected):**
```
┌─────────────────────────────────────────────┐
│ 📊 Selected Firmware Files                  │
├─────────────────────────────────────────────┤
│ Receiver: firmware_1_2_0.bin (v1.2.0) ✅    │
│ Transmitter: (none)                         │
└─────────────────────────────────────────────┘

[Update Receiver] ← Enabled
[Update Transmitter] ← Disabled (no file)
```

### Phase 2: Binary Parsing (Enhanced Security)

**For advanced validation:**

1. **Parse ESP32 App Descriptor** from binary
   - Located at offset 0x20 in ESP32 firmware
   - Contains magic number, version, project name, etc.
   
2. **Read first few KB** of firmware file in JavaScript:
```javascript
function parseESP32AppDescriptor(file) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = function(e) {
            const buffer = new Uint8Array(e.target.result);
            
            // ESP32 app descriptor at offset 0x20
            const magic = buffer[0x20] | (buffer[0x21] << 8) | 
                         (buffer[0x22] << 16) | (buffer[0x23] << 24);
            
            if (magic === 0xABCD5432) { // ESP32 magic
                const version = String.fromCharCode.apply(null, 
                    buffer.slice(0x30, 0x50)).split('\0')[0];
                resolve({ version });
            } else {
                reject('Invalid ESP32 firmware');
            }
        };
        reader.readAsArrayBuffer(file.slice(0, 1024));
    });
}
```

3. **Server-side final validation** during upload

---

## 4. Recommended User Workflow

### Current Workflow (Problematic):
```
1. User selects receiver firmware
2. User clicks "Upload Receiver" → Receiver updates
3. (Optional) User selects transmitter firmware
4. (Optional) User clicks "Upload Transmitter" → Transmitter updates
❌ Risk: No validation of firmware version compatibility
```

### Proposed Workflow A (Single Device Update):
```
1. User selects receiver firmware → Version extracted and displayed
2. System validates firmware:
   ✅ Version >= MIN_COMPATIBLE_VERSION → "Update Receiver" enabled
   ❌ Version too old → Error shown, button disabled
3. User clicks "Update Receiver" → Receiver updates safely
```

### Proposed Workflow B (Dual Device Update):
```
1. User selects receiver firmware → Version displayed
   ✅ Version valid → "Update Receiver" enabled
2. User selects transmitter firmware → Version displayed
   ✅ Version valid → "Update Transmitter" enabled
3. System validates cross-compatibility:
   ✅ Compatible (major versions match) → Both buttons remain enabled
   ❌ Incompatible → Warning shown, buttons disabled with explanation
4. User can choose:
   - "Update Receiver" only
   - "Update Transmitter" only  
   - "Update Both Devices" (if available)
```

---

## 5. Configuration Requirements

### firmware_version.h Updates

**Add compatibility rules:**
```cpp
// Minimum compatible version
#define MIN_COMPATIBLE_VERSION 10000  // v1.0.0

// Major version compatibility rule
#define REQUIRE_MAJOR_MATCH true      // Both devices must have same major version

// Minor version compatibility rule  
#define REQUIRE_MINOR_MATCH false     // Minor versions can differ

// Helper: Check if two versions are compatible with each other
inline bool areVersionsCompatible(uint32_t version1, uint32_t version2) {
    uint16_t major1, minor1, patch1;
    uint16_t major2, minor2, patch2;
    
    getVersionComponents(version1, major1, minor1, patch1);
    getVersionComponents(version2, major2, minor2, patch2);
    
    // Both must meet minimum
    if (version1 < MIN_COMPATIBLE_VERSION || version2 < MIN_COMPATIBLE_VERSION) {
        return false;
    }
    
    #if REQUIRE_MAJOR_MATCH
    if (major1 != major2) {
        return false;
    }
    #endif
    
    #if REQUIRE_MINOR_MATCH
    if (minor1 != minor2) {
        return false;
    }
    #endif
    
    return true;
}
```

---

## 6. Testing Scenarios

### Test Case 1: Both Files Compatible
- **Receiver:** firmware_1_2_0.bin
- **Transmitter:** firmware_1_2_1.bin
- **Expected:** ✅ "Update Both Devices" enabled
- **Result:** Both devices update successfully

### Test Case 2: Major Version Mismatch
- **Receiver:** firmware_2_0_0.bin
- **Transmitter:** firmware_1_5_0.bin
- **Expected:** ❌ Error: "Major version mismatch"
- **Result:** Button disabled, user cannot proceed

### Test Case 3: Below Minimum Version
- **Receiver:** firmware_1_2_0.bin
- **Transmitter:** firmware_0_9_5.bin
- **Expected:** ❌ Error: "Transmitter version too old"
- **Result:** Button disabled

### Test Case 4: Only One File Selected
- **Receiver:** firmware_1_2_0.bin (v1.2.0 >= MIN)
- **Transmitter:** (none)
- **Expected:** ✅ "Update Receiver" enabled
- **Result:** Single-device update allowed, transmitter unaffected

---

## 7. Implementation Priority

### High Priority (Immediate):
1. ✅ Versioned firmware filenames (already done via script)
2. ⚠️ Filename parsing in OTA page
3. ⚠️ Minimum version validation for each file
4. ⚠️ Cross-device compatibility validation when both files selected
5. ⚠️ Individual device update buttons + optional "Update Both" button

### Medium Priority (Phase 2):
5. Binary parsing for version verification
6. Server-side validation endpoint
7. Rollback mechanism if one device fails

### Low Priority (Future):
8. Delta updates (only changed code)
9. Staged rollout (one device at a time with validation)
10. Automatic compatibility testing before production

---

## 8. Current Risks

### Without Proposed Changes:
❌ **No validation of firmware files** → wrong firmware uploaded  
❌ **No minimum version check** → incompatible firmware can be uploaded  
❌ **No cross-compatibility check** → incompatible versions when updating both  
❌ **Manual filename checking** → human error  

### After Implementation:
✅ **Automatic version validation** → prevents incompatible uploads  
✅ **Clear error messages** → user knows what's wrong  
✅ **Single-device updates allowed** → flexibility when needed  
✅ **Cross-compatibility enforced** → safe when updating both devices  
✅ **Versioned artifacts** → easy to identify builds  

---

## 9. Next Steps

**Recommendation:** Implement **Phase 1** (filename-based validation) immediately.

**Required Code Changes:**
1. `ota_page.cpp` - Update JavaScript for dual-file selection and validation
2. Add `areVersionsCompatible()` helper to `firmware_version.h`
3. Test with versioned firmware files from build script

**Estimated Effort:** 2-3 hours development + 1 hour testing

**Ready to proceed?** Please review this document and confirm approach.
