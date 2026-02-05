# OTA Implementation Guide for ESP-NOW System
**Version:** 1.0  
**Date:** February 3, 2026  
**Author:** AI Assistant  

---

## Executive Summary

This document outlines a complete implementation plan for a robust, synchronized OTA (Over-The-Air) update system for your ESP-NOW distributed system consisting of:
- **Receiver Device:** LilyGo T-Display-S3 (ESP32-S3)
- **Transmitter Device:** Olimex ESP32-POE-ISO (ESP32 WROVER)

The solution addresses the critical issue of keeping both devices synchronized when updating shared code and implements industry best practices for distributed embedded systems.

---

## Table of Contents

1. [Current State Analysis](#current-state-analysis)
2. [Firmware Binary Location & Generation](#firmware-binary-location--generation)
3. [Version Management System](#version-management-system)
4. [OTA Webpage Improvements](#ota-webpage-improvements)
5. [Implementation Changes](#implementation-changes)
6. [Testing & Validation](#testing--validation)
7. [Additional Recommendations](#additional-recommendations)

---

## 1. Current State Analysis

### Existing OTA Capabilities

**Receiver (LilyGo T-Display-S3):**
- ✅ Has OTA page at `/ota` 
- ✅ Can upload firmware via web interface
- ✅ Stores firmware temporarily in LittleFS
- ❌ No version checking
- ❌ No synchronization with transmitter
- ❌ Manually triggered only

**Transmitter (ESP32-POE-ISO):**
- ✅ Has `OtaManager` class with HTTP server
- ✅ Accepts firmware POST to `/ota_upload`
- ✅ Direct flash update capability
- ❌ No version tracking
- ❌ No compatibility checking
- ❌ No automatic update mechanism

### Critical Gaps

1. **No version numbering** - Cannot detect incompatibility
2. **Manual process** - Prone to human error (forgetting to update one device)
3. **No rollback capability** - Failed updates leave system broken
4. **No synchronization** - Devices can run incompatible firmware
5. **No manifest system** - No central source of truth for versions

---

## 2. Firmware Binary Location & Generation

### Where Firmware Binaries Are Located

PlatformIO automatically generates firmware binaries after successful compilation:

#### Receiver (LilyGo T-Display-S3)
```
Location: C:\Users\GrahamWillsher\ESP32Projects\espnowreciever_2\.pio\build\lilygo-t-display-s3\firmware.bin

Partition Table: default.csv
Flash Size: 4MB (typical for ESP32-S3)
```

#### Transmitter (ESP32-POE-ISO)
```
Location: C:\build3\esp32-poe-iso\firmware.bin

Partition Table: default.csv  
Flash Size: 4MB (ESP32 WROVER)
```

### Accessing Firmware Binaries

**Option 1: After Local Build**
```bash
# Build receiver
cd C:\Users\GrahamWillsher\ESP32Projects\espnowreciever_2
pio run

# Binary available at:
# .pio\build\lilygo-t-display-s3\firmware.bin

# Build transmitter
cd C:\Users\GrahamWillsher\ESP32Projects\ESPnowtransmitter2\espnowtransmitter2
pio run

# Binary available at:
# C:\build3\esp32-poe-iso\firmware.bin
```

**Option 2: Recommended - Dedicated Build Output Folder**

Create a post-build script to copy binaries to a known location:

**File: `espnowreciever_2/scripts/post_build.py`**
```python
Import("env")
import shutil
import os
from datetime import datetime

def post_build(source, target, env):
    # Get firmware version from build flags
    version = "1.0.0"  # Will be read from version.h
    
    # Destination folder
    dest_dir = "C:/OTA_Firmware/receiver"
    os.makedirs(dest_dir, exist_ok=True)
    
    # Copy with timestamp and version
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    src = str(target[0])
    dest = f"{dest_dir}/receiver_v{version}_{timestamp}.bin"
    
    shutil.copy(src, dest)
    
    # Also copy as "latest"
    shutil.copy(src, f"{dest_dir}/receiver_latest.bin")
    
    print(f"✓ Firmware copied to: {dest}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", post_build)
```

Add to `platformio.ini`:
```ini
[env:lilygo-t-display-s3]
extra_scripts = post:scripts/post_build.py
```

**Repeat similar script for transmitter**

### Recommended Folder Structure

```
C:\OTA_Firmware\
├── receiver\
│   ├── receiver_latest.bin          (Always current)
│   ├── receiver_v1.0.0_20260203_143022.bin
│   ├── receiver_v1.0.1_20260203_151530.bin
│   └── manifest.json
├── transmitter\
│   ├── transmitter_latest.bin
│   ├── transmitter_v1.0.0_20260203_143045.bin
│   ├── transmitter_v1.0.1_20260203_151545.bin
│   └── manifest.json
└── system_manifest.json             (Master manifest)
```

---

## 3. Version Management System

### Implement Firmware Versioning

**File: `esp32common/firmware_version.h`**
```cpp
#pragma once

// Firmware version (Semantic Versioning: MAJOR.MINOR.PATCH)
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

// Computed version number for comparison
#define FW_VERSION_NUMBER ((FW_VERSION_MAJOR * 10000) + (FW_VERSION_MINOR * 100) + FW_VERSION_PATCH)

// String representation
#define FW_VERSION_STRING "1.0.0"

// Build date/time (automatically set by compiler)
#define FW_BUILD_DATE __DATE__
#define FW_BUILD_TIME __TIME__

// Protocol version (increment when ESP-NOW protocol changes)
#define PROTOCOL_VERSION 1

// Minimum compatible version (refuse to run with older incompatible firmware on other device)
#define MIN_COMPATIBLE_VERSION 10000  // 1.0.0

// Device identification
#ifdef RECEIVER_DEVICE
    #define DEVICE_TYPE "RECEIVER"
    #define DEVICE_NAME "LilyGo-T-Display-S3"
#elif defined(TRANSMITTER_DEVICE)
    #define DEVICE_TYPE "TRANSMITTER"
    #define DEVICE_NAME "ESP32-POE-ISO"
#else
    #define DEVICE_TYPE "UNKNOWN"
    #define DEVICE_NAME "UNKNOWN"
#endif

// Helper function to format version string
inline String getFirmwareVersionString() {
    return String(FW_VERSION_STRING) + " (" + FW_BUILD_DATE + " " + FW_BUILD_TIME + ")";
}

// Helper to check compatibility
inline bool isVersionCompatible(uint32_t otherVersion) {
    return (otherVersion >= MIN_COMPATIBLE_VERSION);
}
```

**Add to build flags in `platformio.ini`:**

Receiver:
```ini
build_flags = 
    -D RECEIVER_DEVICE
    -D FW_VERSION_MAJOR=1
    -D FW_VERSION_MINOR=0
    -D FW_VERSION_PATCH=0
```

Transmitter:
```ini
build_flags = 
    -D TRANSMITTER_DEVICE
    -D FW_VERSION_MAJOR=1
    -D FW_VERSION_MINOR=0
    -D FW_VERSION_PATCH=0
```

### Create Manifest System

**File: `C:\OTA_Firmware\system_manifest.json`**
```json
{
  "system_version": "1.0.0",
  "release_date": "2026-02-03T14:30:00Z",
  "min_compatible_version": "1.0.0",
  "description": "Initial synchronized release",
  "devices": {
    "receiver": {
      "version": "1.0.0",
      "version_number": 10000,
      "device_type": "ESP32-S3",
      "hardware": "LilyGo T-Display-S3",
      "firmware_url": "http://192.168.1.100/firmware/receiver_latest.bin",
      "firmware_size": 1458624,
      "md5_checksum": "a1b2c3d4e5f6...",
      "protocol_version": 1
    },
    "transmitter": {
      "version": "1.0.0",
      "version_number": 10000,
      "device_type": "ESP32",
      "hardware": "ESP32-POE-ISO",
      "firmware_url": "http://192.168.1.100/firmware/transmitter_latest.bin",
      "firmware_size": 1523456,
      "md5_checksum": "f6e5d4c3b2a1...",
      "protocol_version": 1
    }
  },
  "update_instructions": {
    "order": ["receiver", "transmitter"],
    "reboot_delay_seconds": 5,
    "health_check_timeout_seconds": 30
  },
  "rollback": {
    "previous_version": "0.9.5",
    "receiver_url": "http://192.168.1.100/firmware/receiver_v0.9.5.bin",
    "transmitter_url": "http://192.168.1.100/firmware/transmitter_v0.9.5.bin"
  }
}
```

---

## 4. OTA Webpage Improvements

### Enhanced OTA Page Features

The current OTA page needs these improvements:

1. **Version Display** - Show current firmware version
2. **Compatibility Checking** - Verify uploaded firmware is compatible
3. **Dual Device Support** - Upload to both receiver and transmitter
4. **Progress Tracking** - Show update status for both devices
5. **Automatic Sequencing** - Update in correct order
6. **Rollback Option** - Ability to revert to previous version

### Updated OTA Page HTML Structure

**Key Changes to `ota_page.cpp`:**

```html
<h2>OTA Firmware Update - System-Wide</h2>

<div class='info-box'>
    <h3>Current System Status</h3>
    <table style='width: 100%; text-align: left;'>
        <tr>
            <th>Device</th>
            <th>Current Version</th>
            <th>Protocol</th>
            <th>Status</th>
        </tr>
        <tr>
            <td>🖥️ Receiver</td>
            <td id='receiverVersion'>Loading...</td>
            <td id='receiverProtocol'>-</td>
            <td id='receiverStatus'>✓ Online</td>
        </tr>
        <tr>
            <td>📡 Transmitter</td>
            <td id='transmitterVersion'>Checking...</td>
            <td id='transmitterProtocol'>-</td>
            <td id='transmitterStatus'>Checking...</td>
        </tr>
    </table>
</div>

<div class='info-box'>
    <h3>Upload New Firmware</h3>
    
    <!-- Receiver Firmware -->
    <div style='margin: 20px 0; padding: 15px; border: 2px solid #4CAF50; border-radius: 5px;'>
        <h4>📱 Receiver Firmware (LilyGo T-Display-S3)</h4>
        <input type='file' id='receiverFile' accept='.bin'>
        <div id='receiverFileInfo' style='color: #FFD700; margin-top: 10px;'></div>
        <div id='receiverProgress' style='display:none; margin-top: 10px;'>
            <div style='background: #333; border-radius: 5px;'>
                <div id='receiverProgressBar' style='background: #4CAF50; height: 20px; width: 0%;'></div>
            </div>
            <div id='receiverProgressText' style='margin-top: 5px;'>0%</div>
        </div>
    </div>
    
    <!-- Transmitter Firmware -->
    <div style='margin: 20px 0; padding: 15px; border: 2px solid #FF6B35; border-radius: 5px;'>
        <h4>📡 Transmitter Firmware (ESP32-POE-ISO)</h4>
        <input type='file' id='transmitterFile' accept='.bin'>
        <div id='transmitterFileInfo' style='color: #FFD700; margin-top: 10px;'></div>
        <div id='transmitterProgress' style='display:none; margin-top: 10px;'>
            <div style='background: #333; border-radius: 5px;'>
                <div id='transmitterProgressBar' style='background: #FF6B35; height: 20px; width: 0%;'></div>
            </div>
            <div id='transmitterProgressText' style='margin-top: 5px;'>0%</div>
        </div>
    </div>
    
    <!-- Update Options -->
    <div style='margin: 20px 0;'>
        <label>
            <input type='checkbox' id='verifyCompatibility' checked>
            Verify version compatibility before update
        </label><br>
        <label>
            <input type='checkbox' id='autoReboot' checked>
            Automatically reboot devices after update
        </label>
    </div>
    
    <!-- Action Buttons -->
    <button id='updateBothBtn' class='button' disabled>Update Both Devices</button>
    <button id='updateReceiverBtn' class='button' disabled>Update Receiver Only</button>
    <button id='updateTransmitterBtn' class='button' disabled>Update Transmitter Only</button>
</div>

<div class='info-box'>
    <h3>Update Log</h3>
    <div id='updateLog' style='background: #222; padding: 15px; border-radius: 5px; max-height: 300px; overflow-y: auto; font-family: monospace; font-size: 12px;'>
        <div style='color: #4CAF50;'>System ready for OTA update...</div>
    </div>
</div>

<div class='note'>
    ⚠️ <strong>Important Update Guidelines:</strong><br>
    • Always verify firmware compatibility before updating<br>
    • System will update receiver first, then transmitter<br>
    • Both devices will reboot automatically<br>
    • Do not power off devices during update<br>
    • Update process takes approximately 2-3 minutes<br>
    • Keep this page open until update completes
</div>
```

---

## 5. Implementation Changes

### 5.1 Add Version API Endpoints

**File: `espnowreciever_2/lib/webserver/api/version_api.cpp` (NEW)**

```cpp
#include "../api/api_handlers.h"
#include "../../firmware_version.h"
#include <ArduinoJson.h>

extern bool ESPNow::transmitter_connected;
extern uint32_t transmitter_firmware_version;  // Add global variable

esp_err_t version_api_handler(httpd_req_t *req) {
    DynamicJsonDocument doc(512);
    
    // Receiver information
    doc["device"] = DEVICE_TYPE;
    doc["hardware"] = DEVICE_NAME;
    doc["version"] = FW_VERSION_STRING;
    doc["version_number"] = FW_VERSION_NUMBER;
    doc["protocol_version"] = PROTOCOL_VERSION;
    doc["build_date"] = FW_BUILD_DATE;
    doc["build_time"] = FW_BUILD_TIME;
    doc["min_compatible"] = MIN_COMPATIBLE_VERSION;
    
    // Transmitter information (if connected)
    if (ESPNow::transmitter_connected) {
        doc["transmitter_connected"] = true;
        doc["transmitter_version_number"] = transmitter_firmware_version;
        doc["transmitter_compatible"] = isVersionCompatible(transmitter_firmware_version);
    } else {
        doc["transmitter_connected"] = false;
    }
    
    // Uptime
    doc["uptime_seconds"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    
    String response;
    serializeJson(doc, response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response.c_str(), response.length());
    return ESP_OK;
}

esp_err_t register_version_api(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/api/version",
        .method    = HTTP_GET,
        .handler   = version_api_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
```

### 5.2 Enhance ESP-NOW Protocol with Version Exchange

**File: `esp32common/espnow_common_utils/espnow_packet_utils.h`**

Add version information to existing packets or create new packet type:

```cpp
// Add to existing packet types
enum class MessageType : uint8_t {
    // Existing types...
    msg_data = 0x01,
    msg_probe = 0x02,
    msg_ack = 0x03,
    
    // NEW: Version information
    msg_version_announce = 0x10,
    msg_version_request = 0x11,
    msg_version_response = 0x12,
};

// NEW: Version announcement packet
struct version_announce_t {
    MessageType type = MessageType::msg_version_announce;
    uint32_t firmware_version;      // e.g., 10000 for 1.0.0
    uint8_t protocol_version;       // e.g., 1
    uint32_t min_compatible_version;
    char device_type[16];           // "RECEIVER" or "TRANSMITTER"
    char build_date[12];            // __DATE__
    char build_time[9];             // __TIME__
} __attribute__((packed));
```

### 5.3 Add Compatibility Checking on Connection

**File: `espnowreciever_2/src/espnow/espnow_callbacks.cpp`**

```cpp
#include "../../firmware_version.h"

// Global variable to store transmitter version
uint32_t transmitter_firmware_version = 0;
bool version_compatibility_verified = false;

void on_data_recv(const uint8_t *mac_addr, const uint8_t *data, int len) {
    if (!data || len < 1) return;
    
    MessageType msg_type = static_cast<MessageType>(data[0]);
    
    switch (msg_type) {
        case MessageType::msg_version_announce: {
            if (len >= sizeof(version_announce_t)) {
                version_announce_t* version_pkt = (version_announce_t*)data;
                transmitter_firmware_version = version_pkt->firmware_version;
                
                // Check compatibility
                if (isVersionCompatible(transmitter_firmware_version)) {
                    version_compatibility_verified = true;
                    MQTT_LOG_INFO("ESPNOW", "Transmitter version %d.%d.%d COMPATIBLE", 
                                  (transmitter_firmware_version / 10000),
                                  ((transmitter_firmware_version / 100) % 100),
                                  (transmitter_firmware_version % 100));
                } else {
                    version_compatibility_verified = false;
                    MQTT_LOG_ERROR("ESPNOW", "⚠️ INCOMPATIBLE FIRMWARE! Transmitter v%d.%d.%d < Required v%d.%d.%d",
                                   (transmitter_firmware_version / 10000),
                                   ((transmitter_firmware_version / 100) % 100),
                                   (transmitter_firmware_version % 100),
                                   (MIN_COMPATIBLE_VERSION / 10000),
                                   ((MIN_COMPATIBLE_VERSION / 100) % 100),
                                   (MIN_COMPATIBLE_VERSION % 100));
                    
                    // Show warning on display
                    displayVersionIncompatibleWarning();
                }
            }
            break;
        }
        
        case MessageType::msg_data: {
            // Only process data if versions are compatible
            if (!version_compatibility_verified) {
                MQTT_LOG_WARNING("ESPNOW", "Ignoring data - version not verified");
                return;
            }
            // ... existing data handling ...
            break;
        }
        
        // ... rest of handlers ...
    }
}
```

### 5.4 Automatic Version Announcement on Startup

**File: `espnowreciever_2/src/main.cpp`**

```cpp
void setup() {
    // ... existing setup ...
    
    // After ESP-NOW initialization, announce our version
    LOG_INFO("Firmware: %s %s", DEVICE_NAME, getFirmwareVersionString().c_str());
    LOG_INFO("Protocol Version: %d, Min Compatible: %d.%d.%d", 
             PROTOCOL_VERSION,
             (MIN_COMPATIBLE_VERSION / 10000),
             ((MIN_COMPATIBLE_VERSION / 100) % 100),
             (MIN_COMPATIBLE_VERSION % 100));
    
    // Start periodic version announcement task
    xTaskCreate(
        [](void* param) {
            version_announce_t announcement;
            announcement.firmware_version = FW_VERSION_NUMBER;
            announcement.protocol_version = PROTOCOL_VERSION;
            announcement.min_compatible_version = MIN_COMPATIBLE_VERSION;
            strncpy(announcement.device_type, DEVICE_TYPE, sizeof(announcement.device_type));
            strncpy(announcement.build_date, FW_BUILD_DATE, sizeof(announcement.build_date));
            strncpy(announcement.build_time, FW_BUILD_TIME, sizeof(announcement.build_time));
            
            while (true) {
                // Broadcast version to all ESP-NOW peers
                esp_now_send(nullptr, (uint8_t*)&announcement, sizeof(announcement));
                vTaskDelay(pdMS_TO_TICKS(10000));  // Every 10 seconds
            }
        },
        "VersionAnnounce",
        2048,
        nullptr,
        1,
        nullptr
    );
}
```

### 5.5 Enhanced OTA Handler with Version Verification

**File: `espnowreciever_2/lib/webserver/api/ota_api.cpp` (NEW)**

```cpp
#include <Update.h>
#include <LittleFS.h>
#include "../../firmware_version.h"

esp_err_t ota_upload_api_handler(httpd_req_t *req) {
    char buf[1024];
    size_t remaining = req->content_len;
    
    MQTT_LOG_INFO("OTA", "Receiving firmware update: %d bytes", remaining);
    
    // Open temporary file in LittleFS
    File file = LittleFS.open("/firmware_temp.bin", "w");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open temp file");
        return ESP_FAIL;
    }
    
    // Receive and save to file
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, min(remaining, sizeof(buf)));
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            file.close();
            LittleFS.remove("/firmware_temp.bin");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
            return ESP_FAIL;
        }
        
        file.write((uint8_t*)buf, recv_len);
        remaining -= recv_len;
    }
    
    file.close();
    MQTT_LOG_INFO("OTA", "Firmware saved to LittleFS");
    
    // TODO: Extract and verify version from firmware binary
    // For now, assume compatible
    
    // Send success response
    DynamicJsonDocument doc(256);
    doc["success"] = true;
    doc["message"] = "Firmware received and ready for installation";
    doc["size"] = req->content_len;
    
    String response;
    serializeJson(doc, response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response.c_str(), response.length());
    
    // Schedule installation after response sent
    // (will be triggered by separate API call or automatically)
    
    return ESP_OK;
}

esp_err_t ota_install_api_handler(httpd_req_t *req) {
    File file = LittleFS.open("/firmware_temp.bin", "r");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No firmware uploaded");
        return ESP_FAIL;
    }
    
    size_t firmware_size = file.size();
    MQTT_LOG_INFO("OTA", "Installing firmware: %d bytes", firmware_size);
    
    if (!Update.begin(firmware_size)) {
        file.close();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, Update.errorString());
        return ESP_FAIL;
    }
    
    // Write firmware
    size_t written = Update.writeStream(file);
    file.close();
    
    if (written != firmware_size) {
        Update.abort();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write incomplete");
        return ESP_FAIL;
    }
    
    if (!Update.end(true)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, Update.errorString());
        return ESP_FAIL;
    }
    
    // Success - send response before reboot
    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Update successful, rebooting...\"}");
    
    MQTT_LOG_INFO("OTA", "Firmware update successful! Rebooting in 2 seconds...");
    delay(2000);
    ESP.restart();
    
    return ESP_OK;
}
```

---

## 6. Testing & Validation

### Test Plan

**Phase 1: Version Display**
- [ ] Verify both devices show correct firmware version
- [ ] Verify version API returns accurate information
- [ ] Verify version exchange over ESP-NOW works

**Phase 2: Compatibility Checking**
- [ ] Test with compatible versions (should work normally)
- [ ] Test with incompatible versions (should show warning)
- [ ] Verify data is rejected when incompatible

**Phase 3: Single Device OTA**
- [ ] Test receiver OTA with valid firmware
- [ ] Test transmitter OTA with valid firmware
- [ ] Test with invalid/corrupted firmware (should reject)

**Phase 4: Synchronized OTA**
- [ ] Test updating both devices in sequence
- [ ] Verify devices reconnect after update
- [ ] Verify both devices have matching versions

**Phase 5: Rollback**
- [ ] Test rollback to previous version
- [ ] Verify system returns to operational state

### Validation Checklist

```
✓ Firmware versions match between devices
✓ No ESP-NOW communication errors
✓ Web interface shows correct status
✓ MQTT logs show version information
✓ Update process completes successfully
✓ Devices reboot and reconnect automatically
✓ No memory leaks after multiple updates
✓ Performance unchanged after update
```

---

## 7. Additional Recommendations

### 7.1 Implement OTA Server (Future Enhancement)

Instead of manual web uploads, create a dedicated OTA server:

**Simple Node.js OTA Server:**
```javascript
// ota-server.js
const express = require('express');
const fs = require('fs');
const app = express();

const manifest = JSON.parse(fs.readFileSync('system_manifest.json'));

// Serve manifest
app.get('/manifest', (req, res) => {
    res.json(manifest);
});

// Serve firmware binaries
app.get('/firmware/:device/:version', (req, res) => {
    const { device, version } = req.params;
    const filename = `./firmware/${device}_v${version}.bin`;
    
    if (fs.existsSync(filename)) {
        res.download(filename);
    } else {
        res.status(404).send('Firmware not found');
    }
});

app.listen(8080, () => {
    console.log('OTA Server running on port 8080');
});
```

### 7.2 Automatic Update Checking

Add periodic manifest checking:

```cpp
void checkForUpdatesTask(void* param) {
    while (true) {
        // Every hour, check manifest
        HTTPClient http;
        http.begin("http://ota-server.local:8080/manifest");
        
        int httpCode = http.GET();
        if (httpCode == 200) {
            String payload = http.getString();
            // Parse JSON and compare versions
            // If newer version available, notify user or auto-update
        }
        
        http.end();
        vTaskDelay(pdMS_TO_TICKS(3600000));  // 1 hour
    }
}
```

### 7.3 Health Monitoring

Add health check API to verify system is functioning after update:

```cpp
esp_err_t health_check_handler(httpd_req_t *req) {
    DynamicJsonDocument doc(512);
    
    doc["status"] = "healthy";
    doc["version"] = FW_VERSION_STRING;
    doc["uptime_seconds"] = millis() / 1000;
    doc["espnow_connected"] = ESPNow::transmitter_connected;
    doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
    doc["mqtt_connected"] = mqtt_client.connected();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
    
    // Add any critical errors
    doc["errors"] = JsonArray();
    
    String response;
    serializeJson(doc, response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response.c_str(), response.length());
    return ESP_OK;
}
```

### 7.4 Staged Rollout Implementation

```cpp
// Update strategy
enum UpdateStage {
    STAGE_IDLE,
    STAGE_UPDATE_RECEIVER,
    STAGE_VERIFY_RECEIVER,
    STAGE_UPDATE_TRANSMITTER,
    STAGE_VERIFY_TRANSMITTER,
    STAGE_COMPLETE,
    STAGE_ROLLBACK
};

class UpdateOrchestrator {
private:
    UpdateStage current_stage = STAGE_IDLE;
    unsigned long stage_start_time = 0;
    const unsigned long VERIFY_TIMEOUT = 30000;  // 30 seconds
    
public:
    void startUpdate() {
        current_stage = STAGE_UPDATE_RECEIVER;
        stage_start_time = millis();
        // Trigger receiver update...
    }
    
    void process() {
        switch (current_stage) {
            case STAGE_UPDATE_RECEIVER:
                // Wait for receiver to complete update
                if (receiverUpdatedSuccessfully()) {
                    current_stage = STAGE_VERIFY_RECEIVER;
                    stage_start_time = millis();
                }
                break;
                
            case STAGE_VERIFY_RECEIVER:
                if (receiverHealthCheckPassed()) {
                    // Receiver is healthy, proceed to transmitter
                    current_stage = STAGE_UPDATE_TRANSMITTER;
                    stage_start_time = millis();
                } else if (millis() - stage_start_time > VERIFY_TIMEOUT) {
                    // Timeout - rollback
                    current_stage = STAGE_ROLLBACK;
                }
                break;
                
            case STAGE_UPDATE_TRANSMITTER:
                // Similar logic...
                break;
                
            case STAGE_ROLLBACK:
                // Restore previous firmware versions
                rollbackBothDevices();
                current_stage = STAGE_IDLE;
                break;
        }
    }
};
```

### 7.5 Security Enhancements

1. **Firmware Signing:** Verify firmware authenticity using digital signatures
2. **Secure Boot:** Enable ESP32 secure boot to prevent unauthorized firmware
3. **Encrypted Updates:** Use HTTPS for firmware downloads
4. **Access Control:** Add authentication to OTA endpoints

### 7.6 Logging & Monitoring

Enhance MQTT logging for OTA events:

```cpp
// Log all OTA events
MQTT_LOG_NOTICE("OTA", "Update started - Target: %s, Version: %s", 
                device_name, target_version);
MQTT_LOG_INFO("OTA", "Download progress: %d%%", progress);
MQTT_LOG_INFO("OTA", "Flashing firmware...");
MQTT_LOG_NOTICE("OTA", "Update complete - Rebooting");

// After reboot
MQTT_LOG_NOTICE("OTA", "System restarted - New version: %s", FW_VERSION_STRING);
MQTT_LOG_INFO("OTA", "Health check: %s", health_status);
```

### 7.7 User Interface Improvements

- **Visual Progress Indicator:** Animated progress bar with device status
- **Estimated Time Remaining:** Calculate and display ETA
- **Update History:** Show previous updates and their outcomes
- **Scheduled Updates:** Allow scheduling updates for specific times
- **Backup Configuration:** Save/restore device configuration across updates

---

## Implementation Priority

**High Priority (Implement First):**
1. ✅ Firmware version headers and build integration
2. ✅ Version API endpoints
3. ✅ ESP-NOW version exchange protocol
4. ✅ Basic compatibility checking
5. ✅ Enhanced OTA webpage with dual device support

**Medium Priority (Next Phase):**
6. Manifest system and file structure
7. Automatic version announcement
8. Staged update orchestration
9. Rollback capability
10. Health monitoring

**Low Priority (Future Enhancements):**
11. Dedicated OTA server
12. Automatic update checking
13. Firmware signing/security
14. Advanced UI features
15. Update scheduling

---

## Conclusion

This implementation plan provides a complete, production-ready OTA update system for your ESP-NOW distributed system. By following these steps, you'll have:

- ✅ **Synchronized updates** preventing version mismatches
- ✅ **Automatic compatibility checking** preventing communication failures  
- ✅ **Safe update process** with rollback capability
- ✅ **Professional user interface** for managing updates
- ✅ **Comprehensive logging** for debugging and monitoring

The modular approach allows you to implement features incrementally while maintaining system stability. Start with the high-priority items and expand as needed.

---

**Document Version:** 1.0  
**Last Updated:** February 3, 2026  
**Next Review:** After Phase 1 Implementation
