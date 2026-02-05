# Firmware Metadata Implementation Plan v2

## Objective
Re-implement firmware metadata embedding in .rodata section of .bin files for **display purposes only** (no validation/enforcement).

## What We're Building

### 1. Embedded Metadata Structure
A fixed-size structure in the firmware binary containing:
- **Magic markers**: Start (0x464D5441 "FMTA") and End (0x454E4446 "ENDF")
- **Environment name**: e.g., "lilygo-t-display-s3"
- **Device type**: "RECEIVER" or "TRANSMITTER"
- **Firmware version**: Major.Minor.Patch (1.1.1)
- **Build timestamp**: Unix timestamp
- **Build date string**: Human-readable date

### 2. OTA Page Enhancement
JavaScript code to:
- Read selected .bin file as binary
- Search for magic markers
- Extract and parse metadata structure
- **Display** version info before upload
- **No validation** - user can upload any firmware

### 3. Build Integration
- `firmware_metadata.h` - Structure definition
- `firmware_metadata.cpp` - Initialization with build flags
- `platformio.ini` - Build flags for metadata population
- Linked into both receiver and transmitter projects

## Implementation Steps

### Phase 1: Core Metadata Files (ESP32common)

**File: `esp32common/firmware_metadata.h`**
```cpp
#pragma once
#include <stdint.h>

namespace FirmwareMetadata {
    constexpr uint32_t MAGIC_START = 0x464D5441;  // "FMTA"
    constexpr uint32_t MAGIC_END = 0x454E4446;    // "ENDF"
    
    struct __attribute__((packed)) Metadata {
        uint32_t magic_start;
        char env_name[32];
        char device_type[16];
        uint8_t version_major;
        uint8_t version_minor;
        uint8_t version_patch;
        uint8_t reserved1;
        uint32_t build_timestamp;
        char build_date[32];
        uint8_t reserved[32];
        uint32_t magic_end;
    };
    
    extern const Metadata metadata;
}
```

**File: `esp32common/firmware_metadata.cpp`**
```cpp
#include "firmware_metadata.h"

// Populate from build flags
namespace FirmwareMetadata {
    const Metadata metadata __attribute__((section(".rodata"))) = {
        .magic_start = MAGIC_START,
        .env_name = STRINGIFY(PIO_ENV_NAME),
        .device_type = STRINGIFY(TARGET_DEVICE),
        .version_major = FW_VERSION_MAJOR,
        .version_minor = FW_VERSION_MINOR,
        .version_patch = FW_VERSION_PATCH,
        .reserved1 = 0,
        .build_timestamp = BUILD_TIMESTAMP,
        .build_date = BUILD_DATE,
        .reserved = {0},
        .magic_end = MAGIC_END
    };
}
```

### Phase 2: Build Configuration

**Both platformio.ini files add:**
```ini
build_flags = 
    -D PIO_ENV_NAME=\"$PIOENV\"
    -D TARGET_DEVICE=\"RECEIVER\"  ; or TRANSMITTER
    -D BUILD_TIMESTAMP=$(shell python -c "import time; print(int(time.time()))")
    -D BUILD_DATE=\"$(shell python -c "import time; print(time.strftime('%Y-%m-%d %H:%M:%S'))")\"
    -D STRINGIFY(x)=#x
    -I../esp32common  ; Include metadata header
```

### Phase 3: Link Metadata

**Receiver `src/main.cpp`:**
```cpp
#include <firmware_metadata.h>

void setup() {
    // Optionally print at boot
    Serial.printf("Firmware: %s v%d.%d.%d\n",
        FirmwareMetadata::metadata.env_name,
        FirmwareMetadata::metadata.version_major,
        FirmwareMetadata::metadata.version_minor,
        FirmwareMetadata::metadata.version_patch);
}
```

**Transmitter `src/main.cpp`:** (same pattern)

### Phase 4: OTA Page JavaScript Enhancement

**File: `espnowreciever_2/lib/webserver/pages/ota_page.cpp`**

Add JavaScript function:
```javascript
async function extractMetadataFromFile(file) {
    return new Promise((resolve) => {
        const reader = new FileReader();
        reader.onload = function(e) {
            const buffer = new Uint8Array(e.target.result);
            const view = new DataView(buffer.buffer);
            
            // Search for magic marker 0x464D5441
            const MAGIC_START = 0x464D5441;
            for (let i = 0; i < buffer.length - 128; i++) {
                if (view.getUint32(i, true) === MAGIC_START) {
                    // Found metadata!
                    const metadata = {
                        envName: readString(buffer, i + 4, 32),
                        deviceType: readString(buffer, i + 36, 16),
                        major: buffer[i + 52],
                        minor: buffer[i + 53],
                        patch: buffer[i + 54],
                        timestamp: view.getUint32(i + 56, true),
                        buildDate: readString(buffer, i + 60, 32)
                    };
                    resolve(metadata);
                    return;
                }
            }
            resolve(null);
        };
        reader.readAsArrayBuffer(file);
    });
}

function readString(buffer, offset, maxLen) {
    let str = '';
    for (let i = 0; i < maxLen && buffer[offset + i] !== 0; i++) {
        str += String.fromCharCode(buffer[offset + i]);
    }
    return str;
}
```

Update file selection handler:
```javascript
fileInput.addEventListener('change', async function(e) {
    const file = e.target.files[0];
    if (file) {
        document.getElementById('fileName').textContent = file.name;
        document.getElementById('fileSize').textContent = (file.size / 1024).toFixed(1) + ' KB';
        
        // Extract and display metadata
        const metadata = await extractMetadataFromFile(file);
        if (metadata) {
            document.getElementById('metadataInfo').innerHTML = `
                <strong>Firmware Info:</strong><br>
                Environment: ${metadata.envName}<br>
                Device: ${metadata.deviceType}<br>
                Version: ${metadata.major}.${metadata.minor}.${metadata.patch}<br>
                Build Date: ${metadata.buildDate}
            `;
        } else {
            document.getElementById('metadataInfo').innerHTML = 
                '<em>No metadata found (legacy firmware)</em>';
        }
    }
});
```

## Critical Differences from Previous Failed Attempt

### What Went Wrong Before
1. **Correlation ≠ Causation**: We blamed metadata for stack overflow crash
2. **Multiple initialization attempts**: constexpr, C-style, ifdef guards - none helped
3. **Actual cause**: `espnow_announce` task had insufficient stack (2048 bytes) for MqttLogger

### What's Different Now
1. **Root cause fixed**: Discovery task stack already increased to 4096 bytes (main.cpp:108)
2. **Simple initialization**: Use working C-style static const pattern
3. **No validation logic**: Just embed and display - no compatibility checks
4. **Clean implementation**: Start fresh with lessons learned

## Potential Issues & Mitigations

### Issue 1: Build flag escaping
**Problem**: String macros can cause compilation errors
**Mitigation**: Use Python in platformio.ini for timestamp generation, proper quote escaping

### Issue 2: .rodata alignment
**Problem**: Structure might not be aligned properly
**Mitigation**: Use `__attribute__((packed))` and verify with `extract_firmware_metadata.py`

### Issue 3: Large binary search
**Problem**: Searching entire .bin file could be slow
**Mitigation**: 
- Limit search to first 50% of file (metadata likely near start)
- Optimize JavaScript with early termination

### Issue 4: Endianness
**Problem**: ESP32 is little-endian, JavaScript DataView default is big-endian
**Mitigation**: Always use `getUint32(offset, true)` for little-endian reads

## Verification Steps

### 1. Build Verification
```bash
cd espnowreciever_2
pio run
# Check for firmware_metadata.cpp.o in build output
```

### 2. Binary Verification
```bash
python ../esp32common/scripts/extract_firmware_metadata.py .pio/build/lilygo-t-display-s3/firmware.bin
```

Expected output:
```
Found metadata at offset: 0x####
Environment: lilygo-t-display-s3
Device Type: RECEIVER
Version: 1.1.1
Build Timestamp: 1738761234
Build Date: 2026-02-05 14:20:34
```

### 3. Runtime Verification
- Upload firmware
- Check serial output for metadata print
- Open OTA page
- Select .bin file
- Verify metadata displays correctly

### 4. No Validation Test
- Try uploading transmitter firmware to receiver
- Should display different metadata but **not block upload**
- User makes final decision

## Files to Create/Modify

### New Files
1. `esp32common/firmware_metadata.h`
2. `esp32common/firmware_metadata.cpp`
3. `esp32common/scripts/extract_firmware_metadata.py` (optional verification tool)

### Modified Files
1. `espnowreciever_2/platformio.ini` - Add build flags
2. `ESPnowtransmitter2/espnowtransmitter2/platformio.ini` - Add build flags
3. `espnowreciever_2/src/main.cpp` - Include header, optional boot print
4. `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp` - Include header
5. `espnowreciever_2/lib/webserver/pages/ota_page.cpp` - Add JavaScript extraction

## Success Criteria

✅ Firmware builds successfully with metadata embedded
✅ `extract_firmware_metadata.py` can read metadata from .bin file
✅ OTA page displays metadata when file is selected
✅ Metadata shows correct version, environment, device type
✅ Upload works regardless of metadata content
✅ No crashes or stack overflows (already fixed)
✅ Firmware file names still include version: `env_fw_1_1_1.bin`

## What You Might Have Missed

### 1. Git Workflow
- Current work is v1.1.1 (uncommitted)
- Implement metadata in v1.1.1
- Test thoroughly before committing
- Tag as v1.1.1 when stable

### 2. Both Projects Need It
- Receiver firmware needs metadata
- Transmitter firmware needs metadata
- Both can be uploaded via receiver's OTA page

### 3. Python Script Helper
Optional but recommended: `extract_firmware_metadata.py` for debugging
- Verifies metadata is correctly embedded
- Helps troubleshoot if JavaScript can't find it
- Useful for CI/CD pipelines

### 4. Display Location
Should add a new `<div id="metadataInfo"></div>` to OTA page HTML
Currently you probably have just file name and size display

### 5. Backward Compatibility
Legacy firmware without metadata should gracefully show "No metadata found"
- Don't break uploads of old .bin files
- Helpful message instead of error

## Confirmed Requirements

1. **Metadata Display Location: TO THE RIGHT OF CHOSEN FILE**
   - Display metadata in a column/div next to file name and size
   - Side-by-side layout for easy comparison

2. **Currently Running Firmware Metadata: YES - DISPLAY WITH INDICATORS**
   - Read version from embedded metadata if available
   - Display format with indicators:
     - **With metadata**: "v1.1.0 ●" (● = from metadata, alternative: ✓, ROM, or similar)
     - **Without metadata (fallback)**: "v1.1.0 *" (* = from build flags/code only)
   - Implementation: Add API endpoint `/api/firmware-info` that returns:
     - Attempts to read `FirmwareMetadata::metadata` structure
     - Falls back to FW_VERSION_MAJOR/MINOR/PATCH defines if metadata not available
     - Returns JSON with version and source indicator

3. **Format: HUMAN READABLE**
   - Use build date strings (not Unix timestamps)
   - Format: "2026-02-05 14:20:34" or similar
   - Version as "1.1.1" not separate numbers
   - Clear labels: "Receiver v1.1.1" not just numbers

4. **Proceed: YES**
   - Implementation approved
   - Ready to begin

## Timeline Estimate

- **Phase 1**: Create metadata files (30 min)
- **Phase 2**: Configure build flags (20 min)
- **Phase 3**: Link into projects (10 min)
- **Phase 4**: OTA page JavaScript (45 min)
- **Testing**: Full verification (30 min)
- **Total**: ~2 hours

## Proceed?

Please review this plan and confirm:
1. Is the objective correctly understood?
2. Are the steps comprehensive?
3. Any additional requirements?
4. Ready to implement?
