# Static IP Configuration Implementation Plan

**Date:** February 8, 2026  
**Feature:** Dynamic/Static IP Configuration for Transmitter  
**Scope:** Full bidirectional ESP-NOW configuration with NVS persistence  
**Status:** User feedback integrated - ready for implementation

---

## User Feedback Integration Summary

This plan has been updated based on comprehensive user feedback. All requested changes have been incorporated:

### ✅ Feedback Addressed

1. **Remove Hardcoded Config Entirely** (Section 9)
   - Changed from "comment out" to complete removal of Network namespace
   - NVS is now single source of truth
   - DHCP fallback on first boot until user configures

2. **No DNS in Dashboard** (Section 12)
   - Dashboard only shows IP with (S)/(D) indicator
   - DNS fields remain in settings page only

3. **Transmitter-Side Validation** (Section 3)
   - Added comprehensive IP validation before NVS save:
     - Valid IP format (not 0.0.0.0, not broadcast, not multicast)
     - Gateway in same subnet as IP
     - Subnet mask validity (contiguous 1s)

4. **Dual DNS Support** (Sections 1, 2)
   - Message structure expanded to include `dns_primary` and `dns_secondary`
   - NVS stores both DNS servers
   - Message size: 32 bytes (well under 250 limit)

5. **Version Tracking** (Sections 1, 2)
   - Added `config_version` field to messages
   - Network config version stored in NVS
   - Follows same pattern as battery settings (`settings_manager.cpp`)
   - Version increments on every save

6. **Static IP Reachability Testing** (NEW Section 17)
   - **Question:** "How would the testing if a static ip is reachable work?"
   - **Answer:** Three options provided with recommendation:
     - **Recommended:** Pre-save gateway ping test using `ESP32Ping` library
     - Alternative: Post-save validation with manual confirm
     - Alternative: Format validation only (no network test)
   - Implementation includes temporary config apply → ping gateway → rollback if failed

7. **IP Conflict Detection** (NEW Section 18)
   - **Question:** "Same with if the static IP is already in use?"
   - **Answer:** Two approaches documented:
     - **Recommended:** ICMP ping to proposed IP (simple, uses existing library)
     - Alternative: Gratuitous ARP request (more accurate but complex)
   - Implementation checks for existing device before applying config

8. **Network Diagnostics Check** (Sections 17-18)
   - **Question:** "Does the transmitter already have network diagnostics built in?"
   - **Answer:** No existing ping or diagnostic features found
   - New diagnostics added as part of this implementation

9. **Version Mechanism Research** (Sections 1-2)
   - **Request:** "Can you add the version mechanism like the other NVS items have"
   - **Implementation:** Researched `settings_manager.cpp` pattern
   - Added `network_config_version_` with same load/increment/save flow

---

## Overview

Add the ability to configure the transmitter's Ethernet connection as either **DHCP (dynamic)** or **Static IP** from the receiver's web interface. Settings will be stored in the transmitter's NVS and synchronized via ESP-NOW messages.

---

## Design Requirements

### Functional Requirements
1. ✅ Store static IP configuration in transmitter NVS
2. ✅ Toggle between DHCP and Static IP modes
3. ✅ Send network configuration via ESP-NOW (receiver → transmitter)
4. ✅ Apply configuration and persist to NVS (transmitter)
5. ✅ Display current IP mode on dashboard with (S) or (D) indicator
6. ✅ Configuration UI on transmitter settings page with toggle/slider
7. ✅ Validate IP addresses before sending
8. ✅ Handle configuration errors gracefully

### Non-Functional Requirements
- Configuration changes require transmitter reboot to apply
- Invalid IP addresses should be rejected client-side
- ESP-NOW message size must stay within limits (250 bytes)
- NVS namespace management to avoid conflicts
- UI follows existing project design patterns

---

## Architecture

### Data Flow
```
┌──────────────────────────────────────────────────────────────────┐
│ 1. User configures static IP on receiver web UI                  │
│    (Toggle, IP fields, Gateway, Subnet, DNS)                     │
└──────────────────────────────────────┬───────────────────────────┘
                                       │
                                       ▼
┌──────────────────────────────────────────────────────────────────┐
│ 2. Receiver validates and sends ESP-NOW message                  │
│    msg_network_config_update (new message type)                  │
└──────────────────────────────────┬───────────────────────────────┘
                                   │
                                   ▼
┌──────────────────────────────────────────────────────────────────┐
│ 3. Transmitter receives message and saves to NVS                 │
│    Namespace: "network", Keys: use_static, ip, gateway, etc.     │
└──────────────────────────────────┬───────────────────────────────┘
                                   │
                                   ▼
┌──────────────────────────────────────────────────────────────────┐
│ 4. Transmitter sends ACK with success/failure                    │
│    msg_network_config_ack (new message type)                     │
└──────────────────────────────────┬───────────────────────────────┘
                                   │
                                   ▼
┌──────────────────────────────────────────────────────────────────┐
│ 5. Receiver updates UI and cache                                 │
│    Shows success message, prompts user to reboot transmitter     │
└──────────────────────────────────────────────────────────────────┘
```

### On Transmitter Reboot
```
┌──────────────────────────────────────────────────────────────────┐
│ 1. Transmitter loads network config from NVS                     │
│    Check "use_static_ip" flag                                    │
└──────────────────────────────────┬───────────────────────────────┘
                                   │
                                   ▼
┌──────────────────────────────────────────────────────────────────┐
│ 2. Apply configuration to Ethernet                               │
│    If static: ETH.config(ip, gateway, subnet, dns)              │
│    If DHCP: ETH.begin() (default DHCP)                          │
└──────────────────────────────────┬───────────────────────────────┘
                                   │
                                   ▼
┌──────────────────────────────────────────────────────────────────┐
│ 3. Send new IP info to receiver via existing msg_packet flow    │
│    Receiver updates TransmitterManager cache                     │
└──────────────────────────────────────────────────────────────────┘
```

---

## IP Address Format Standards & Task Priority

### IP Address Storage and Display - Industry Best Practices

#### Storage Format (In Memory & NVS)
**Standard:** `uint8_t[4]` array (4 bytes)

**Why Array Over String:**
- ✅ **Compact:** 4 bytes vs. 15 bytes for string "255.255.255.255"
- ✅ **Efficient:** Direct byte access for network operations
- ✅ **Standard:** Matches ESP32 `IPAddress` class internal representation
- ✅ **Fast:** No parsing overhead when converting to/from `IPAddress`
- ✅ **Binary Protocol:** ESP-NOW messages are binary - array is native format

**Implementation:**
```cpp
// In NVS storage
uint8_t ip[4] = {192, 168, 1, 100};
prefs.putBytes("ip", ip, 4);

// In ESP-NOW messages
typedef struct {
    uint8_t ip[4];        // Each octet 0-255
    uint8_t gateway[4];
    uint8_t subnet[4];
    // ...
} network_config_update_t;

// In EthernetManager members
IPAddress static_ip_;     // ESP32's IPAddress class (stores as uint32_t internally)
```

#### Display Format (Web UI & Logs)
**Standard:** Dotted decimal notation string (e.g., "192.168.1.100")

**Why This Format:**
- ✅ **Human-readable:** Universal understanding (RFC 791)
- ✅ **Web standard:** HTML/JavaScript uses string format
- ✅ **Validation-friendly:** Easy to validate each octet (0-255)
- ✅ **Logging clarity:** Readable in serial monitor logs

**Conversion Methods:**

**Array → String (for display):**
```cpp
// C++ (transmitter/receiver logs)
uint8_t ip[4] = {192, 168, 1, 100};
LOG_INFO("IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

// Using IPAddress class
IPAddress ipAddr(ip[0], ip[1], ip[2], ip[3]);
LOG_INFO("IP: %s", ipAddr.toString().c_str());

// In JSON API response
snprintf(json_buf, sizeof(json_buf), 
         "{\"ip\":\"%d.%d.%d.%d\"}", 
         ip[0], ip[1], ip[2], ip[3]);
```

**String → Array (from web UI):**
```cpp
// C++ (API handler parsing)
const char* ip_str = "192.168.1.100";
IPAddress ip;
if (ip.fromString(ip_str)) {  // Validates format
    msg.ip[0] = ip[0];
    msg.ip[1] = ip[1];
    msg.ip[2] = ip[2];
    msg.ip[3] = ip[3];
}

// JavaScript (web UI input)
const ip = `${ip1}.${ip2}.${ip3}.${ip4}`;  // Build string from 4 inputs
// Send to API as string, server converts to array
```

#### Web UI Input Format
**Standard:** 4 separate numeric input fields (0-255 each) with visual dots

**Why Separate Fields:**
- ✅ **Validation per octet:** Browser enforces min=0, max=255 automatically
- ✅ **User-friendly:** Clear which part is being edited
- ✅ **Mobile-friendly:** Numeric keyboard appears on mobile devices
- ✅ **Visual clarity:** Dots between fields make format obvious
- ✅ **Prevents errors:** Can't enter "999" or invalid characters

**HTML Implementation:**
```html
<div class='ip-input'>
    <input class='octet' type='number' id='ip1' min='0' max='255' value='192' />
    <span class='dot'>.</span>
    <input class='octet' type='number' id='ip2' min='0' max='255' value='168' />
    <span class='dot'>.</span>
    <input class='octet' type='number' id='ip3' min='0' max='255' value='1' />
    <span class='dot'>.</span>
    <input class='octet' type='number' id='ip4' min='0' max='255' value='100' />
</div>
```

**JavaScript Aggregation:**
```javascript
// Convert 4 separate inputs into dotted string
const ip = `${document.getElementById('ip1').value}.` +
           `${document.getElementById('ip2').value}.` +
           `${document.getElementById('ip3').value}.` +
           `${document.getElementById('ip4').value}`;

// Parse received dotted string into 4 inputs
const octets = data.ip.split('.');
for (let i = 0; i < 4; i++) {
    document.getElementById(`ip${i+1}`).value = octets[i];
}
```

### Task Priority Considerations (Transmitter)

**CRITICAL:** Network configuration handling must NOT block or interfere with primary control functions.

#### Priority Levels

**Level 1 - HIGHEST (Hardware Interrupt):**
- ESP-NOW receive callbacks (`on_data_recv`)
- Ethernet interrupt handlers
- Timer interrupts

**Level 2 - HIGH (Control Loop):**
- Main control logic (sensor reading, actuator control)
- Critical ESP-NOW sends (control commands)
- Safety monitoring

**Level 3 - MEDIUM (Network Configuration):**
- Network config message processing ← THIS FEATURE
- NVS write operations
- Gateway reachability tests
- IP conflict detection

**Level 4 - LOW (Background Tasks):**
- Web UI updates
- Non-critical logging
- Diagnostics

#### Implementation Strategy

**Use FreeRTOS Task Priorities:**
```cpp
// In main.cpp or task_config.h

// Existing critical tasks
#define CONTROL_TASK_PRIORITY    5    // Main control loop (HIGH)
#define ESPNOW_TASK_PRIORITY     4    // ESP-NOW processing (MEDIUM-HIGH)

// Network config task (MEDIUM - lower than control)
#define NETWORK_CONFIG_TASK_PRIORITY  3  // Process config messages

// Background tasks
#define LOGGING_TASK_PRIORITY    2    // Low priority
```

**Defer Heavy Operations:**
```cpp
// In message_handler.cpp (runs in ESP-NOW callback context)
void handle_network_config_update(const network_config_update_t* msg) {
    // Quick validation only (< 1ms)
    if (use_static && ip[0] == 0) {
        send_network_config_ack(false, "Invalid IP");
        return;  // Fast exit
    }
    
    // Queue heavy operations for background task
    network_config_queue.push(*msg);  // Non-blocking queue
    xTaskNotifyGive(network_config_task_handle);  // Wake config task
}

// Separate FreeRTOS task handles heavy operations
void network_config_task(void* params) {
    while (1) {
        // Wait for notification (blocks, no CPU usage)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        if (!network_config_queue.empty()) {
            auto msg = network_config_queue.front();
            network_config_queue.pop();
            
            // Heavy operations here (won't block ESP-NOW or control loop):
            // - Gateway ping test (500ms-2s)
            // - IP conflict detection (500ms)
            // - NVS write (10-100ms)
            
            if (testStaticIPReachability(...)) {
                if (!checkIPConflict(...)) {
                    saveNetworkConfig(...);  // NVS write
                    send_network_config_ack(true, "OK");
                } else {
                    send_network_config_ack(false, "IP conflict");
                }
            } else {
                send_network_config_ack(false, "Gateway unreachable");
            }
        }
    }
}
```

**Task Creation:**
```cpp
// In main.cpp setup()
void setup() {
    // ... existing initialization ...
    
    // Create network config task with MEDIUM priority
    xTaskCreatePinnedToCore(
        network_config_task,           // Task function
        "NetConfig",                   // Name
        4096,                          // Stack size
        NULL,                          // Parameters
        NETWORK_CONFIG_TASK_PRIORITY,  // Priority (3 - medium)
        &network_config_task_handle,   // Task handle
        0                              // Core 0 (Core 1 for critical control)
    );
    
    LOG_INFO("Network config task started (priority %d)", NETWORK_CONFIG_TASK_PRIORITY);
}
```

#### Why This Approach

✅ **Non-blocking:** ESP-NOW callback returns immediately (< 1ms)  
✅ **Isolated:** Config processing in separate task, won't hang control loop  
✅ **Prioritized:** Control tasks always run first if ready  
✅ **Timeout-safe:** Ping tests won't block critical operations  
✅ **Clean architecture:** Separation of concerns (callback vs. processing)  

#### Timing Budget (Worst Case)

| Operation | Time | Blocks Critical Tasks? |
|-----------|------|------------------------|
| ESP-NOW callback | < 1ms | ❌ No (quick validation only) |
| Queue message | < 0.1ms | ❌ No (non-blocking queue) |
| Gateway ping test | 500-2000ms | ❌ No (separate task) |
| IP conflict check | 500ms | ❌ No (separate task) |
| NVS write | 10-100ms | ❌ No (separate task) |
| Send ACK | 1-10ms | ❌ No (async ESP-NOW) |

**Total impact on critical tasks: < 1ms** ✅

---

## Implementation Details

## 1. ESP-NOW Protocol Changes

### File: `esp32common/espnow_transmitter/espnow_common.h`

**Add new message types:**
```cpp
enum msg_type : uint8_t {
    // ... existing types ...
    
    // Network configuration messages (add after existing config messages)
    msg_network_config_update,      // Update network configuration (receiver → transmitter)
    msg_network_config_ack,         // Network config update ACK (transmitter → receiver)
};
```

**Add message structures (after existing structures ~line 400):**
```cpp
// Network configuration update message
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_network_config_update
    uint8_t use_static_ip;       // 0 = DHCP, 1 = Static
    uint8_t ip[4];               // Static IP octets (192.168.1.100)
    uint8_t gateway[4];          // Gateway octets
    uint8_t subnet[4];           // Subnet mask octets
    uint8_t dns_primary[4];      // Primary DNS server octets
    uint8_t dns_secondary[4];    // Secondary DNS server octets (USER FEEDBACK: provision for second DNS)
    uint32_t config_version;     // Version number (USER FEEDBACK: use version mechanism)
    uint16_t checksum;           // Simple checksum for integrity
} network_config_update_t;

// Network configuration ACK
typedef struct __attribute__((packed)) {
    uint8_t type;                // msg_network_config_ack
    uint8_t success;             // 0 = failed, 1 = success
    uint8_t use_static_ip;       // Echo back the mode
    uint8_t ip[4];               // Echo back the IP
    uint32_t config_version;     // Echo back the version
    char message[32];            // Error message or "OK"
} network_config_ack_t;
```

**Message size validation:**
- `network_config_update_t`: 1 + 1 + 4*6 + 4 + 2 = 32 bytes ✅ (well under 250 byte limit)
- `network_config_ack_t`: 1 + 1 + 1 + 4 + 4 + 32 = 43 bytes ✅

---

## 2. Transmitter - NVS Storage

### File: `ESPnowtransmitter2/src/network/ethernet_manager.h`

**Add to class:**
```cpp
class EthernetManager {
public:
    // ... existing methods ...
    
    // Network configuration management_primary, IPAddress dns_secondary);
    bool isStaticIP() const { return use_static_ip_; }
    uint32_t getNetworkConfigVersion() const { return network_config_version_; }
    
    // Getters for DNS (USER FEEDBACK: provision for second DNS)
    IPAddress getDNSPrimary() const { return static_dns_primary_; }
    IPAddress getDNSSecondary() const { return static_dns_secondary_; }
    
private:
    // ... existing members ...
    
    // Network configuration
    bool use_static_ip_ = false;
    IPAddress static_ip_;
    IPAddress static_gateway_;
    IPAddress static_subnet_;
    IPAddress static_dns_primary_;
    IPAddress static_dns_secondary_;
    uint32_t network_config_version_ = 0;  // USER FEEDBACK: version tracking like battery settings
    IPAddress static_gateway_;
    IPAddress static_subnet_;
    IPAddress static_dns_;
};
```

### File: `ESPnowtransmitter2/src/network/ethernet_manager.cpp`

**NVS n_primary"` → uint32_t (USER FEEDBACK: primary DNS)
- `"dns_secondary"` → uint32_t (USER FEEDBACK: secondary DNS)
- `"version"` → uint32_t (USER FEEDBACK: version tracking like battery settings)"network"`

**NVS keys:**
- `"use_static"` → uint8_t (0 or 1)
- `"ip"` → uint32_t (IP address as 32-bit integer)
- `"gateway"` → uint32_t
- `"subnet"` → uint32_t
- `"dns"` → uint32_t

**Implement methods:**
```cpp
bool EthernetManager::loadNetworkConfig() {
    Preferences prefs;
    if (!prefs.begin("network", true)) {  // read-only
        LOG_WARN("[ETH] Failed to open NVS for network config - using DHCP");
        return false;
    }
    
    use_static_ip_ = prefs.getUChar("use_static", 0);  // Default DHCP
    network_config_version_ = prefs.getUInt("version", 0);
    
    if (use_static_ip_) {
        uint32_t ip = prefs.getUInt("ip", 0);
        uint32_t gateway = prefs.getUInt("gateway", 0);
        uint32_t subnet = prefs.getUInt("subnet", 0);
        uint32_t dns_primary = prefs.getUInt("dns_primary", 0);
        uint32_t dns_secondary = prefs.getUInt("dns_secondary", 0);
        
        if (ip == 0 || gateway == 0) {
            LOG_ERROR("[ETH] Invalid static IP in NVS, falling back to DHCP");
            use_static_ip_ = false;
            prefs.end();
            return false;
        }
        
        static_ip_ = IPAddress(ip);
        static_gateway_ = IPAddress(gateway);
        static_subnet_ = IPAddress(subnet);
        static_dns_primary_ = IPAddress(dns_primary);
        static_dns_secondary_ = IPAddress(dns_secondary);
        
        LOG_INFO("[ETH] Loaded static IP config from NVS (v%u): %s", 
                 network_config_version_, static_ip_.toString().c_str());
    } else {
        LOG_INFO("[ETH] Using DHCP (from NVS, v%u)", network_config_version_);
    }
    
    prefs.end();
    return true;
}

bool EthernetManager::saveNetworkConfig(bool use_static, IPAddress ip, 
                                        IPAddress gateway, IPAddress subnet, 
                                        IPAddress dns_primary, IPAddress dns_secondary) {
    Preferences prefs;
    if (!prefs.begin("network", false)) {  // read-write
        LOG_ERROR("[ETH] Failed to open NVS for saving network config");
        return false;
    }
    
    // Increment version (like battery settings)
    network_config_version_++;
    
    prefs.putUChar("use_static", use_static ? 1 : 0);
    prefs.putUInt("version", network_config_version_);
    
    if (use_static) {
        prefs.putUInt("ip", static_cast<uint32_t>(ip));
        prefs.putUInt("gateway", static_cast<uint32_t>(gateway));
        prefs.putUInt("subnet", static_cast<uint32_t>(subnet));
        prefs.putUInt("dns_primary", static_cast<uint32_t>(dns_primary));
        prefs.putUInt("dns_secondary", static_cast<uint32_t>(dns_secondary));
        
        LOG_INFO("[ETH] Saved static IP config to NVS (v%u): %s", 
                 network_config_version_, ip.toString().c_str());
    } else {
        LOG_INFO("[ETH] Saved DHCP config to NVS (v%u)", network_config_version_);
    }
    
    prefs.end();
    
    // Update internal state
    use_static_ip_ = use_static;
    static_ip_ = ip;
    static_gateway_ = gateway;
    static_subnet_ = subnet;
    static_dns_primary_ = dns_primary;
    static_dns_secondary_ = dns_secondary;
    
    return true;
}
```

**Update `init()` method:**
```cpp
bool EthernetManager::init() {
    LOG_DEBUG("Initializing Ethernet for Olimex ESP32-POE-ISO (WROVER)...");
    
    // Load network configuration from NVS
    loadNetworkConfig();
    
    // Register event handler
    WiFi.onEvent(event_handler);
    
    // Hardware reset sequence for PHY
    pinMode(hardware::ETH_POWER_PIN, OUTPUT);
    digitalWrite(hardware::ETH_POWER_PIN, LOW);
    delay(10);
    digitalWrite(hardware::ETH_POWER_PIN, HIGH);
    delay(150);
    
    // Initialize Ethernet with GPIO0 clock (WROVER requirement)
    if (!ETH.begin(hardware::PHY_ADDR, 
                   hardware::ETH_POWER_PIN, 
                   hardware::ETH_MDC_PIN, 
                   hardware::ETH_MDIO_PIN, 
                   ETH_PHY_LAN8720,
                   ETH_CLOCK_GPIO0_OUT)) {
        LOG_ERROR("[ETH] Failed to initialize Ethernet");
        return false;
    }
    
    // Configure IP settings (now from NVS - no hardcoded fallback)
    if (use_static_ip_) {
        LOG_INFO("[ETH] Applying static IP (v%u): %s", network_config_version_, static_ip_.toString().c_str());
        // Note: ETH.config() only accepts primary DNS, we'll use dns_primary_
        ETH.config(static_ip_, static_gateway_, static_subnet_, static_dns_primary_);
    } else {
        LOG_INFO("[ETH] Using DHCP");
        // DHCP is default - no config() call needed
    }
    
    LOG_INFO("[ETH] Ethernet initialization started (async)");
    delay(1000);
    return true;
}
```

---

## 3. Transmitter - ESP-NOW Message Handler

### File: `ESPnowtransmitter2/src/espnow/message_handler.cpp`

**Add handler registration (in `register_message_handlers()`):**
```cpp
void register_message_handlers() {
    // ... existing handlers ...
    
    // Network configuration handler
    MessageRouter::registerRoute(msg_network_config_update, 
                                 subtype_none, 
                                 handle_network_config_update);
}
```

**Add handler function:**
```cpp
#include "../network/ethernet_manager.h"

void handle_network_config_update(const uint8_t* mac, const uint8_t* data, int len) {
    if (len != sizeof(network_config_update_t)) {
        LOG_ERROR("[NET_CFG] Invalid network config message size: %d", len);
        send_network_config_ack(false, "Invalid message size");
        return;
    }
    
    const network_config_update_t* msg = reinterpret_cast<const network_config_update_t*>(data);
    
    // Verify checksum
    uint16_t calc_checksum = 0;
    for (int i = 0; i < sizeof(network_config_update_t) - 2; i++) {
        calc_checksum ^= data[i];
    }
    
    if (calc_checksum != msg->checksum) {
        LOG_ERROR("[NET_CFG] Checksum mismatch");
        send_network_config_ack(false, "Checksum error");
        return;
    }
    
    // Convert octets to IPAddress
    IPAddress ip(msg->ip[0], msg->ip[1], msg->ip[2], msg->ip[3]);
    IPAddress gateway(msg->gateway[0], msg->gateway[1], msg->gateway[2], msg->gateway[3]);
    IPAddress subnet(msg->subnet[0], msg->subnet[1], msg->subnet[2], msg->subnet[3]);
    IPAddress dns(msg->dns[0], msg->dns[1], msg->dns[2], msg->dns[3]);
    
    bool use_static = (msg->use_static_ip != 0);
    
    LOG_INFO("[NET_CFG] Received network config update:");
    LOG_INFO("[NET_CFG]   Mode: %s", use_static ? "Static" : "DHCP");
    if (use_static) {
        LOG_INFO("[NET_CFG]   IP: %s", ip.toString().c_str());
        LOG_INFO("[NET_CFG]   Gateway: %s", gateway.toString().c_str());
        LOG_INFO("[NET_CFG]   Subnet: %s", subnet.toString().c_str());
        LOG_INFO("[NET_CFG]   DNS: %s", dns.toString().c_str());
    }
    
    // Validate static IP settings
    if (use_static) {
        // USER FEEDBACK: Validation on transmitter
        
        // 1. Check for valid IP and gateway (not 0.0.0.0)
        if (ip[0] == 0 || gateway[0] == 0) {
            LOG_ERROR("[NET_CFG] Invalid static IP or gateway (cannot be 0.0.0.0)");
            send_network_config_ack(false, "Invalid IP/Gateway");
            return;
        }
        
        // 2. Check IP is not broadcast (255.255.255.255)
        if (ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255) {
            LOG_ERROR("[NET_CFG] IP cannot be broadcast address");
            send_network_config_ack(false, "IP is broadcast");
            return;
        }
        
        // 3. Check IP is not multicast (224.0.0.0 - 239.255.255.255)
        if (ip[0] >= 224 && ip[0] <= 239) {
            LOG_ERROR("[NET_CFG] IP cannot be multicast address");
            send_network_config_ack(false, "IP is multicast");
            return;
        }
        
        // 4. Check IP and gateway are in same subnet
        bool same_subnet = true;
        for (int i = 0; i < 4; i++) {
            if ((ip[i] & msg->subnet[i]) != (gateway[i] & msg->subnet[i])) {
                same_subnet = false;
                break;
            }
        }
        if (!same_subnet) {
            LOG_WARN("[NET_CFG] IP and gateway not in same subnet - may cause routing issues");
            // Warning only, not fatal
        }
        
        // 5. Check subnet mask is valid (contiguous 1s followed by 0s)
        uint32_t subnet_val = (msg->subnet[0] << 24) | (msg->subnet[1] << 16) | 
                             (msg->subnet[2] << 8) | msg->subnet[3];
        // Invert and add 1, should be a power of 2 if valid
        uint32_t inverted = ~subnet_val + 1;
        if ((inverted & (inverted - 1)) != 0 && inverted != 0) {
            LOG_ERROR("[NET_CFG] Invalid subnet mask (not contiguous)");
            send_network_config_ack(false, "Invalid subnet mask");
            return;
        }
    }
    
    // Save to NVS (includes version increment)
    if (EthernetManager::instance().saveNetworkConfig(use_static, ip, gateway, subnet, dns)) {
        LOG_INFO("[NET_CFG] ✓ Configuration saved to NVS");
        send_network_config_ack(true, "OK - reboot required");
    } else {
        LOG_ERROR("[NET_CFG] ✗ Failed to save configuration");
        send_network_config_ack(false, "NVS save failed");
    }
}

void send_network_config_ack(bool success, const char* message) {
    network_config_ack_t ack;
    memset(&ack, 0, sizeof(ack));
    
    ack.type = msg_network_config_ack;
    ack.success = success ? 1 : 0;
    ack.use_static_ip = EthernetManager::instance().isStaticIP() ? 1 : 0;
    
    IPAddress ip = EthernetManager::instance().get_local_ip();
    ack.ip[0] = ip[0];
    ack.ip[1] = ip[1];
    ack.ip[2] = ip[2];
    ack.ip[3] = ip[3];
    
    strncpy(ack.message, message, sizeof(ack.message) - 1);
    
    // Get receiver MAC (assumes already known from PeerManager)
    const uint8_t* receiver_mac = PeerManager::getPeerMAC();
    if (receiver_mac) {
        esp_err_t result = esp_now_send(receiver_mac, (const uint8_t*)&ack, sizeof(ack));
        if (result == ESP_OK) {
            LOG_INFO("[NET_CFG] Sent ACK: %s", message);
        } else {
            LOG_ERROR("[NET_CFG] Failed to send ACK: %s", esp_err_to_name(result));
        }
    }
}
```

---

## 4. Receiver - TransmitterManager Cache

### File: `espnowreciever_2/lib/webserver/utils/transmitter_manager.h`

**Add to class:**
```cpp
class TransmitterManager {
public:
    // ... existing methods ...
    
    // Network configuration
    static bool isStaticIP();
    static void setNetworkMode(bool is_static);
    
private:
    // ... existing members ...
    
    static bool is_static_ip_;
};
```

### File: `espnowreciever_2/lib/webserver/utils/transmitter_manager.cpp`

**Add static member:**
```cpp
bool TransmitterManager::is_static_ip_ = false;

bool TransmitterManager::isStaticIP() {
    return is_static_ip_;
}

void TransmitterManager::setNetworkMode(bool is_static) {
    is_static_ip_ = is_static;
}
```

---

## 5. Receiver - ESP-NOW Handler

### File: `espnowreciever_2/src/espnow/espnow_callbacks.cpp`

**Add handler registration:**
```cpp
void register_message_handlers() {
    // ... existing handlers ...
    
    MessageRouter::registerRoute(msg_network_config_ack, 
                                 subtype_none, 
                                 handle_network_config_ack);
}
```

**Add handler function:**
```cpp
void handle_network_config_ack(const uint8_t* mac, const uint8_t* data, int len) {
    if (len != sizeof(network_config_ack_t)) {
        LOG_ERROR("[NET_CFG] Invalid ACK size: %d", len);
        return;
    }
    
    const network_config_ack_t* ack = reinterpret_cast<const network_config_ack_t*>(data);
    
    bool success = (ack->success != 0);
    bool is_static = (ack->use_static_ip != 0);
    
    LOG_INFO("[NET_CFG] Received ACK: %s", ack->message);
    LOG_INFO("[NET_CFG]   Success: %s", success ? "Yes" : "No");
    LOG_INFO("[NET_CFG]   Mode: %s", is_static ? "Static" : "DHCP");
    
    // Update cache
    TransmitterManager::setNetworkMode(is_static);
    
    // TODO: Notify web UI via SSE or callback
    // For now, the UI will refresh and see the updated config
}
```

---

## 6. Receiver - API Endpoints

### File: `espnowreciever_2/lib/webserver/api/api_handlers.cpp`

**Add new endpoint handlers:**

```cpp
// Get network configuration from transmitter
static esp_err_t api_get_network_config_handler(httpd_req_t *req) {
    char json[512];
    
    if (!TransmitterManager::isMACKnown()) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Transmitter not connected\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    const uint8_t* ip = TransmitterManager::getIP();
    const uint8_t* gateway = TransmitterManager::getGateway();
    const uint8_t* subnet = TransmitterManager::getSubnet();
    bool is_static = TransmitterManager::isStaticIP();
    
    snprintf(json, sizeof(json),
             "{"
             "\"success\":true,"
             "\"use_static_ip\":%s,"
             "\"ip\":\"%d.%d.%d.%d\","
             "\"gateway\":\"%d.%d.%d.%d\","
             "\"subnet\":\"%d.%d.%d.%d\""
             "}",
             is_static ? "true" : "false",
             ip[0], ip[1], ip[2], ip[3],
             gateway[0], gateway[1], gateway[2], gateway[3],
             subnet[0], subnet[1], subnet[2], subnet[3]);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Save network configuration and send to transmitter
static esp_err_t api_save_network_config_handler(httpd_req_t *req) {
    char json[256];
    char buf[512];
    int ret, remaining = req->content_len;
    
    if (remaining == 0 || remaining > sizeof(buf) - 1) {
        LOG_ERROR("API: Invalid request size: %d", remaining);
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Invalid request size\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        LOG_ERROR("API: Failed to read request body");
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Failed to read request\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    buf[ret] = '\0';
    
    // Parse JSON
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        LOG_ERROR("API: JSON parse error: %s", error.c_str());
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"JSON parse error\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    // Extract parameters
    bool use_static = doc["use_static_ip"] | false;
    const char* ip_str = doc["ip"] | "192.168.1.100";
    const char* gateway_str = doc["gateway"] | "192.168.1.1";
    const char* subnet_str = doc["subnet"] | "255.255.255.0";
    const char* dns_str = doc["dns"] | "192.168.1.1";
    
    LOG_INFO("API: Network config - Mode: %s", use_static ? "Static" : "DHCP");
    if (use_static) {
        LOG_INFO("API:   IP: %s", ip_str);
        LOG_INFO("API:   Gateway: %s", gateway_str);
        LOG_INFO("API:   Subnet: %s", subnet_str);
        LOG_INFO("API:   DNS: %s", dns_str);
    }
    
    // Parse IP addresses
    IPAddress ip, gateway, subnet, dns;
    if (!ip.fromString(ip_str) || !gateway.fromString(gateway_str) || 
        !subnet.fromString(subnet_str) || !dns.fromString(dns_str)) {
        LOG_ERROR("API: Invalid IP address format");
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Invalid IP address format\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    // Create ESP-NOW message
    network_config_update_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = msg_network_config_update;
    msg.use_static_ip = use_static ? 1 : 0;
    msg.ip[0] = ip[0]; msg.ip[1] = ip[1]; msg.ip[2] = ip[2]; msg.ip[3] = ip[3];
    msg.gateway[0] = gateway[0]; msg.gateway[1] = gateway[1]; msg.gateway[2] = gateway[2]; msg.gateway[3] = gateway[3];
    msg.subnet[0] = subnet[0]; msg.subnet[1] = subnet[1]; msg.subnet[2] = subnet[2]; msg.subnet[3] = subnet[3];
    msg.dns[0] = dns[0]; msg.dns[1] = dns[1]; msg.dns[2] = dns[2]; msg.dns[3] = dns[3];
    
    // Calculate checksum
    msg.checksum = 0;
    uint8_t* bytes = (uint8_t*)&msg;
    for (size_t i = 0; i < sizeof(msg) - 2; i++) {
        msg.checksum ^= bytes[i];
    }
    
    // Send to transmitter
    if (!TransmitterManager::isMACKnown()) {
        LOG_ERROR("API: Transmitter not connected");
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Transmitter not connected\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&msg, sizeof(msg));
    if (result == ESP_OK) {
        LOG_INFO("API: ✓ Network config sent to transmitter");
        snprintf(json, sizeof(json), 
                 "{\"success\":true,\"message\":\"Configuration sent - reboot transmitter to apply\"}");
    } else {
        LOG_ERROR("API: ✗ Failed to send config: %s", esp_err_to_name(result));
        snprintf(json, sizeof(json), 
                 "{\"success\":false,\"message\":\"ESP-NOW send failed: %s\"}", 
                 esp_err_to_name(result));
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}
```

**Register handlers in `register_all_api_handlers()`:**
```cpp
int register_all_api_handlers(httpd_handle_t server) {
    int count = 0;
    
    httpd_uri_t handlers[] = {
        // ... existing handlers ...
        {.uri = "/api/get_network_config", .method = HTTP_GET, .handler = api_get_network_config_handler, .user_ctx = NULL},
        {.uri = "/api/save_network_config", .method = HTTP_POST, .handler = api_save_network_config_handler, .user_ctx = NULL},
        // ... rest of handlers ...
    };
    
    // ... rest of function ...
}
```

**Update handler count in `webserver.cpp`:**
```cpp
const int EXPECTED_HANDLER_COUNT = 29;  // Was 27, now +2 for network config
```

---

## 7. Dashboard Page - Static/DHCP Indicator

### File: `espnowreciever_2/lib/webserver/pages/dashboard_page.cpp`

**Update the transmitter card IP display:**
```cpp
// In the dashboard_handler function, change IP display line from:
<span id='txIP' style='font-family: monospace; color: #fff;'>...</span>

// To:
<span id='txIP' style='font-family: monospace; color: #fff;'>...</span>
<span id='txIPMode' style='color: #888; font-size: 11px; margin-left: 5px;'></span>
```

**Update the auto-refresh JavaScript:**
```javascript
// Auto-refresh dashboard data every 5 seconds
setInterval(async function() {
    try {
        const response = await fetch('/api/dashboard_data');
        const data = await response.json();
        
        // Update transmitter status
        if (data.transmitter) {
            const tx = data.transmitter;
            // ... existing status updates ...
            
            // Update IP with mode indicator
            if (tx.ip && tx.ip !== 'Unknown') {
                const ipMode = tx.is_static ? ' (S)' : ' (D)';
                document.getElementById('txIP').textContent = tx.ip;
                document.getElementById('txIPMode').textContent = ipMode;
            }
        }
    } catch (e) {
        console.error('Failed to update dashboard:', e);
    }
}, 5000);
```

**Update `/api/dashboard_data` to include `is_static` flag:**
```cpp
// In api_dashboard_data_handler(), add:
snprintf(json, sizeof(json),
         "{"
         "\"receiver\":{...},"
         "\"transmitter\":{"
         "\"connected\":%s,"
         "\"status\":\"%s\","
         "\"ip\":\"%s\","
         "\"mac\":\"%s\","
         "\"firmware\":\"%s\","
         "\"is_static\":%s"  // <-- ADD THIS
         "}"
         "}",
         // ... existing params ...
         transmitter_connected ? "true" : "false",
         transmitter_status.c_str(),
         transmitter_ip.c_str(),
         transmitter_mac.c_str(),
         transmitter_firmware.c_str(),
         TransmitterManager::isStaticIP() ? "true" : "false");  // <-- ADD THIS
```

---

## 8. Transmitter Settings Page - Network Configuration UI

### File: `espnowreciever_2/lib/webserver/pages/settings_page.cpp`

**Replace the existing "Transmitter Network Status" and "Ethernet Static IP Configuration" sections with:**

```html
<div class='settings-card'>
    <h3>Network Configuration</h3>
    
    <!-- Network Mode Toggle -->
    <div class='settings-row' style='background: rgba(255,255,255,0.05); padding: 15px; margin-bottom: 15px;'>
        <label style='font-size: 16px; font-weight: bold;'>IP Configuration Mode:</label>
        <div style='display: flex; align-items: center; gap: 15px;'>
            <label class='toggle-switch'>
                <input type='checkbox' id='staticIpEnabled' onchange='toggleNetworkMode()' />
                <span class='toggle-slider'></span>
            </label>
            <span id='networkModeLabel' style='font-weight: bold; color: #4CAF50;'>DHCP (Dynamic)</span>
        </div>
    </div>
    
    <!-- Current Status -->
    <div class='settings-section'>
        <h4>Current Network Status</h4>
        <div class='settings-row'>
            <label>Mode:</label>
            <span id='currentMode' style='font-weight: bold;'>Loading...</span>
        </div>
        <div class='settings-row'>
            <label>IP Address:</label>
            <span id='currentIP'>Loading...</span>
        </div>
        <div class='settings-row'>
            <label>Gateway:</label>
            <span id='currentGateway'>Loading...</span>
        </div>
        <div class='settings-row'>
            <label>Subnet:</label>
            <span id='currentSubnet'>Loading...</span>
        </div>
    </div>
    
    <!-- Static IP Configuration (hidden by default) -->
    <div id='staticIpConfig' style='display: none; margin-top: 20px; padding: 15px; background: rgba(33,150,243,0.1); border-left: 3px solid #2196F3;'>
        <h4 style='color: #2196F3; margin-top: 0;'>Static IP Settings</h4>
        <p style='color: #888; font-size: 13px; margin-bottom: 10px;'>
            ⚠️ Configuration changes require transmitter reboot to apply
        </p>
        <p style='color: #ff9800; font-size: 12px; margin-bottom: 15px; padding: 8px; background: rgba(255,152,0,0.1); border-radius: 4px;'>
            ℹ️ <strong>Note:</strong> IP conflict detection can only identify devices currently active on the network. 
            Offline or powered-down devices with the same IP will not be detected.
        </p>
        
        <div class='settings-row'>
            <label>IP Address:</label>
            <div class='ip-input'>
                <input class='octet' type='number' id='ip1' min='0' max='255' value='192' />
                <span class='dot'>.</span>
                <input class='octet' type='number' id='ip2' min='0' max='255' value='168' />
                <span class='dot'>.</span>
                <input class='octet' type='number' id='ip3' min='0' max='255' value='1' />
                <span class='dot'>.</span>
                <input class='octet' type='number' id='ip4' min='0' max='255' value='100' />
            </div>
        </div>
        
        <div class='settings-row'>
            <label>Gateway:</label>
            <div class='ip-input'>
                <input class='octet' type='number' id='gw1' min='0' max='255' value='192' />
                <span class='dot'>.</span>
                <input class='octet' type='number' id='gw2' min='0' max='255' value='168' />
                <span class='dot'>.</span>
                <input class='octet' type='number' id='gw3' min='0' max='255' value='1' />
                <span class='dot'>.</span>
                <input class='octet' type='number' id='gw4' min='0' max='255' value='1' />
            </div>
        </div>
        
        <div class='settings-row'>
            <label>Subnet Mask:</label>
            <div class='ip-input'>
                <input class='octet' type='number' id='sn1' min='0' max='255' value='255' />
                <span class='dot'>.</span>
                <input class='octet' type='number' id='sn2' min='0' max='255' value='255' />
                <span class='dot'>.</span>
                <input class='octet' type='number' id='sn3' min='0' max='255' value='255' />
                <span class='dot'>.</span>
                <input class='octet' type='number' id='sn4' min='0' max='255' value='0' />
            </div>
        </div>
        
        <div class='settings-row'>
            <label>DNS Server:</label>
            <div class='ip-input'>
                <input class='octet' type='number' id='dns1' min='0' max='255' value='192' />
                <span class='dot'>.</span>
                <input class='octet' type='number' id='dns2' min='0' max='255' value='168' />
                <span class='dot'>.</span>
                <input class='octet' type='number' id='dns3' min='0' max='255' value='1' />
                <span class='dot'>.</span>
                <input class='octet' type='number' id='dns4' min='0' max='255' value='1' />
            </div>
        </div>
        
        <div class='settings-row'>
            <button onclick='saveNetworkConfig()' style='background: #2196F3; color: white; padding: 12px 30px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; font-weight: bold;'>
                💾 Save Network Configuration
            </button>
        </div>
        
        <div id='networkConfigStatus' style='margin-top: 15px; padding: 10px; border-radius: 5px; display: none;'></div>
    </div>
</div>
```

**Add CSS for toggle switch (in the `<style>` section):**
```css
.toggle-switch {
    position: relative;
    display: inline-block;
    width: 60px;
    height: 30px;
}

.toggle-switch input {
    opacity: 0;
    width: 0;
    height: 0;
}

.toggle-slider {
    position: absolute;
    cursor: pointer;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background-color: #4CAF50;
    transition: 0.4s;
    border-radius: 30px;
}

.toggle-slider:before {
    position: absolute;
    content: "";
    height: 22px;
    width: 22px;
    left: 4px;
    bottom: 4px;
    background-color: white;
    transition: 0.4s;
    border-radius: 50%;
}

.toggle-switch input:checked + .toggle-slider {
    background-color: #2196F3;
}

.toggle-switch input:checked + .toggle-slider:before {
    transform: translateX(30px);
}

.ip-input {
    display: flex;
    align-items: center;
    gap: 5px;
}

.ip-input .octet {
    width: 60px;
    padding: 8px;
    text-align: center;
    background: rgba(255,255,255,0.1);
    border: 1px solid rgba(255,255,255,0.3);
    border-radius: 5px;
    color: white;
    font-family: monospace;
    font-size: 14px;
}

.ip-input .dot {
    color: #888;
    font-weight: bold;
}
```

**Add JavaScript functions:**
```javascript
<script>
// Load current network configuration on page load
async function loadNetworkConfig() {
    try {
        const response = await fetch('/api/get_network_config');
        const data = await response.json();
        
        if (data.success) {
            const isStatic = data.use_static_ip;
            
            // Update toggle switch
            document.getElementById('staticIpEnabled').checked = isStatic;
            toggleNetworkMode(); // Update UI visibility
            
            // Update current status
            document.getElementById('currentMode').textContent = isStatic ? 'Static IP' : 'DHCP (Dynamic)';
            document.getElementById('currentMode').style.color = isStatic ? '#2196F3' : '#4CAF50';
            document.getElementById('currentIP').textContent = data.ip;
            document.getElementById('currentGateway').textContent = data.gateway;
            document.getElementById('currentSubnet').textContent = data.subnet;
            
            // Populate static IP fields
            if (isStatic) {
                const ip = data.ip.split('.');
                const gateway = data.gateway.split('.');
                const subnet = data.subnet.split('.');
                
                for (let i = 0; i < 4; i++) {
                    document.getElementById(`ip${i+1}`).value = ip[i];
                    document.getElementById(`gw${i+1}`).value = gateway[i];
                    document.getElementById(`sn${i+1}`).value = subnet[i];
                }
                // DNS defaults to gateway if not provided
                for (let i = 0; i < 4; i++) {
                    document.getElementById(`dns${i+1}`).value = gateway[i];
                }
            }
        }
    } catch (e) {
        console.error('Failed to load network config:', e);
        document.getElementById('currentMode').textContent = 'Error loading';
    }
}

function toggleNetworkMode() {
    const checkbox = document.getElementById('staticIpEnabled');
    const staticConfig = document.getElementById('staticIpConfig');
    const label = document.getElementById('networkModeLabel');
    
    if (checkbox.checked) {
        staticConfig.style.display = 'block';
        label.textContent = 'Static IP';
        label.style.color = '#2196F3';
    } else {
        staticConfig.style.display = 'none';
        label.textContent = 'DHCP (Dynamic)';
        label.style.color = '#4CAF50';
    }
}

async function saveNetworkConfig() {
    const statusDiv = document.getElementById('networkConfigStatus');
    const useStatic = document.getElementById('staticIpEnabled').checked;
    
    // Build IP addresses from octets
    const ip = `${document.getElementById('ip1').value}.${document.getElementById('ip2').value}.${document.getElementById('ip3').value}.${document.getElementById('ip4').value}`;
    const gateway = `${document.getElementById('gw1').value}.${document.getElementById('gw2').value}.${document.getElementById('gw3').value}.${document.getElementById('gw4').value}`;
    const subnet = `${document.getElementById('sn1').value}.${document.getElementById('sn2').value}.${document.getElementById('sn3').value}.${document.getElementById('sn4').value}`;
    const dns = `${document.getElementById('dns1').value}.${document.getElementById('dns2').value}.${document.getElementById('dns3').value}.${document.getElementById('dns4').value}`;
    
    // Validate IP addresses (basic check)
    const ipPattern = /^(\d{1,3}\.){3}\d{1,3}$/;
    if (useStatic && (!ipPattern.test(ip) || !ipPattern.test(gateway) || !ipPattern.test(subnet))) {
        statusDiv.style.display = 'block';
        statusDiv.style.background = 'rgba(255,107,53,0.2)';
        statusDiv.style.color = '#ff6b35';
        statusDiv.textContent = '❌ Invalid IP address format';
        return;
    }
    
    const config = {
        use_static_ip: useStatic,
        ip: ip,
        gateway: gateway,
        subnet: subnet,
        dns: dns
    };
    
    statusDiv.style.display = 'block';
    statusDiv.style.background = 'rgba(255,193,7,0.2)';
    statusDiv.style.color = '#FFC107';
    statusDiv.textContent = '⏳ Sending configuration to transmitter...';
    
    try {
        const response = await fetch('/api/save_network_config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });
        
        const result = await response.json();
        
        if (result.success) {
            statusDiv.style.background = 'rgba(76,175,80,0.2)';
            statusDiv.style.color = '#4CAF50';
            statusDiv.innerHTML = `✅ ${result.message}<br><strong>Please reboot the transmitter to apply changes.</strong>`;
            
            // Reload config after 2 seconds
            setTimeout(loadNetworkConfig, 2000);
        } else {
            statusDiv.style.background = 'rgba(255,107,53,0.2)';
            statusDiv.style.color = '#ff6b35';
            statusDiv.textContent = `❌ ${result.message}`;
        }
    } catch (e) {
        statusDiv.style.background = 'rgba(255,107,53,0.2)';
        statusDiv.style.color = '#ff6b35';
        statusDiv.textContent = '❌ Failed to send configuration: ' + e.message;
    }
}

// Load configuration when page loads
window.addEventListener('load', loadNetworkConfig);
</script>
```

---

## 9. Remove Hardcoded Static IP from Config

### File: `esp32common/ethernet_config.h`

**USER FEEDBACK: Remove hardcoded config entirely**

**COMPLETELY REMOVE the Network namespace:**
```cpp
// REMOVE THIS SECTION:
// namespace Network {
//     constexpr bool USE_STATIC_IP = false;
//     const IPAddress STATIC_IP(192, 168, 1, 100);
//     const IPAddress GATEWAY(192, 168, 1, 1);
//     const IPAddress SUBNET(255, 255, 255, 0);
//     const IPAddress DNS(192, 168, 1, 1);
// }
```

**Replace with comment only:**
```cpp
// ============================================================================
// NETWORK CONFIGURATION
// All network configuration is now stored in transmitter NVS and managed via
// the web interface. No hardcoded defaults exist - the transmitter will use
// DHCP on first boot until configured otherwise.
// ============================================================================
```

**Remove references in `network_config.h`:**
```cpp
// REMOVE THIS:
// namespace ethernet = EthernetConfig::Network;

// Network configuration is now fully managed in ethernet_manager.cpp from NVS
```

---

## 10. Testing Checklist

### Unit Testing
- [ ] NVS save/load functions work correctly
- [ ] Invalid IP addresses are rejected
- [ ] Checksum validation works
- [ ] ESP-NOW message sizes are correct

### Integration Testing
- [ ] DHCP → Static IP transition works
- [ ] Static IP → DHCP transition works
- [ ] Configuration persists across reboots
- [ ] ESP-NOW message delivery and ACK work
- [ ] TransmitterManager cache updates correctly
- [ ] Gateway reachability test blocks invalid configs
- [ ] IP conflict detection identifies live devices
- [ ] **Offline device test:** Power down a device with target IP, verify it's not detected (expected behavior)

### UI Testing
- [ ] Toggle switch works smoothly
- [ ] Static IP fields show/hide correctly
- [ ] IP validation works client-side
- [ ] Dashboard shows (S) or (D) indicator
- [ ] Success/error messages display correctly
- [ ] **Warning message visible:** User sees note about offline device limitation

### End-to-End Testing
1. **Test DHCP → Static:**
   - Start with DHCP
   - Change to static via web UI
   - Save and reboot transmitter
   - Verify transmitter comes up with static IP
   - Verify dashboard shows (S) indicator

2. **Test Static → DHCP:**
   - Start with static IP
   - Change to DHCP via web UI
   - Save and reboot transmitter
   - Verify transmitter gets DHCP address
   - Verify dashboard shows (D) indicator

3. **Test Invalid Inputs:**
   - Try invalid IP addresses
   - Verify client-side validation
   - Verify server-side validation

4. **Test ESP-NOW Failures:**
   - Disconnect receiver
   - Try to save config
   - Verify error message

5. **Test Gateway Reachability:**
   - Enter valid static IP with unreachable gateway
   - Save configuration
   - Verify pre-check fails with "Gateway unreachable" error
   - Configuration should NOT be saved

6. **Test IP Conflict Detection (Live Device):**
   - Find an active device IP on the network
   - Try to configure transmitter with same IP
   - Verify pre-check fails with "IP in use by active device" error
   - Configuration should NOT be saved

7. **Test IP Conflict Detection (Offline Device):**
   - Power down a device, note its IP address
   - Configure transmitter with the offline device's IP
   - Verify pre-check passes (offline device not detected - **expected behavior**)
   - Configuration saves successfully
   - Power on the offline device
   - Check transmitter logs for lwIP ARP conflict warnings (post-detection)
   - Verify this limitation is documented in UI warning

---

## 11. Project Guidelines Compliance

### URI Patterns (from WEB_UI_REDESIGN_PROPOSAL.md)
✅ API endpoints follow `/api/` pattern:
- `/api/get_network_config` - GET
- `/api/save_network_config` - POST

✅ Page URIs follow device hierarchy:
- `/transmitter/config` - Updated settings page
- `/` - Dashboard with indicators

### Code Organization
✅ Follows existing structure:
- ESP-NOW messages in `espnow_common.h`
- Handlers in `message_handler.cpp` (transmitter) and `espnow_callbacks.cpp` (receiver)
- API handlers in `api/api_handlers.cpp`
- Page handlers in `pages/settings_page.cpp`
- Manager classes in `utils/transmitter_manager.*`

### Logging
✅ Uses existing LOG_* macros:
- `LOG_INFO` for successful operations
- `LOG_ERROR` for failures
- `LOG_DEBUG` for debug info

### Error Handling
✅ Graceful degradation:
- Returns JSON errors in API endpoints
- Shows user-friendly messages in UI
- Falls back to DHCP if NVS read fails

---

## 12. Potential Issues & Improvements

### Issues to Watch
1. **Reboot Requirement:** Users must manually reboot transmitter for changes to apply
   - **Improvement:** Add auto-reboot option (checkbox in UI) that sends `msg_reboot` after saving

### Future Enhancements
1. **Auto-fill DHCP Values:** Button to copy current DHCP values to static fields
2. **Configuration History:** Track previous network configs in NVS (rollback feature)
3. **Network Quality Metrics:** Track ping latency, packet loss to gateway

**NOTE:** Validation, DNS, conflict detection, and reachability testing are now addressed in Sections 3, 17, and 18.

---

## 13. Implementation Order

**Recommended sequence:**

1. ✅ **Phase 1: ESP-NOW Protocol** (30 min)
   - Add message types to `espnow_common.h`
   - Define message structures
   - Test message sizes

2. ✅ **Phase 2: Transmitter Storage** (45 min)
   - Implement NVS save/load in `ethernet_manager.*`
   - Test NVS functions independently
   - Update `init()` to load from NVS

3. ✅ **Phase 3: Transmitter Task & Handler** (45 min)
   - Create FreeRTOS task for network config processing
   - Add message queue for deferred operations
   - Add handler in `message_handler.cpp`
   - Implement ACK response
   - Test with dummy messages

4. ✅ **Phase 4: Receiver Handler** (20 min)
   - Add ACK handler in `espnow_callbacks.cpp`
   - Update TransmitterManager cache

5. ✅ **Phase 5: Receiver API** (60 min)
   - Implement `/api/get_network_config`
   - Implement `/api/save_network_config`
   - Test with Postman/curl

6. ✅ **Phase 6: Dashboard UI** (30 min)
   - Add (S)/(D) indicator
   - Update `/api/dashboard_data`
   - Test auto-refresh

7. ✅ **Phase 7: Settings Page UI** (90 min)
   - Add toggle switch
   - Add IP input fields
   - Add JavaScript functions
   - Test UI interactions

8. ✅ **Phase 8: Reachability & Conflict Testing** (60 min)
   - Add ESP32Ping library dependency
   - Implement `testStaticIPReachability()`
   - Implement `checkIPConflict()`
   - Test with various network scenarios

9. ✅ **Phase 9: Integration Testing** (60 min)
   - Test full flow end-to-end
   - Test error cases
   - Test persistence across reboots
   - Test conflict detection and reachability
   - Verify task priorities work correctly (control loop not affected)

**Total Estimated Time:** ~7.5-8 hours

---

## 14. File Change Summary

### Files to Modify (20 files)

#### Common Library (3 files)
1. `esp32common/espnow_transmitter/espnow_common.h` - Add message types and structures
2. `esp32common/ethernet_config.h` - Add NVS precedence comment
3. *(Optional)* Create `esp32common/network_config/` library for shared network logic

#### Transmitter (6 files)
4. `ESPnowtransmitter2/src/config/task_config.h` - Add NETWORK_CONFIG_TASK_PRIORITY definition
5. `ESPnowtransmitter2/src/network/ethernet_manager.h` - Add network config methods + testing functions
6. `ESPnowtransmitter2/src/network/ethernet_manager.cpp` - Implement NVS save/load + conflict/reachability tests
7. `ESPnowtransmitter2/src/espnow/message_handler.h` - Add handler declaration
8. `ESPnowtransmitter2/src/espnow/message_handler.cpp` - Implement handler with validation + FreeRTOS task
9. `ESPnowtransmitter2/platformio.ini` - Add ESP32Ping library dependency

#### Receiver (12 files)
10. `espnowreciever_2/lib/webserver/utils/transmitter_manager.h` - Add static IP flag
11. `espnowreciever_2/lib/webserver/utils/transmitter_manager.cpp` - Implement getters/setters
12. `espnowreciever_2/src/espnow/espnow_callbacks.cpp` - Add ACK handler
13. `espnowreciever_2/lib/webserver/api/api_handlers.h` - Add handler declarations
14. `espnowreciever_2/lib/webserver/api/api_handlers.cpp` - Implement 2 new endpoints
15. `espnowreciever_2/lib/webserver/webserver.cpp` - Update handler count
16. `espnowreciever_2/lib/webserver/pages/dashboard_page.h` - *(no change needed)*
17. `espnowreciever_2/lib/webserver/pages/dashboard_page.cpp` - Add (S)/(D) indicator
18. `espnowreciever_2/lib/webserver/pages/settings_page.h` - *(no change needed)*
19. `espnowreciever_2/lib/webserver/pages/settings_page.cpp` - Complete UI overhaul
20. `espnowreciever_2/platformio.ini` - *(no change needed)*
21. `espnowreciever_2/docs/STATIC_IP_IMPLEMENTATION_PLAN.md` - This document
22. *(Optional)* `ESPnowtransmitter2/docs/NETWORK_CONFIG_TESTING.md` - Document testing approach

### Total Files to Modify: 22 files

### Files to Create (0 files)
- None (all functionality fits into existing files)

---

## 15. Risk Assessment

### High Risk
- **NVS Corruption:** If NVS save fails mid-write, could corrupt config
  - **Mitigation:** Use Preferences library which handles atomic writes
  - **Fallback:** Fall back to DHCP if NVS read fails

### Medium Risk
- **Invalid IP Configuration:** User enters unreachable static IP, loses connection
  - **Mitigation:** Client-side validation, clear warnings
  - **Recovery:** Physical access to transmitter, serial console to clear NVS

### Low Risk
- **ESP-NOW Message Loss:** Config update lost in transit
  - **Mitigation:** ACK mechanism, user sees "no response" error
  - **Recovery:** User retries save

---

## 16. Documentation Updates Needed

After implementation:
1. Update `README.md` - Add network configuration section
2. Update `WEB_UI_REDESIGN_PROPOSAL.md` - Document network settings page
3. Create `NETWORK_CONFIGURATION_GUIDE.md` - User guide for static IP setup
4. Update `TROUBLESHOOTING.md` - Add "Lost network connection" recovery steps

---

## 17. Static IP Reachability Testing

### USER FEEDBACK: "How would the testing if a static ip is reachable work?"

**Answer:** There are several approaches to test if a static IP configuration is valid before permanently applying it. Here are the recommended options:

### Option A: Pre-Save Gateway Ping Test ✅ CHOSEN APPROACH

**USER DECISION:** Use this approach - pre-check gateway reachability before allocating and saving.

**Concept:** Before saving to NVS, temporarily apply the static IP and ping the gateway. If successful, save; if failed, revert to DHCP.

**Implementation in `ethernet_manager.cpp`:**

```cpp
bool EthernetManager::testStaticIPReachability(const uint8_t ip[4], const uint8_t gateway[4], 
                                                const uint8_t subnet[4], const uint8_t dns[4]) {
    LOG_INFO("[NET_TEST] Testing static IP reachability...");
    
    // 1. Save current DHCP config for rollback
    IPAddress current_ip = ETH.localIP();
    IPAddress current_gateway = ETH.gatewayIP();
    IPAddress current_subnet = ETH.subnetMask();
    IPAddress current_dns = ETH.dnsIP();
    bool was_dhcp = !use_static_ip_;
    
    // 2. Temporarily apply static IP
    IPAddress test_ip(ip[0], ip[1], ip[2], ip[3]);
    IPAddress test_gateway(gateway[0], gateway[1], gateway[2], gateway[3]);
    IPAddress test_subnet(subnet[0], subnet[1], subnet[2], subnet[3]);
    IPAddress test_dns(dns[0], dns[1], dns[2], dns[3]);
    
    if (!ETH.config(test_ip, test_gateway, test_subnet, test_dns)) {
        LOG_ERROR("[NET_TEST] ✗ Failed to apply test config");
        return false;
    }
    
    // 3. Wait for network stack to settle
    delay(2000);
    
    // 4. Ping gateway using ICMP
    bool ping_success = Ping.ping(test_gateway, 3);  // 3 attempts
    
    if (ping_success) {
        LOG_INFO("[NET_TEST] ✓ Gateway is reachable (%d.%d.%d.%d)",
                 gateway[0], gateway[1], gateway[2], gateway[3]);
    } else {
        LOG_WARN("[NET_TEST] ✗ Gateway not reachable, reverting to previous config");
        
        // Rollback to previous config
        if (was_dhcp) {
            ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // Re-enable DHCP
        } else {
            ETH.config(current_ip, current_gateway, current_subnet, current_dns);
        }
        delay(2000);
    }
    
    return ping_success;
}
```

**Note:** ESP32 Arduino core includes `<ESP32Ping.h>` library for ICMP ping. Install via:
```ini
# In platformio.ini
lib_deps =
    ...
    marian-craciunescu/ESP32Ping @ ^1.7
```

### Option B: Post-Save Validation with Manual Rollback

**Concept:** Save configuration and apply it, but provide a "Confirm" button in UI. If user doesn't confirm within 60 seconds, auto-revert.

**Flow:**
1. User saves static IP → Applied immediately
2. UI shows countdown: "New config applied. Confirm in 60s or will revert"
3. If gateway pingable: Show green "Network OK" banner
4. If gateway not pingable: Show red "Cannot reach gateway" warning
5. User clicks "Confirm" → Config becomes permanent
6. Timeout expires → Auto-revert to previous config

**Pros:** Safer (user validates connectivity)  
**Cons:** More complex UI, requires timer logic

### Option C: Pre-Save Validation Only (No Ping)

**Concept:** Validate IP format and subnet logic, but trust the user to know their network.

**Flow:**
1. Check IP is valid (not 0.0.0.0, not multicast)
2. Check gateway is in same subnet
3. Show warning: "Static IP will apply on next reboot. Ensure values are correct."
4. Save to NVS without testing

**Pros:** Simple, no external dependencies  
**Cons:** User could lock themselves out

### Recommendation ✅ DECISION CONFIRMED

**IMPLEMENT Option A (Pre-Save Gateway Ping)** with these enhancements:
- Add library dependency: `ESP32Ping`
- Implement `testStaticIPReachability()` in `ethernet_manager.cpp`
- Call before `saveNetworkConfig()` in message handler
- If ping fails, send ACK with error: "Gateway unreachable - config not saved"
- User sees error in UI, can correct and retry
- Configuration only saved if gateway is reachable

**Add to message handler in transmitter:**
```cpp
// Before saving, test reachability
if (use_static) {
    if (!EthernetManager::instance().testStaticIPReachability(ip, gateway, subnet, dns)) {
        LOG_ERROR("[NET_CFG] Static IP test failed - not saving");
        send_network_config_ack(false, "Gateway unreachable");
        return;
    }
}

// Test passed or DHCP mode, proceed with save
if (EthernetManager::instance().saveNetworkConfig(use_static, ip, gateway, subnet, dns)) {
    // ... success ...
}
```

---

## 18. IP Conflict Detection

### USER FEEDBACK: "Same with if the static IP is already in use?"

**Answer:** Detecting IP conflicts requires checking if another device is already using the proposed static IP. Here are the approaches:

**⚠️ IMPORTANT LIMITATION:** IP conflict detection can **only detect devices that are currently live/active on the network**. If a device already has this IP address but is currently offline, powered down, or in sleep mode, the conflict will NOT be detected during pre-check. The transmitter will discover the conflict only when the other device comes online (lwIP will detect and log the ARP conflict).

**USER DECISION:** Pre-check the IP address as far as possible before allocating and saving.

### Option A: Gratuitous ARP Request (RECOMMENDED)

**Concept:** Before applying static IP, send a gratuitous ARP request asking "Who has this IP?" If another device responds, the IP is in use.

**Implementation:**

```cpp
#include <lwip/etharp.h>
#include <lwip/prot/ethernet.h>

bool EthernetManager::checkIPConflict(const uint8_t ip[4]) {
    LOG_INFO("[NET_CONFLICT] Checking if %d.%d.%d.%d is in use...",
             ip[0], ip[1], ip[2], ip[3]);
    
    IPAddress test_ip(ip[0], ip[1], ip[2], ip[3]);
    
    // Send gratuitous ARP: "Who has <test_ip>? Tell <test_ip>"
    // If we get a response, someone else has this IP
    
    ip4_addr_t ipaddr;
    IP4_ADDR(&ipaddr, ip[0], ip[1], ip[2], ip[3]);
    
    // Use lwIP's etharp_request to probe
    err_t err = etharp_request(netif_default, &ipaddr);
    
    if (err != ERR_OK) {
        LOG_ERROR("[NET_CONFLICT] ARP request failed: %d", err);
        return false;  // Assume no conflict if check fails
    }
    
    // Wait briefly for responses
    delay(500);
    
    // Check ARP table for entry
    const ip4_addr_t* found_ip;
    struct eth_addr* eth_ret;
    const struct eth_addr* eth_addr = NULL;
    
    ip_addr_t ip_lookup = IPADDR4_INIT(ipaddr.addr);
    int found = etharp_find_addr(netif_default, &ip_lookup, &eth_ret, &found_ip);
    
    if (found >= 0) {
        LOG_WARN("[NET_CONFLICT] ✗ IP is in use by MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                 eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
        return true;  // Conflict detected
    }
    
    LOG_INFO("[NET_CONFLICT] ✓ IP appears to be available");
    return false;  // No conflict
}
```

**Add to message handler:**
```cpp
// After validation, before saving
if (use_static) {
    if (EthernetManager::instance().checkIPConflict(ip)) {
        LOG_ERROR("[NET_CFG] IP address is already in use");
        send_network_config_ack(false, "IP conflict detected");
        return;
    }
}
```

### Option B: ICMP Ping to Proposed IP ✅ CHOSEN APPROACH

**USER DECISION:** Use this approach - ping the proposed IP before allocating.

**Concept:** Ping the proposed static IP before applying it. If it responds, it's in use.

**⚠️ Limitation:** Only detects live/powered-on devices. Offline devices with the same IP will not be detected.

**Implementation:**

```cpp
#include <ESP32Ping.h>

bool EthernetManager::checkIPConflict(const uint8_t ip[4]) {
    IPAddress test_ip(ip[0], ip[1], ip[2], ip[3]);
    
    LOG_INFO("[NET_CONFLICT] Pinging %s to check availability...", test_ip.toString().c_str());
    LOG_INFO("[NET_CONFLICT] Note: Can only detect live devices currently on network");
    
    // Ping the IP - if it responds, it's in use
    bool responds = Ping.ping(test_ip, 2);  // 2 attempts
    
    if (responds) {
        LOG_WARN("[NET_CONFLICT] ✗ IP is in use by live device (ping successful)");
        return true;  // Conflict detected
    }
    
    LOG_INFO("[NET_CONFLICT] ✓ No live device responded (IP appears available)");
    LOG_INFO("[NET_CONFLICT] Warning: Offline devices with this IP will not be detected");
    return false;  // No conflict detected (but could exist offline)
}
```

**Pros:** Simple, uses existing Ping library  
**Cons:** False negative if device has firewall blocking ICMP

### Option C: Post-Apply ARP Conflict Detection

**Concept:** Apply the static IP, then monitor for ARP conflicts (lwIP will detect this).

**Implementation:**

ESP32's lwIP stack has built-in ARP conflict detection. When enabled, it will log errors if duplicate IPs are detected:

```cpp
// In ethernet_manager.cpp init()
#if LWIP_DHCP && LWIP_NETIF_HOSTNAME
    ETH.setHostname("esp32-transmitter");
    etharp_gratuitous(netif_default);  // Announce our presence
#endif
```

Then monitor logs for:
```
etharp: ARP conflict detected for 192.168.1.100
```

**Pros:** Automatic, no code needed  
**Cons:** Conflict detected after applying (too late)

### Recommendation ✅ DECISION CONFIRMED

**IMPLEMENT Option B (ICMP Ping)** with these considerations:
- Already have `ESP32Ping` library for reachability testing
- Easy to implement and understand
- Provides clear yes/no answer for **live devices only**
- **Known limitation:** Cannot detect offline devices with same IP
- User should be warned in UI about this limitation

**Combine with Option C (lwIP conflict detection) for runtime monitoring:**
- After applying static IP, lwIP will warn if conflicts occur
- User can see in serial logs if another device comes online with same IP
- Provides safety net for offline devices that wake up later

**Full integration with pre-checks:**

```cpp
// In message handler, before saving:
if (use_static) {
    LOG_INFO("[NET_CFG] Pre-checking static IP configuration...");
    
    // 1. Check if IP is already in use by live devices
    if (EthernetManager::instance().checkIPConflict(ip)) {
        LOG_ERROR("[NET_CFG] IP conflict detected with live device");
        send_network_config_ack(false, "IP in use by active device");
        return;
    }
    
    // 2. Test if gateway is reachable
    if (!EthernetManager::instance().testStaticIPReachability(ip, gateway, subnet, dns)) {
        LOG_ERROR("[NET_CFG] Gateway unreachable with proposed config");
        send_network_config_ack(false, "Gateway unreachable");
        return;
    }
    
    LOG_INFO("[NET_CFG] Pre-checks passed (note: offline devices not detected)");
}

// All checks passed, save config
EthernetManager::instance().saveNetworkConfig(use_static, ip, gateway, subnet, dns);
send_network_config_ack(true, "OK - reboot required");
```

**Note:** The ACK message intentionally says "IP in use by active device" to remind users that offline devices cannot be detected.

---

## 19. Success Criteria

Implementation is complete when:

**Core Functionality:**
✅ User can toggle between DHCP and Static IP via web UI  
✅ Static IP configuration persists across transmitter reboots  
✅ Dashboard shows (S) or (D) indicator next to IP address  
✅ Configuration changes are sent via ESP-NOW with ACK  

**Data Format & Display:**
✅ IP addresses stored as `uint8_t[4]` arrays (4 bytes) in NVS and messages  
✅ Web UI displays IP in dotted decimal notation (e.g., "192.168.1.100")  
✅ Web UI uses 4 separate numeric input fields (0-255 per octet)  
✅ API converts between string format (web) and array format (binary)  

**Validation & Safety:**
✅ Invalid IP addresses are rejected with clear error messages  
✅ Transmitter validates IP format, subnet logic, and gateway reachability  
✅ Settings page has toggle switch with conditional field visibility  

**Advanced Features:**
✅ Network config version tracking works (like battery settings)  
✅ Dual DNS servers (primary + secondary) are supported  
✅ Gateway reachability is tested before saving configuration  
✅ IP conflict detection prevents using already-occupied addresses  

**Task Priority & Performance:**
✅ Network config processing runs in separate FreeRTOS task (priority 3)  
✅ ESP-NOW callback returns quickly (< 1ms) without blocking  
✅ Heavy operations (ping, NVS write) don't interfere with control loop  
✅ Control tasks maintain highest priority and responsiveness  

**Quality & Compliance:**
✅ All existing functionality remains working  
✅ Code follows project guidelines and conventions  
✅ No compilation errors or warnings  
✅ End-to-end testing passes all scenarios  

---

**End of Implementation Plan**
