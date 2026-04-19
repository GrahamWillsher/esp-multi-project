# OTA Failure Analysis: `espnowreceiver_LCD` vs `espnowreceiver_2`

**Date:** 2026-04-19  
**Author:** Investigation (GitHub Copilot)  
**Status:** Findings Complete — Awaiting Resolution  
**Severity:** 🔴 Critical — OTA is non-functional in `espnowreceiver_LCD`
**Scope:** Receiver self-OTA (`/api/ota_upload_receiver`) and Transmitter proxy-OTA (`/api/ota_upload`) via the receiver web page

---

## 1. Executive Summary

OTA firmware updates work correctly in `espnowreceiver_2` but fail in `espnowreceiver_LCD`. This document
presents a full root-cause analysis of every difference between the two projects that affects OTA reliability,
ranked by severity. The primary failure is a missing build-flag that causes the firmware binary to embed
`TARGET_DEVICE = "UNKNOWN"` instead of `"RECEIVER"`, causing the shared `FirmwareCompatibilityPolicy`
validation layer to reject every single upload before it can be applied. Several secondary issues compound
the problem during initial provisioning (AP-fallback mode) and during serial-vs-OTA upload selection.

This document also covers the second OTA path: updating the **transmitter** firmware via the receiver's
`/ota` web page. The receiver proxies the binary over a direct TCP connection to the transmitter. This
proxy path has its own pre-requisites and failure modes that are independent of — but interact with — the
receiver self-OTA issues above.

---

## 2. Background and Scope

Both `espnowreceiver_2` and `espnowreceiver_LCD` share:


The LCD project is a new hardware variant (Waveshare ESP32-S3, 8 MB flash, LVGL display) built from the
`_2` receiver codebase as a starting point. Because it was not yet a production device at the time the
`firmware_metadata` and `FirmwareCompatibilityPolicy` systems were hardened, the necessary build flag
additions were missed.

**Projects examined:**

| Path | Status |
|---|---|
| `c:\...\espnowreceiver_2` | OTA working correctly |
| `c:\...\espnowreceiver_LCD` | OTA failing — subject of this investigation |
| `c:\...\esp32common` | Shared library providing OTA infrastructure |

The receiver web interface exposes **two distinct OTA endpoints**, both of which are affected by issues
described in this document:

| Endpoint | Purpose | Validation owner |
|---|---|---|
| `POST /api/ota_upload_receiver` | Flash new firmware directly onto the receiver | Receiver validates binary device type |
| `POST /api/ota_upload` | Proxy transmitter firmware through the receiver to the transmitter | Transmitter validates binary device type |

The transmitter project `ESPnowtransmitter2` was also examined as part of this investigation.

| Path | Status |
|---|---|
| `c:\...\ESPnowtransmitter2` | Transmitter project — `TARGET_DEVICE=TRANSMITTER` correctly set |

---

## 3. Architecture of the OTA Pipeline (Shared)

Understanding how the shared OTA pipeline works is essential to understanding every finding below.

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Build time                                                              │
│                                                                          │
│  platformio.ini                                                          │
│  build_flags: -DTARGET_DEVICE=RECEIVER                                  │
│                      │                                                   │
│                      ▼                                                   │
│  firmware_metadata/firmware_metadata.h                                  │
│    embeds TARGET_DEVICE, FW_VERSION_MAJOR/MINOR/PATCH                   │
│    into a known binary signature block                                  │
└──────────────────────────────┬──────────────────────────────────────────┘
                               │ Produces .bin with embedded metadata
                               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  Upload time (web UI)                                                    │
│                                                                          │
│  Browser → POST /api/ota_upload_receiver                                │
│              + Header: X-OTA-Image-SHA256: <hex64>                      │
│              + Body: raw firmware .bin                                  │
│                      │                                                   │
│                      ▼                                                   │
│  api_ota_upload_receiver_handler()   (lib/webserver_lcd/api/)           │
│    1. Stream bytes → Update.write()                                      │
│    2. Verify SHA-256 of received bytes                                  │
│    3. Scan received binary for metadata signature block                 │
│    4. Call FirmwareCompatibilityPolicy::validate_scan(                  │
│           scan, expected_device="RECEIVER", expected_major=2)           │
│    5. If ALLOWED → Update.end() → reboot                                │
│       If REJECTED → Update.abort() → return 400                         │
└─────────────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  Boot time (after OTA partition swap)                                    │
│                                                                          │
│  OtaBootGuard::begin("RX_LCD_BOOT_GUARD")                               │
│    Reads esp_ota_get_running_partition()                                 │
│    Validates boot succeeded, confirms partition                         │
│    On failure: rolls back to previous partition                         │
└─────────────────────────────────────────────────────────────────────────┘
```

The critical gate is **Step 4** in the upload pipeline — the `FirmwareCompatibilityPolicy` scan. This is
where all `espnowreceiver_LCD` OTA attempts die.

### 3.2 Transmitter Proxy OTA Pipeline

The receiver's `/ota` web page also allows flashing the **transmitter** by proxying the binary over a
direct raw TCP HTTP connection. The receiver does **not** validate device type — the transmitter does.

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Build time (transmitter firmware)                                      │
│                                                                          │
│  ESPnowtransmitter2/platformio.ini                                      │
│  build_flags: -DTARGET_DEVICE=TRANSMITTER  ✓ (correctly set)           │
│                      │                                                   │
│                      ▼                                                   │
│  firmware_metadata embeds device_type="TRANSMITTER"                    │
└──────────────────────────────┬──────────────────────────────────────────┘
                               │ Produces transmitter .bin
                               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  Upload time — browser → receiver                                        │
│                                                                          │
│  Browser → POST /api/ota_upload  (receiver endpoint)                   │
│              + Header: X-OTA-Image-SHA256: <hex64>                      │
│              + Body: raw transmitter .bin                               │
│                      │                                                   │
│                      ▼                                                   │
│  api_ota_upload_handler()  (receiver — lib/webserver_lcd/api/)         │
│    1. Validate X-OTA-Image-SHA256 header format only                   │
│    2. Check TransmitterManager::isIPKnown()                            │
│       └─ If false → 400 "Transmitter IP unknown"                       │
│    3. Send ESP-NOW msg_ota_start to transmitter MAC                    │
│       └─ Arms an OTA session on the transmitter (200ms settle)         │
│    4. acquire_ota_session_challenge()                                   │
│       GET http://<tx_ip>/api/ota_status → reuse session, or           │
│       POST http://<tx_ip>/api/ota_arm → fresh session                  │
│       Returns: session_id, nonce, expires_str, HMAC-SHA256 signature   │
│    5. Open raw WiFiClient TCP to tx_ip:80                              │
│       Send HTTP headers including X-OTA-Session / X-OTA-Signature      │
│    6. Stream .bin bytes from browser socket → transmitter TCP socket   │
│       (2 KB chunks; 60 s stall timeout; early-rejection detection)     │
│    7. Await transmitter HTTP response (up to 70 s)                     │
│    8. Return result to browser                                          │
└──────────────────────────────┬──────────────────────────────────────────┘
                               │ Raw firmware bytes arrive at transmitter
                               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  Transmitter — ota_upload_handler()                                     │
│                                                                          │
│    1. validate_ota_auth_headers()                                       │
│       HMAC-SHA256(ota_psk, session_id+":"+nonce) == X-OTA-Signature    │
│    2. Update.begin(UPDATE_SIZE_UNKNOWN)                                 │
│    3. Per-chunk: Update.write() + rolling SHA-256 + metadata scan      │
│    4. After all bytes:                                                  │
│       a. SHA-256 verify → must match X-OTA-Image-SHA256 header        │
│       b. FirmwareCompatibilityPolicy::validate_scan(                   │
│              scan, "TRANSMITTER", FW_VERSION_MAJOR)                    │
│          → device_type "TRANSMITTER" ✓  → allowed                      │
│          → device_type mismatch      → Update.abort(), HTTP 400        │
│       c. Update.end(true) → marks OTA partition pending                │
│    5. Returns HTTP 200 {"success":true,"ready_for_reboot":true}        │
└──────────────────────────────┬──────────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  Reboot                                                                  │
│                                                                          │
│  Browser clicks "Reboot Transmitter"                                    │
│  → POST /api/reboot (receiver)                                          │
│  → receiver sends ESP-NOW msg_reboot to transmitter                    │
│  → transmitter restarts into new OTA partition                          │
│  → OtaBootGuard::begin() confirms new partition                        │
└─────────────────────────────────────────────────────────────────────────┘
```

**Key observation:** The transmitter proxy path bypasses the receiver's `FirmwareCompatibilityPolicy`
entirely. The binary bytes flow straight through. The receiver only checks that the `X-OTA-Image-SHA256`
header is syntactically valid. All semantic validation (device type, version) occurs inside the
transmitter. This design is correct and intentional — the receiver is acting as a dumb pipe.

**Critical pre-requisite:** The receiver must have **established ESP-NOW contact with the transmitter**
before proxy OTA can be attempted. `TransmitterManager::isIPKnown()` must return `true`. This means:
- The transmitter must be powered and running
- At least one ESP-NOW packet must have been received from the transmitter (carries its IP)
- The receiver must be in STA mode on the home LAN (not AP-fallback mode) for the subsequent TCP connection

---

## 4. Findings

### Finding 1 — 🔴 CRITICAL: `TARGET_DEVICE` Not Defined in LCD Build Flags

**Category:** Build configuration  
**File affected:** `espnowreceiver_LCD/platformio.ini`  
**Breaks:** Every OTA upload, unconditionally

#### What is happening

`esp32common/firmware_metadata/firmware_metadata.h` embeds a device-type string into the compiled firmware
binary at build time using the `TARGET_DEVICE` preprocessor macro:

```cpp
// firmware_metadata.h (simplified)
#ifndef TARGET_DEVICE
  #define TARGET_DEVICE UNKNOWN     // ← fallback when not set
#endif

// A signature block is placed in flash:
// { magic, TOSTRING(TARGET_DEVICE), FW_VERSION_MAJOR, ... }
```

`espnowreceiver_2/platformio.ini` correctly defines this:

```ini
build_flags =
    -D TARGET_DEVICE=RECEIVER
    -D RECEIVER_DEVICE
    -D FW_VERSION_MAJOR=2
    ...
```

`espnowreceiver_LCD/platformio.ini` does **not** define it at all:

```ini
build_flags =
    -DCORE_DEBUG_LEVEL=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DBOARD_HAS_PSRAM
    -I ../esp32common
    -Iinclude
    -DLV_CONF_PATH=lv_conf.h
    -DFW_VERSION_MAJOR=2
    -DFW_VERSION_MINOR=0
    -DFW_VERSION_PATCH=0
    -DLOG_USE_MQTT=0
    ; ← TARGET_DEVICE=RECEIVER is ABSENT
    ; ← RECEIVER_DEVICE is ABSENT
```

Every `espnowreceiver_LCD` firmware binary therefore contains the string `"UNKNOWN"` in its embedded
metadata signature block.

#### How the rejection occurs

When `api_ota_upload_receiver_handler()` receives the binary, it scans it and then calls:

```cpp
// firmware_compatibility_policy.cpp
ValidationResult validate_scan(const MetadataScan& scan,
                                const char* expected_device,
                                uint8_t expected_major)
{
    // Normalise both sides to uppercase
    char expected_norm[32];
    strncpy(expected_norm, expected_device, 31);
    toupper_inplace(expected_norm);

    if (strcmp(scan.device_type, expected_norm) != 0) {
        // scan.device_type = "UNKNOWN"
        // expected_norm    = "RECEIVER"
        result.allowed = false;
        result.code    = ValidationCode::DeviceTypeMismatch;
        result.message = "Firmware target mismatch (expected RECEIVER, got UNKNOWN)";
        return result;                    // ← EXITS HERE EVERY TIME
    }
    // ... version checks never reached
}
```

`Update.abort()` is called. The device is not rebooted. The upload appears to "succeed" at the HTTP
transport layer (bytes received, SHA-256 verified) but is rejected by the policy layer. From the user's
perspective the web UI may simply show a failure response with no clear indication of why.

#### Impact

- **Severity:** Every OTA upload is rejected. There is no workaround at runtime.
- **Affected builds:** All `espnowreceiver_LCD` firmware ever built without this flag.
- **Detection difficulty:** HIGH — the failure occurs silently inside the handler; the HTTP response
  contains the rejection reason but it may not be prominently surfaced in the web UI.

#### Root cause

The LCD project was created as a hardware adaptation of `espnowreceiver_2` but the build flags were not
fully carried over. The `firmware_metadata` and `FirmwareCompatibilityPolicy` systems were added to
`esp32common` after the LCD project's `platformio.ini` was initially created, and the new required flags
were not back-ported.

#### Resolution — Required

Add both missing defines to `espnowreceiver_LCD/platformio.ini` `build_flags`:

```ini
-DTARGET_DEVICE=RECEIVER
-DRECEIVER_DEVICE
```

After adding these flags, rebuild the firmware from scratch (a full clean build is required — incremental
builds may not regenerate the metadata block). The resulting binary will embed `"RECEIVER"` and the
compatibility policy will permit the upload.

---

### Finding 2 — 🔴 CRITICAL: AP-Fallback Mode Makes Device Unreachable for OTA

**Category:** WiFi architecture / provisioning state machine  
**File affected:** `espnowreceiver_LCD/src/config/wifi_setup.cpp`, `src/main.cpp`  
**Breaks:** OTA reachability on first boot or after flash-erase

#### What is happening

`espnowreceiver_2` uses a simple, unconditional STA-only WiFi strategy:

```cpp
// espnowreceiver_2/src/config/wifi_setup.cpp
void setupWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.config(Config::LOCAL_IP, Config::GATEWAY, Config::SUBNET);
    WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);
    // ... retry logic, then give up but remain in STA mode
    // Device stays on the LAN or fails to connect — never enters AP mode
}
```

Because credentials are hardcoded at compile time the device always attempts to join the home LAN. Even if
it fails, it never disappears off the LAN.

`espnowreceiver_LCD` introduced runtime-configurable credentials stored in NVS, which requires an AP
fallback for initial provisioning. The fallback is implemented as follows:

```cpp
// espnowreceiver_LCD/src/config/wifi_setup.cpp
void start_ap_fallback() {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP);                             // ← PURE AP, no STA component
    WiFi.softAP("ESP32-LCD-Setup", nullptr, 1);     // open AP, channel 1
    // Device IP: 192.168.4.1 (default ESP32 softAP subnet)
}

// espnowreceiver_LCD/src/main.cpp
bool wifi_ok = false;
if (cfg_loaded) {
    wifi_ok = WiFiSetup::setup_from_loaded_config();
}
if (!wifi_ok) {
    WiFiSetup::start_ap_fallback();         // ← triggers when NVS has no creds
}
g_espnow_transport_enabled = WiFiSetup::is_sta_connected();   // false in AP mode
```

When the device is in `WIFI_AP` mode:

- It is reachable **only** from a device that has joined the `ESP32-LCD-Setup` access point.
- It is **completely invisible** to the home LAN.
- Any OTA tool (browser, curl, PlatformIO espota) running on the home LAN **cannot reach the device**.
- ESP-NOW is explicitly disabled (`g_espnow_transport_enabled = false`).

This mode is entered on:
1. First boot after programming (NVS empty)
2. After any full flash erase (e.g. `pio run -t erase`)
3. After a deliberate credential reset

#### Compounding effect with Finding 1

Even if a developer connects their machine to the `ESP32-LCD-Setup` AP and reaches the web UI at
`192.168.4.1`, the OTA upload will still be rejected because of Finding 1. The device must be on the LAN
**and** have the correct `TARGET_DEVICE` flag before OTA can succeed.

#### Root cause

`WIFI_AP` (pure Access Point) mode was chosen for the fallback instead of `WIFI_AP_STA` (concurrent AP +
STA). This is appropriate for a provisioning-only flow where no STA credentials are known yet. However the
current implementation does not distinguish between "I have no credentials" (appropriate for pure AP) and
"I have credentials but failed to connect" (where AP+STA or a retry strategy would be safer). Both
conditions currently end up in pure AP mode.

There is a secondary issue: after the user saves credentials via the web UI (`POST /api/v1/network`), the
device should immediately attempt to transition to STA mode (or `WIFI_AP_STA`). If it does not, the user
may assume the device is now on the LAN and attempt OTA, only to find it has stayed in AP mode.

#### `_2` vs `_lcd` AP-mode behaviour comparison (stuck-mode analysis)

The two receivers differ not just in WiFi mode selection, but in their entire transition model.

| Aspect | `espnowreceiver_2` | `espnowreceiver_LCD` | OTA Impact |
|---|---|---|---|
| Credential source | Compile-time constants (`Config::WIFI_SSID`, `Config::WIFI_PASSWORD`) | Runtime NVS (`ReceiverNetworkConfig`) | LCD has provisioning state complexity |
| WiFi mode at boot | Always `WIFI_STA` | `WIFI_STA` if config loads + connects, else `WIFI_AP` | LCD can isolate itself from LAN |
| AP fallback | None | Yes (`start_ap_fallback()`) | OTA may be reachable only via AP subnet |
| Transition out of AP | Not applicable | Reboot after `/api/v1/network` save (deferred restart) | If config invalid, reboot loops back to AP |
| ESP-NOW enable condition | Always initialized in services phase | Gated by `is_sta_connected()` | In AP mode ESP-NOW is disabled in LCD |
| Webserver bootstrap | Accepts AP/STA checks, but system practically STA-only | Accepts AP/STA checks | Both start webserver, but LCD can show AP-only UI |

Why users perceive `_lcd` as “stuck”:

1. Boot path in `src/main.cpp` is one-shot: attempt STA once (`setup_from_loaded_config()`), then hard-fallback to AP.
2. In AP mode, there is no periodic background re-attempt to join STA.
3. ESP-NOW transport is disabled in AP mode (`g_espnow_transport_enabled = false`).
4. Because transmitter IP discovery rides ESP-NOW, transmitter proxy OTA also appears “stuck”.
5. Current AP-mode logging can show `0.0.0.0` for `WiFi.localIP()`, reinforcing the impression that networking is broken.

This means `_lcd` can enter a stable AP-only state and remain there indefinitely until one of these occurs:

- valid credentials are saved via `/api/v1/network` and the built-in deferred auto-reboot succeeds,
- firmware changes AP fallback policy,
- or operator manually reconfigures/reset-flashes the unit.

#### Direct confirmation of the expected boot behaviour

The statements below confirm exactly what the current `_lcd` implementation does today.

1. **First boot with no credentials:**
    - ✅ **True**: `_lcd` enters AP mode (`WIFI_AP`) with SSID `ESP32-LCD-Setup` so credentials can be entered.
    - ✅ **True**: after credentials are saved, `/api/v1/network` already triggers an automatic deferred reboot (~500 ms delay), then STA connect is attempted during the immediately following boot.
    - ✅ **True**: when STA connects successfully, receiver runs in STA mode and ESP-NOW transport is enabled.

2. **Boot with stored credentials present:**
    - ✅ **True**: `_lcd` attempts STA connection for `kConnectTimeoutMs = 30000` (30 seconds), polling every 500 ms.
    - ✅ **True**: if STA does not connect in that window, `_lcd` falls back to AP mode.

3. **Periodic reconnect while in AP fallback:**
    - ❌ **Currently false**: `_lcd` does **not** currently run a periodic STA reconnect loop after entering AP fallback.
    - ❌ **Currently false**: `_lcd` does **not** currently auto-transition from AP fallback back to STA without an external trigger
      (credential save + reboot, reboot with valid network available, or future APSTA recovery implementation).

So the requested behaviour is only partially implemented today: the first-boot provisioning path works, but
the continuous AP-fallback recovery loop is not yet implemented.

#### Resolution — Required

**Short term (clarification):** The firmware already auto-reboots after `/api/v1/network` save (deferred
task, ~500 ms). The required improvement is UX clarity: the web UI must explicitly show
"Settings saved — rebooting now" and a short countdown so users do not assume the device is stuck.

**Implementation decision (locked): Option A only**

- Keep the existing deferred auto-reboot behaviour.
- Reuse the **same countdown format and UI mechanism already used for transmitter reboot** from the receiver
  web page.
- Do not introduce any new reboot/countdown mechanism.

**Risk review (Option A):**

- Safe: preserves current bootstrap ordering and avoids runtime service reconfiguration hazards.
- Predictable: no additional lifecycle complexity for `webserver`, ESP-NOW runtime, or transmitter session state.

**Correct long-term fix:** Change `start_ap_fallback()` to use `WIFI_AP_STA` for the "credentials exist
but STA failed" case, so the device is simultaneously discoverable for provisioning **and** attempts to
rejoin the LAN. For the "no credentials at all" case, pure `WIFI_AP` is acceptable:

```cpp
void start_ap_fallback(bool has_credentials) {
    WiFi.disconnect(true, true);
    if (has_credentials) {
        // STA credentials exist but connection failed — try AP+STA so device
        // remains reachable on both the home LAN (once it reconnects) and via AP
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("ESP32-LCD-Setup", nullptr, 1);
        WiFi.begin(ReceiverNetworkConfig::getSSID(), ReceiverNetworkConfig::getPassword());
    } else {
        // No credentials at all — pure AP for provisioning only
        WiFi.mode(WIFI_AP);
        WiFi.softAP("ESP32-LCD-Setup", nullptr, 1);
    }
}
```

Pass the boolean through from `main.cpp`:

```cpp
bool wifi_ok = false;
if (cfg_loaded) {
    wifi_ok = WiFiSetup::setup_from_loaded_config();
}
if (!wifi_ok) {
    WiFiSetup::start_ap_fallback(/*has_credentials=*/cfg_loaded);
}
```

---

### Finding 3 — 🟠 MAJOR: Webserver Logs Incorrect IP Address in AP Mode

**Category:** Observability / diagnostics  
**File affected:** `espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp` (approximately line 195)  
**Breaks:** Developer's ability to diagnose OTA failures; misleading log output

#### What is happening

Both projects log the webserver's IP address at startup. `espnowreceiver_2` always calls
`WiFi.localIP()` which is correct because it is always in STA mode.

`espnowreceiver_LCD`'s webserver correctly detects that it can start in AP mode:

```cpp
// webserver_lcd/webserver.cpp
auto wifi_ready = []() {
    return WiFi.status() == WL_CONNECTED
        || WiFi.getMode() == WIFI_MODE_AP
        || WiFi.getMode() == WIFI_MODE_APSTA;
};
```

But the startup log still uses `WiFi.localIP()`:

```cpp
LOG_INFO("WEBSERVER", "Access webserver at: http://%s",
         WiFi.localIP().toString().c_str());
```

In `WIFI_MODE_AP`, `WiFi.localIP()` returns `0.0.0.0` because the STA interface has no IP. The correct
call for the AP interface is `WiFi.softAPIP()` which returns `192.168.4.1`. A developer reading the serial
log will see:

```
[WEBSERVER] Access webserver at: http://0.0.0.0
```

This erroneously suggests the webserver is unreachable, when it is actually running on `192.168.4.1`.
Developers will waste time diagnosing a network problem that does not exist, or conclude the device is
broken.

#### Resolution

```cpp
// webserver_lcd/webserver.cpp — replace the IP log line
const wifi_mode_t mode = WiFi.getMode();
const String ip = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
    ? WiFi.softAPIP().toString()
    : WiFi.localIP().toString();
LOG_INFO("WEBSERVER", "Access webserver at: http://%s", ip.c_str());
```

For `WIFI_MODE_APSTA` where both interfaces are active, both IPs should ideally be logged:

```cpp
if (mode == WIFI_MODE_APSTA) {
    LOG_INFO("WEBSERVER", "Access webserver at: http://%s (STA) or http://%s (AP)",
             WiFi.localIP().toString().c_str(),
             WiFi.softAPIP().toString().c_str());
} else if (mode == WIFI_MODE_AP) {
    LOG_INFO("WEBSERVER", "Access webserver at: http://%s (AP mode)",
             WiFi.softAPIP().toString().c_str());
} else {
    LOG_INFO("WEBSERVER", "Access webserver at: http://%s",
             WiFi.localIP().toString().c_str());
}
```

---

### Finding 4 — 🟠 MAJOR: Serial Upload Flags Present; OTA Upload Path Not Configured in PlatformIO

**Category:** PlatformIO configuration  
**File affected:** `espnowreceiver_LCD/platformio.ini`  
**Breaks:** PlatformIO `pio run -t upload` OTA path; contributes to terminal exit code 1

#### What is happening

The terminal context shows the last command run was:

```
pio run -e waveshare_esp32s3_lcd7_lvgl -j 12 -t upload -t monitor   [Exit Code: 1]
```

`espnowreceiver_LCD/platformio.ini` contains the following upload configuration:

```ini
upload_speed = 115200
upload_flags =
    --before
    default_reset
    --after
    hard_reset
```

These are `esptool.py` flags for **serial UART upload**. They are not OTA flags. PlatformIO will attempt
to auto-detect a serial port for `esptool.py` and use these flags. This will fail if:

- The board is connected via USB-CDC (which the Waveshare ESP32-S3 uses), and the CDC behaviour does not
  match `default_reset` / `hard_reset` timing
- No serial port is detected at all
- The board is on the bench connected only via LAN

`espnowreceiver_2` appears to rely exclusively on **web-UI OTA** (browser upload to
`/api/ota_upload_receiver`) rather than PlatformIO `espota`. There is no `upload_protocol = espota`
in either project.

If PlatformIO-based OTA via `espota` is desired, the configuration must be:

```ini
upload_protocol = espota
upload_port = 192.168.x.x     ; device's current STA IP
; upload_auth = password       ; if ArduinoOTA password is set
```

If only web-UI OTA is intended and `pio run -t upload` is for serial only (bench programming), then the
current `upload_flags` are correct but the serial port must be properly detected. The exit code 1 suggests
a port detection or reset-sequence failure.

#### Resolution

**If web-UI OTA is the intended method:** Remove the `-t upload` flag from the monitor command and only
use it when physically connected via USB-serial. Document this clearly.

**If PlatformIO `espota` OTA via network is desired:** Replace serial upload flags with:

```ini
upload_protocol = espota
upload_port = <device-STA-IP>
; Remove upload_speed and upload_flags (esptool-specific)
```

Note: even with `upload_protocol = espota`, Finding 1 above still applies — the PlatformIO espota
mechanism is separate from the web-UI OTA handler and uses the ArduinoOTA library, not
`api_ota_upload_receiver_handler`. These are two parallel OTA paths. Confirm which is intended.

---

### Finding 5 — 🟡 MODERATE: HTTP Header Length Limits Not Configured

**Category:** ESP-IDF HTTP server configuration  
**File affected:** `espnowreceiver_LCD/platformio.ini`  
**Breaks:** Potential silent rejection of OTA HTTP requests by the ESP-IDF httpd layer

#### What is happening

`espnowreceiver_2/platformio.ini` includes:

```ini
-D CONFIG_HTTPD_MAX_URI_LEN=1024
-D CONFIG_HTTPD_MAX_REQ_HDR_LEN=2048
```

`espnowreceiver_LCD/platformio.ini` does not define either.

The default values for ESP-IDF's `httpd` are:

| Config Key | Default |
|---|---|
| `CONFIG_HTTPD_MAX_URI_LEN` | 512 bytes |
| `CONFIG_HTTPD_MAX_REQ_HDR_LEN` | 512 bytes |

The OTA upload request includes:
- `Content-Type: application/octet-stream`
- `X-OTA-Image-SHA256: <64 hex chars>`
- `Content-Length: <up to 10 digits>`
- Browser-added headers (User-Agent, Accept, Origin, etc.)

Combined, these headers can easily exceed 512 bytes. If the total request header block exceeds
`CONFIG_HTTPD_MAX_REQ_HDR_LEN`, the ESP-IDF HTTP server rejects the request at the socket layer **before**
it reaches `api_ota_upload_receiver_handler()`. The handler never runs. The connection is closed by the
server. The browser receives a connection reset.

This is a secondary risk factor — in normal operation, Finding 1 prevents the handler from completing
anyway, but after Finding 1 is fixed, this limit could cause intermittent OTA failures depending on the
browser and the headers it sends.

#### Resolution

Add to `espnowreceiver_LCD/platformio.ini` `build_flags`:

```ini
-DCONFIG_HTTPD_MAX_URI_LEN=1024
-DCONFIG_HTTPD_MAX_REQ_HDR_LEN=2048
```

---

### Finding 6 — 🟢 MINOR: Partition Table Size Difference (Informational)

**Category:** Flash layout  
**File affected:** `espnowreceiver_LCD/partitions_8mb_ota.csv` vs `espnowreceiver_2/partitions_16mb_ota.csv`

| Project | Flash | App partition size |
|---|---|---|
| `espnowreceiver_2` | 16 MB | 0x7A0000 ≈ **7.7 MB** each |
| `espnowreceiver_LCD` | 8 MB | 0x390000 ≈ **3.7 MB** each |

This is correct and expected. The Waveshare ESP32-S3 used for the LCD variant has 8 MB flash. The smaller
partition is appropriate. There is no bug here.

**However, the developer must ensure that the LVGL + LovyanGFX + full LCD firmware binary stays under
approximately 3.7 MB**. If the binary grows to exceed this limit, OTA will fail at `Update.begin()` (size
check) rather than at the compatibility policy. This should be monitored as the LCD firmware grows.

---

### Finding 7 — 🟢 MINOR: WiFi Credential Strategy Difference (Informational)

| Project | Credential source |
|---|---|
| `espnowreceiver_2` | Hardcoded at compile time (`Config::WIFI_SSID`, etc.) |
| `espnowreceiver_LCD` | NVS (runtime configurable via `/api/v1/network`) |

The LCD project's runtime-configurable approach is architecturally superior for a deployable product. It
is not a bug. However it introduces the AP-fallback flow described in Finding 2, which must be handled
carefully to avoid disrupting OTA accessibility.


### Finding 8 — 🟠 MAJOR: Transmitter Proxy OTA Cannot Start If Receiver Is in AP-Fallback Mode

**Category:** Transmitter OTA / receiver WiFi state  
**File affected:** `espnowreceiver_LCD/src/config/wifi_setup.cpp`, `lib/webserver_lcd/api/api_control_handlers.cpp`  
**Breaks:** Transmitter OTA from the LCD receiver whenever the receiver falls back to AP mode

#### What is happening

The transmitter proxy OTA path requires:
1. The receiver to have received at least one ESP-NOW packet from the transmitter so that `TransmitterManager::isIPKnown()` returns `true`.
2. The receiver to have a routable TCP connection to the transmitter's IP address.

When the LCD receiver enters `WIFI_AP` mode (Finding 2), ESP-NOW is explicitly disabled:

```cpp
// src/main.cpp
g_espnow_transport_enabled = WiFiSetup::is_sta_connected();  // false in AP mode
```

With ESP-NOW disabled the transmitter's IP is never received, `isIPKnown()` remains `false`, and
`api_ota_upload_handler()` immediately returns HTTP 400: `"Transmitter IP unknown"`.

Even if ESP-NOW were still running in AP mode, the receiver's `WiFiClient` TCP connection to the
transmitter would fail: the transmitter is on the home LAN subnet (e.g. `192.168.1.x`) while the receiver
in pure AP mode has no interface on that subnet.

#### How this differs from `espnowreceiver_2`

`espnowreceiver_2` always starts in STA mode. ESP-NOW is always enabled alongside WiFi STA. The
transmitter's IP is received immediately on first ESP-NOW contact, and the receiver always has a route to
the transmitter's IP. Transmitter proxy OTA is therefore available as soon as the receiver joins the LAN.

In the LCD receiver this is only the case after:
1. Credentials have been provisioned via the AP config page
2. The device has rebooted into STA mode
3. The transmitter has sent at least one ESP-NOW packet

#### Resolution

The fix for Finding 2 (use `WIFI_AP_STA` for the "credentials exist but connection failed" case)
partially addresses this, as ESP-NOW can operate alongside `WIFI_AP_STA`. However, for the "no
credentials at all" case (pure `WIFI_AP`), transmitter proxy OTA will never be available — which is
acceptable since the receiver itself cannot reach the LAN.

No additional code changes are needed beyond the Finding 2 resolution, but the web UI should clearly
indicate whether the transmitter IP is known before offering the transmitter OTA upload form.

---

### Finding 9 — 🟢 MINOR: Transmitter `TARGET_DEVICE` Is Correctly Set (Informational)

**Category:** Transmitter build configuration  
**File affected:** `ESPnowtransmitter2/platformio.ini`

The transmitter project **correctly** defines both required flags:

```ini
build_flags =
    -D TRANSMITTER_DEVICE
    -D TARGET_DEVICE=TRANSMITTER
    -D FW_VERSION_MAJOR=2
    ...
```

The transmitter firmware therefore embeds `device_type = "TRANSMITTER"` in its metadata signature block.
The transmitter's `FirmwareCompatibilityPolicy::validate_scan()` will accept this binary and reject any
receiver binary accidentally uploaded via the proxy path. No changes are required in the transmitter
project.

---

### Finding 10 — 🟡 MODERATE: Missing `CONFIG_HTTPD_MAX_REQ_HDR_LEN` Affects Both OTA Paths

**Category:** ESP-IDF HTTP server configuration  
**File affected:** `espnowreceiver_LCD/platformio.ini`  
**Breaks:** Both `/api/ota_upload_receiver` and `/api/ota_upload` — httpd may silently reject large browser requests

This was identified as Finding 5 for the receiver self-OTA path. It equally affects the transmitter
proxy OTA endpoint because the browser sends the same headers (including `X-OTA-Image-SHA256`) to both
endpoints. The 512-byte default header limit applies to all httpd URIs on the receiver. Adding
`-DCONFIG_HTTPD_MAX_REQ_HDR_LEN=2048` to `build_flags` resolves the risk for both paths simultaneously.

---

## 5. Complete Failure Chain (As Experienced by Developer)

The following is the exact sequence of events that leads to OTA failure in the current LCD codebase:

```
Step 1: Developer builds espnowreceiver_LCD firmware
        → platformio.ini missing -DTARGET_DEVICE=RECEIVER
        → firmware_metadata embeds device_type = "UNKNOWN"
        → Binary produced: espnowreceiver_LCD.bin (contains "UNKNOWN")

Step 2: Device boots (possibly first boot after flash-erase)
        → NVS has no WiFi credentials
        → WiFiSetup::start_ap_fallback() called
        → WiFi.mode(WIFI_AP)
        → Device creates open AP "ESP32-LCD-Setup" at 192.168.4.1
        → Serial log shows: "Access webserver at: http://0.0.0.0"  ← MISLEADING
        → Device is NOT on home LAN

Step 3: Developer attempts OTA from home LAN
        → curl / browser cannot reach device IP
        → OTA fails at network layer (device not reachable)
        
Step 4: Developer notices AP, connects their machine to "ESP32-LCD-Setup"
        → Device now reachable at 192.168.4.1
        → Developer opens web UI, uploads espnowreceiver_LCD.bin

Step 5: api_ota_upload_receiver_handler() receives binary
        → Streams all bytes to Update.write() — succeeds
        → Computes SHA-256 — matches header
        → Scans binary metadata block
        → Calls FirmwareCompatibilityPolicy::validate_scan(scan, "RECEIVER", 2)
        → scan.device_type = "UNKNOWN", expected = "RECEIVER"
        → ValidationCode::DeviceTypeMismatch
        → Update.abort() called
        → Returns HTTP 400: "Firmware target mismatch (expected RECEIVER, got UNKNOWN)"

Step 6: Device not rebooted. Running firmware unchanged.
        Developer sees failure, may be unclear why.
        
Step 7: Developer may re-attempt upload multiple times — fails every time
        The root cause (missing build flag) persists across all attempts.
```

### 5.2 Transmitter OTA Failure Chain (Current LCD State)

```
Step 1: Developer builds ESPnowtransmitter2 firmware
     → TARGET_DEVICE=TRANSMITTER correctly set ✓
     → Binary produced: transmitter.bin (contains "TRANSMITTER")

Step 2: LCD receiver boots → enters WIFI_AP mode (NVS empty)
     → g_espnow_transport_enabled = false
     → ESP-NOW disabled — transmitter packets not received
     → TransmitterManager::isIPKnown() = false

Step 3: Developer opens browser, navigates to http://192.168.4.1/ota
     → Selects "Transmitter" target, uploads transmitter.bin

Step 4: api_ota_upload_handler() runs
     → Checks TransmitterManager::isIPKnown()
     → Returns false (ESP-NOW never received transmitter IP)
     → Returns HTTP 400: "Transmitter IP unknown"
     → Upload never reaches transmitter

Step 5: Even if receiver were in STA mode with ESP-NOW enabled:
     → TransmitterManager::isIPKnown() = true (after first ESP-NOW packet)
     → Receiver opens WiFiClient to transmitter IP — succeeds
     → Streams binary bytes to transmitter
     → Transmitter validates device_type "TRANSMITTER" ✓ — ACCEPTED
     → Transmitter applies firmware, returns HTTP 200
     → Receiver returns success to browser
     → Developer reboots transmitter via web UI

CONCLUSION: Transmitter proxy OTA will work correctly once the receiver is fixed
(Findings 1+2) and is operating in STA mode with ESP-NOW active. No changes to
the transmitter project are required. The LCD receiver's AP-fallback mode is the
only blocker for transmitter OTA beyond the receiver self-OTA fixes.
```

---

## 6. Consolidated Resolution Checklist

### Mandatory Fixes (OTA will not work without these)

| # | Change | File | Action |
|---|---|---|---|
| M1 | Add `-DTARGET_DEVICE=RECEIVER` | `espnowreceiver_LCD/platformio.ini` | Add to `build_flags` |
| M2 | Add `-DRECEIVER_DEVICE` | `espnowreceiver_LCD/platformio.ini` | Add to `build_flags` |
| M3 | After M1+M2: perform **full clean rebuild** | CLI | `pio run -e waveshare_esp32s3_lcd7_lvgl -t clean && pio run ...` |
| M4 | Document that device must be on STA (LAN) before OTA | README / web UI | Process/UX change |

### Important Fixes (OTA will work but will be fragile without these)

| # | Change | File | Action |
|---|---|---|---|
| I1 | Fix IP log in AP mode | `lib/webserver_lcd/webserver.cpp` | Use `softAPIP()` when in AP/APSTA mode |
| I2 | Add HTTP header length overrides | `espnowreceiver_LCD/platformio.ini` | Add `CONFIG_HTTPD_MAX_URI_LEN` and `CONFIG_HTTPD_MAX_REQ_HDR_LEN` |
| I3 | Improve AP fallback: WIFI_AP vs WIFI_AP_STA distinction | `src/config/wifi_setup.cpp` | Separate "no creds" from "creds but connect failed" |
| I4 | Clarify upload method in platformio.ini | `espnowreceiver_LCD/platformio.ini` | Resolve serial vs espota intent; fix exit code 1 |

### Recommended Improvements (longer term)

| # | Change | Description |
|---|---|---|
| R1 | Add `RECEIVER_LCD` as a distinct device type | Currently all LCD receiver firmware uses `TARGET_DEVICE=RECEIVER`. If LCD and non-LCD receivers should be distinguished at the OTA level, a new device type `RECEIVER_LCD` should be added to `FirmwareCompatibilityPolicy` and embedded in LCD builds. |
| R2 | Post-save reboot UX (reuse existing transmitter reboot countdown) | `/api/v1/network` already auto-reboots; web UI should show the same countdown format/mechanism used by current transmitter reboot flow, plus reconnect target. No new reboot/countdown mechanism should be introduced. |
| R3 | Add a `devices` endpoint | A `GET /api/v1/device_info` endpoint returning `device_type`, `fw_version`, `wifi_mode`, and `local_ip` would allow OTA tooling to verify device state before attempting an upload. |
| R4 | Monitor firmware binary size | As the LCD firmware grows with LVGL, LovyanGFX, and additional features, add a CI/build check that the `.bin` size remains under 3.7 MB (the OTA partition limit on 8 MB flash). |
| R5 | Add `shared_build_flags.ini` | Create a shared PlatformIO file with common required flags (`TARGET_DEVICE`, `RECEIVER_DEVICE`, HTTP server limits) that all receiver variants include, preventing future omissions. |

### Transmitter OTA — Status Per Fix Applied

The table below shows the state of transmitter proxy OTA at each stage of applying the fixes above:

| State | Receiver self-OTA | Transmitter proxy OTA | Notes |
|---|---|---|---|
| Current (no fixes applied) | ❌ Rejected (DeviceTypeMismatch) | ❌ Fails (IP unknown; AP mode; ESP-NOW off) | |
| M1+M2 applied, device still in AP mode | ✅ Would pass policy | ❌ Still fails (AP mode, no ESP-NOW) | Receiver must be serial-flashed first |
| M1+M2+I3 applied, device in STA mode | ✅ Works | ✅ Works (once transmitter sends ESP-NOW) | Primary target state |
| All fixes applied (I1+I2+I3+I4 too) | ✅ Works reliably | ✅ Works reliably | Production-ready state |

**No changes are required to `ESPnowtransmitter2`** — it is correctly configured and will accept proxy
OTA uploads once the receiver can forward them.

---

## 7. Immediate Action Plan

To unblock OTA as quickly as possible:

### Step 1 — Apply mandatory fix M1 + M2

Open `espnowreceiver_LCD/platformio.ini` and add the two lines to `build_flags`:

```ini
build_flags =
    -DCORE_DEBUG_LEVEL=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DBOARD_HAS_PSRAM
    -I ../esp32common
    -Iinclude
    -DLV_CONF_PATH=lv_conf.h
    -DFW_VERSION_MAJOR=2
    -DFW_VERSION_MINOR=0
    -DFW_VERSION_PATCH=0
    -DLOG_USE_MQTT=0
    -DTARGET_DEVICE=RECEIVER        ; ← ADD THIS
    -DRECEIVER_DEVICE               ; ← ADD THIS
    -DCONFIG_HTTPD_MAX_URI_LEN=1024
    -DCONFIG_HTTPD_MAX_REQ_HDR_LEN=2048
```

### Step 2 — Clean rebuild via serial

Flash the corrected firmware via USB-serial (not OTA — the device cannot OTA-update itself out of the
broken state):

```sh
pio run -e waveshare_esp32s3_lcd7_lvgl -t clean
pio run -e waveshare_esp32s3_lcd7_lvgl -t upload
```

Ensure the device is connected via USB-serial for this step.

### Step 3 — Verify device is on STA/LAN

After the corrected firmware boots:
- If NVS has no credentials: connect to the `ESP32-LCD-Setup` AP, navigate to `http://192.168.4.1`, save
  WiFi credentials, then reboot.
- After reboot the device should join the home LAN. Note the IP from serial monitor.

### Step 4 — Test web-UI OTA

With the device on the LAN:
1. Build the firmware again (it now embeds `"RECEIVER"`)
2. Open `http://<device-IP>` in a browser
3. Navigate to the OTA upload page
4. Upload the `.bin` — it should now be accepted

### Step 5 — Apply important fixes I1–I4

These can be applied incrementally in subsequent builds and should be part of the same PR as the mandatory
fixes.

### Step 6 — Test transmitter proxy OTA

With the receiver running corrected firmware on the LAN:
1. Power on the transmitter — it will send ESP-NOW packets to the receiver
2. Confirm in the receiver serial log that `TransmitterManager` records the transmitter IP
3. Open `http://<receiver-IP>/ota` in a browser
4. Select the **Transmitter** target
5. Build `ESPnowtransmitter2` firmware (no changes needed — `TARGET_DEVICE=TRANSMITTER` already set)
6. Upload the `.bin` — the receiver will proxy it to the transmitter; the transmitter validates and applies it
7. Click "Reboot Transmitter" — transmitter restarts into the new firmware

If Step 2 fails (transmitter IP not seen), verify:
- The transmitter is powered and transmitting ESP-NOW
- The receiver is not in AP-fallback mode (check serial log for STA connection)
- `g_espnow_transport_enabled` is `true` (set when `WiFiSetup::is_sta_connected()` returns `true`)

---

## 8. Relationship to `esp32common` OTA Infrastructure

The `FirmwareCompatibilityPolicy` system in `esp32common` is working exactly as designed. It correctly
rejected a firmware binary with an unknown device type. The problem is entirely in the `espnowreceiver_LCD`
project's failure to provide the information that the policy requires.

No changes to `esp32common` are required. The shared library did its job.

This does, however, highlight that **the policy creates a build-time contract** that every consumer of
`esp32common` must honour. That contract should be:

1. Codified in the `esp32common` README or a dedicated `CONSUMER_CHECKLIST.md`
2. Enforced by a shared PlatformIO include (see Recommendation R5)
3. Validated by a build-time assertion if possible (a `static_assert` on the `TARGET_DEVICE` macro)

---

## 9. Long-Term Implementation Plan (Production-Grade OTA)

This plan is designed to remove AP-mode dead-ends, harden both OTA paths, and prevent regression across
all receiver/transmitter variants.

### Phase A — Stabilize Boot Networking State Machine (1 sprint)

**Goal:** eliminate "stuck in AP" behaviour while preserving first-boot provisioning UX.

#### A1. Replace binary fallback with explicit network states

Introduce a small explicit state machine in `WiFiSetup`:

- `NO_CONFIG_AP_ONLY` (first boot, no credentials): `WIFI_AP`
- `CONNECTING_STA` (credentials exist, attempting join): `WIFI_STA`
- `RECOVERY_APSTA` (credentials exist but STA failed): `WIFI_AP_STA`
- `STA_CONNECTED` (normal operation): `WIFI_STA`

Persist last transition cause in NVS (`last_wifi_state`, `last_wifi_error`) for diagnostics.

**Delivery note:** keep current auto-reboot-on-save behaviour in the first implementation pass. This avoids
runtime service reconfiguration hazards while state-machine changes are being validated.

#### A2. Add periodic STA recovery attempts when in APSTA

When in `RECOVERY_APSTA`, reattempt STA join on backoff schedule (e.g. 5s, 10s, 20s, capped at 60s).
If join succeeds, optionally disable AP after a grace period.

#### A3. Keep ESP-NOW transport enabled in APSTA recovery mode

Gate ESP-NOW on "radio initialized + channel valid" rather than strict STA-connected only. This maintains
transmitter discovery and control-plane continuity while recovering network.

#### A4. Correct AP/STA observability

- Log `WiFi.softAPIP()` in AP mode
- Log both AP and STA IPs in APSTA mode
- Expose mode and IPs through API (`/api/v1/network/status`)

#### A5. Adopt graceful reconnect timing profile for `_lcd` (aligned to existing codebase patterns)

The codebase already contains stable retry/recovery timing patterns in shared timing contracts and related
network modules:

- `TimingConfig::DISCOVERY.retry_interval_ms = 5000`
- `TimingConfig::DISCOVERY.recovery_retry_delay_ms = 5000`
- `TimingConfig::DISCOVERY.recovery_timeout_ms = 60000`
- `TimingConfig::MQTT.initial_retry_delay_ms = 5000`
- `TimingConfig::MQTT.max_retry_delay_ms = 300000`
- transmitter devboard WiFi monitor pattern uses stepped reconnect intervals up to 60 s

Recommended `_lcd` WiFi/APSTA recovery timing profile:

| Parameter | Current `_lcd` | Recommended `_lcd` | Rationale |
|---|---|---|---|
| Initial STA connect timeout | 30000 ms | 30000 ms (keep) | Already aligned with `HEARTBEAT.espnow_connecting_timeout_ms` and practical boot UX |
| STA poll step | 500 ms | 500 ms (keep) | Good responsiveness with low overhead |
| APSTA retry initial delay | not implemented | 5000 ms | Aligns with discovery/MQTT retry baseline |
| APSTA retry backoff | not implemented | +5000 ms per failed cycle, capped at 60000 ms | Matches existing recovery style and prevents network thrash |
| APSTA retry timeout budget before escalation | not implemented | 60000 ms | Reuse shared recovery timeout semantic |
| ESP-NOW enable in APSTA recovery | disabled today when STA not connected | enable in APSTA, degrade features needing LAN until STA restored | Keeps transmitter discovery/control alive during recovery |
| AP disable grace after STA recovery | not implemented | 15000 ms grace, then disable AP | Prevents immediate UI disconnect while transitioning users |

Suggested recovery sequence:

1. Boot with credentials → try STA for 30 s.
2. On failure, switch to `WIFI_AP_STA` (not AP-only) and start retry timer.
3. Retry `WiFi.reconnect()` or `WiFi.begin()` every 5 s, with stepped backoff to 60 s.
4. When STA gets IP, keep AP up for 15 s grace, then disable AP and continue STA-only operation.
5. If no recovery after prolonged period (e.g. 10 min), remain in APSTA and keep low-rate retries (60 s).

This profile is intentionally conservative and follows existing codebase timing conventions rather than
introducing new aggressive intervals.

**Acceptance criteria:**

- Device with invalid credentials enters APSTA recovery, not AP-only dead-end.
- Device auto-recovers to STA without manual reboot once valid network is available.
- Logs always show reachable IP address(es).

#### Cross-system safety re-review (WiFi state machine + OTA dependencies)

The proposed plan was re-reviewed against current `_lcd` runtime wiring to ensure no hidden regressions.

1. **Bootstrap gating dependency:**
    - Current code computes `g_espnow_transport_enabled` once during boot based on `is_sta_connected()`.
    - `bootstrap_espnow_radio()` and `bootstrap_espnow_state()` are skipped when that flag is false.
    - **Implication:** if APSTA recovery later obtains STA IP without reboot, ESP-NOW may still remain uninitialized.
    - **Mitigation in plan:** keep auto-reboot as the canonical credential-transition path; introduce APSTA recovery
      only with explicit ESP-NOW bring-up logic (WP-03) and HIL validation.

2. **Transmitter proxy OTA dependency:**
    - `/api/ota_upload` requires `TransmitterManager::isIPKnown()` and TCP reachability to transmitter IP.
    - If ESP-NOW is not active, transmitter IP cache cannot refresh.
    - **Mitigation:** do not rely on AP-only operation for proxy OTA; require STA/APSTA + ESP-NOW active state.

3. **Webserver continuity:**
    - Current webserver is boot-initialized and already supports AP/APSTA/STA readiness.
    - **Risk avoided by Option A:** no runtime webserver teardown/rebind choreography is required.

4. **User flow consistency:**
    - Network save currently triggers deferred reboot via `api_network.cpp`.
    - OTA page already contains a mature reboot countdown pattern for transmitter operations.
    - **Mitigation:** reuse existing countdown UX pattern; do not add a second reboot UX mechanism.

5. **State/cache freshness after transition:**
    - Reboot-based transition naturally reinitializes WiFi, ESP-NOW runtime, and transmitter caches in known order.
    - **Mitigation:** keep reboot-first strategy for reliability; only extend runtime transitions after lifecycle tests pass.

Conclusion: the current recommendations are safe if implemented in dependency order (WP-01 → WP-03 → HIL),
with auto-reboot retained as the primary transition mechanism.

### Phase B — Unify OTA Contracts Across Projects (1 sprint)

**Goal:** prevent build-flag drift (`TARGET_DEVICE`, `RECEIVER_DEVICE`) from recurring.

#### B1. Shared build flag include

Create a shared PlatformIO include file in `esp32common` (e.g. `build_flags_common_receiver.ini`) and
`extra_configs` include it in all receiver variants.

#### B2. Build-time hard failure for missing target device

In `firmware_metadata.h` replace permissive fallback with guarded compile error for production builds:

```cpp
#ifndef TARGET_DEVICE
#error "TARGET_DEVICE must be defined (RECEIVER or TRANSMITTER)"
#endif
```

Optionally allow fallback only under explicit `DEV_ALLOW_UNKNOWN_TARGET`.

#### B3. CI metadata validation

Add CI step to parse built `.bin` metadata signature and assert expected `device_type` + `FW_VERSION_MAJOR`.

**Acceptance criteria:**

- Build fails immediately if required OTA metadata flags are missing.
- CI blocks merge if firmware metadata does not match project target.

### Phase C — Harden Receiver OTA Endpoints (1 sprint)

**Goal:** make receiver `/ota` UX deterministic and self-diagnosing.

#### C1. Preflight endpoint

Add `/api/v1/ota/preflight` returning:

- receiver mode (`STA/AP/APSTA`)
- receiver IP(s)
- transmitter IP known (`true/false`)
- ESP-NOW enabled (`true/false`)
- OTA partition free bytes
- required headers present/limits

UI should block upload with actionable reason if preflight fails.

#### C2. Header-size safety and request diagnostics

Ensure `CONFIG_HTTPD_MAX_REQ_HDR_LEN=2048` and `CONFIG_HTTPD_MAX_URI_LEN=1024` are enforced across variants.
Return structured JSON errors for malformed headers and early connection drops.

#### C3. Progress and reason-code normalization

Standardize failure enums across receiver and transmitter proxy flows:

- `ERR_DEVICE_TYPE_MISMATCH`
- `ERR_TX_IP_UNKNOWN`
- `ERR_NETWORK_MODE_AP_ONLY`
- `ERR_HEADER_LIMIT`
- `ERR_SHA256_MISMATCH`

**Acceptance criteria:**

- `/ota` page can always explain exactly why upload is blocked before transfer starts.
- Field logs show a single canonical reason code for each failed attempt.

### Phase D — Transmitter Proxy OTA Resilience (1 sprint)

**Goal:** make transmitter OTA robust even during transient link/network issues.

#### D1. Session lifecycle observability

Expose `session_active`, `expires_in_ms`, `signature_available`, `last_arm_result` via `/api/ota_status`.

#### D2. Retry policy tuning

Tune:

- challenge acquisition retries
- early-response timeout during streaming
- final response timeout

based on measured LAN latency and large-image transfer timings.

#### D3. Optional resumable proxy upload (future)

For large images, evaluate chunk-indexed resumable upload between browser and receiver, then receiver to
transmitter; only needed if field network quality is poor.

**Acceptance criteria:**

- Proxy OTA succeeds under controlled packet loss / brief AP disconnections.
- Session expiry and signature mismatch are explicit and recoverable via re-arm.

### Phase E — Operational Rollout and Regression Coverage (ongoing)

#### E1. Hardware-in-loop test matrix

Automate tests for:

1. First boot (no config) → AP provisioning → STA join → receiver OTA
2. Invalid credentials → APSTA recovery → corrected credentials → transmitter OTA
3. Missing transmitter / late transmitter arrival
4. Metadata mismatch negative tests (receiver bin to transmitter endpoint, and vice versa)

#### E2. Release gates

No release if any of the following fail:

- Receiver self-OTA pass
- Transmitter proxy OTA pass
- AP recovery to STA pass
- Metadata contract validation pass

#### E3. Documentation and runbooks

Update operator runbooks with explicit decision tree:

- "Device in AP-only" path
- "Transmitter IP unknown" path
- "Firmware target mismatch" path

### Implementation steps for long-term resolution (ordered, dependency-safe)

1. **Baseline hardening (must complete first)**
    - Apply M1/M2/I2 and reflash via serial.
    - Keep existing auto-reboot-on-network-save behavior.
    - Verify receiver boots to STA with valid credentials and both OTA paths function.

2. **UX consistency using existing mechanism only**
    - Reuse transmitter reboot countdown UI format/mechanism for post-save reboot messaging.
    - Add explicit reconnect hint (`hostname.local` and/or last known STA IP).
    - No new reboot/countdown mechanism.

3. **WiFi state-machine refactor (without runtime in-place STA transition)**
    - Implement explicit states (`NO_CONFIG_AP_ONLY`, `CONNECTING_STA`, `RECOVERY_APSTA`, `STA_CONNECTED`).
    - Preserve reboot-driven transition path as primary behaviour.
    - Add structured logging for state transitions and reason codes.

4. **APSTA recovery retries with guarded scope**
    - Add periodic retry timing profile (5s stepped to 60s cap).
    - Keep AP enabled during recovery with 15s post-recovery grace.
    - Do not declare success criteria until STA-IP acquisition is observed and stable.

5. **ESP-NOW lifecycle alignment**
    - Implement WP-03 so ESP-NOW initialization state remains correct when network mode changes.
    - Ensure transmitter discovery/IP cache refresh occurs after STA/APSTA recovery.
    - Add guards so proxy OTA is blocked with explicit reason if prerequisites are not met.

6. **OTA preflight and reason-code normalization**
    - Implement `/api/v1/ota/preflight` and standard error codes.
    - Gate upload controls in UI until preflight passes.
    - Ensure failure paths are deterministic for AP-only, IP unknown, and header-limit cases.

7. **Build/metadata contract enforcement**
    - Add shared build flag include and compile-time guardrails.
    - Add CI binary metadata scanning.
    - Block merges on metadata contract failures.

8. **Regression gates and rollout**
    - Execute HIL scenarios for AP provisioning, STA recovery, receiver OTA, and transmitter proxy OTA.
    - Require all release gates to pass before enabling broader deployment.
    - Update runbooks/support playbooks with new state/reason-code decision trees.

**Go/No-Go gates between steps:**

- Gate A (after step 2): post-save reboot UX matches transmitter reboot countdown behavior.
- Gate B (after step 5): ESP-NOW comes up reliably after network recovery and transmitter IP is re-learned.
- Gate C (after step 8): no regressions in receiver self-OTA or transmitter proxy OTA across HIL suite.

---

## 10. Implementation Backlog (Concrete Work Packages)

1. **WP-01:** Refactor `WiFiSetup` into explicit state machine (`NO_CONFIG_AP_ONLY`, `RECOVERY_APSTA`, etc.)
2. **WP-02:** Add network status API + UI banner showing current mode/IP + OTA readiness, and reuse existing transmitter-reboot countdown UI for post-save auto-reboot messaging
3. **WP-03:** Standardize `g_espnow_transport_enabled` gating for APSTA recovery mode
4. **WP-04:** Shared `extra_configs` build flags for all receiver variants
5. **WP-05:** Compile-time metadata guardrails (`#error` when `TARGET_DEVICE` undefined)
6. **WP-06:** CI binary metadata scanner and OTA contract tests
7. **WP-07:** OTA preflight and standardized error codes across both OTA endpoints
8. **WP-08:** HIL regression suite for AP/STA transitions and proxy OTA flows

---

## 11. References

| Resource | Path |
|---|---|
| OTA boot guard | `esp32common/runtime_common_utils/include/runtime_common_utils/ota_boot_guard.h` |
| Firmware metadata | `esp32common/firmware_metadata/firmware_metadata.h` |
| Compatibility policy | `esp32common/firmware_metadata/firmware_compatibility_policy.h` |
| LCD webserver | `espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp` |
| LCD OTA handler | `espnowreceiver_LCD/lib/webserver_lcd/api/api_control_handlers.cpp` |
| LCD network config API (deferred reboot on save) | `espnowreceiver_LCD/lib/webserver_lcd/api/api_network.cpp` |
| LCD WiFi setup | `espnowreceiver_LCD/src/config/wifi_setup.cpp` |
| LCD platformio.ini | `espnowreceiver_LCD/platformio.ini` |
| _2 platformio.ini | `espnowreceiver_2/platformio.ini` |
| LCD main | `espnowreceiver_LCD/src/main.cpp` |
| Transmitter OTA handler | `ESPnowtransmitter2/.../ota_upload_handler.cpp` |
| Transmitter OTA manager | `ESPnowtransmitter2/.../ota_manager.cpp` |
| Transmitter platformio.ini | `ESPnowtransmitter2/platformio.ini` |
| Transmitter manager (receiver side) | `espnowreceiver_LCD/lib/webserver_lcd/utils/transmitter_manager.cpp` |
| Shared timing contract | `esp32common/include/esp32common/config/timing_config.h` |
| Transmitter WiFi reconnect pattern reference | `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/wifi/wifi.cpp` |


*End of analysis. All findings above are based on direct code inspection of all three projects and the*
*shared esp32common library. The root cause (Finding 1) is confirmed with high confidence. Findings 2–5*
*and 8 are confirmed secondary issues that should be resolved alongside the primary fix. The transmitter*
*project itself requires no changes — it is correctly configured and will accept proxy OTA uploads as*
*soon as the receiver is able to forward them.*
