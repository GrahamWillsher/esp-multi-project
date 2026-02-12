# ESP-NOW Channel Management - Comprehensive Review

**Date:** February 10, 2026  
**Scope:** Multi-device ESP-NOW architecture (Transmitter + Receiver)  
**Issue:** Ongoing channel hopping/mismatch between transmitter and receiver

---

## Executive Summary

### Critical Findings

1. **ARCHITECTURAL CONFLICT**: Channel = 0 "magic value" creates race conditions
2. **BROADCAST PEER PERSISTENCE**: Old peers survive discovery restart with stale channel
3. **INITIALIZATION ORDER DEPENDENCY**: Boot order determines channel source of truth
4. **INCONSISTENT PEER REGISTRATION**: Multiple code paths use different channel values
5. **NO RUNTIME CHANNEL VALIDATION**: Peers registered once, never re-validated

### Root Cause

The ESP-NOW channel mismatch stems from **inconsistent peer channel management** during registration and discovery restart. The broadcast peer uses `channel = 0` (expecting ESP-IDF to use "current WiFi channel"), but:
- Old broadcast peers persist across discovery restarts with stale channel values
- ESP-IDF captures WiFi channel at `esp_now_add_peer()` call time, creating timing sensitivity
- No mechanism validates or updates peer channel after registration

### Answer to "Does receiver need to boot first?"

**NO**, but there's a **critical asymmetry**:
- **Receiver boots first**: Connects to router → gets channel from AP → transmitter scans and locks to receiver's channel ✅
- **Transmitter boots first**: Scans 1-13 → finds nothing → defaults to channel 1 → receiver boots on different channel (router's channel) → MISMATCH ❌

The current architecture **requires receiver to boot first** or both devices must reboot together.

---

## 1. Initialization Order Analysis

### Transmitter Boot Sequence

**File:** [ESPnowtransmitter2/src/main.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESPnowtransmitter2/espnowtransmitter2/src/main.cpp#L60-L130)

```cpp
void setup() {
    // 1. Initialize Ethernet (W5500 via SPI)
    EthernetManager::instance().init();  // ETH.begin() - sets up Ethernet hardware
    
    // 2. Initialize WiFi for ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    // ⚠️ NO channel forced here - WiFi defaults to channel 1
    
    // 3. Initialize ESP-NOW
    esp_now_init();
    
    // 4. Start message handler (to receive ACKs)
    EspnowMessageHandler::instance().start_rx_task();
    
    // 5. Discover receiver and lock channel
    discover_and_lock_channel();  // Scans channels 1-13
    
    // 6. Start periodic discovery announcements
    DiscoveryTask::instance().start();
}
```

**Channel Flow:**
1. WiFi initialized on **channel 1** (default)
2. `discover_and_lock_channel()` scans channels 1-13 sending PROBE
3. If receiver responds: locks to receiver's channel → stores in `g_lock_channel`
4. If no receiver: **stays on channel 1** → `g_lock_channel = 1`

### Receiver Boot Sequence

**File:** [espnowreciever_2/src/main.cpp](c:/Users/GrahamWillsher/ESP32Projects/espnowreciever_2/src/main.cpp#L40-L85)

```cpp
void setup() {
    // 1. Initialize WiFi with static IP
    WiFi.config(static_ip, gateway, subnet);
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASSWORD);  // ⚠️ Connects to router
    
    // 2. Wait for connection (up to 10 seconds)
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
    }
    
    // 3. Store WiFi channel from router
    if (WiFi.status() == WL_CONNECTED) {
        ESPNow::wifi_channel = WiFi.channel();  // ⚠️ CHANNEL SOURCE OF TRUTH
        Serial.println(ESPNow::wifi_channel);   // e.g., channel 6, 11, etc.
    }
    
    // 4. Initialize ESP-NOW (no channel set - uses current WiFi channel)
    esp_now_init();  // ESP-NOW operates on WiFi.channel()
    
    // 5. Register receive callback (waits for transmitter)
}
```

**File:** [espnowreciever_2/src/config/wifi_setup.cpp](c:/Users/GrahamWillsher/ESP32Projects/espnowreciever_2/src/config/wifi_setup.cpp#L1-L60)

**Channel Flow:**
1. WiFi connects to router
2. Router assigns channel (e.g., 6 or 11)
3. `ESPNow::wifi_channel = WiFi.channel()` → **receiver's channel is router-determined**
4. ESP-NOW initialized on this channel
5. Receiver **never changes channel** after boot

### Initialization Order Impact

| Scenario | Transmitter Channel | Receiver Channel | Result |
|----------|---------------------|------------------|--------|
| **Receiver boots first** | Scans → finds receiver on channel 6 → locks to 6 | Router channel 6 | ✅ **MATCH** |
| **Transmitter boots first** | Scans → no receiver → defaults to 1 | Router channel 6 (boots later) | ❌ **MISMATCH** |
| **Router changes channel** | Still locked to old channel 6 | New router channel 11 | ❌ **MISMATCH** |
| **Simultaneous boot** | Scans channels 1-13 while receiver connecting | Connecting to router (not responding) | ❌ **TIMING RACE** |

---

## 2. Channel Source of Truth

### Who Establishes the Channel?

**Answer: RECEIVER establishes the channel**

**Receiver:**
- Connects to WiFi router
- Router assigns channel (based on router's configuration)
- Receiver **passively accepts** router's channel
- ESP-NOW operates on this channel

**Transmitter:**
- Scans all channels (1-13)
- Sends PROBE messages on each channel
- When receiver ACKs, **locks to receiver's channel**
- Stores in `g_lock_channel` variable

### Channel Variables Across Projects

| Device | Variable | File | Purpose |
|--------|----------|------|---------|
| **Transmitter** | `g_lock_channel` | [espnow_transmitter.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_transmitter/espnow_transmitter.cpp#L16) | Stores discovered channel |
| **Receiver** | `ESPNow::wifi_channel` | [globals.cpp](c:/Users/GrahamWillsher/ESP32Projects/espnowreciever_2/src/globals.cpp#L31) | Stores WiFi.channel() at boot |

### Channel Change Scenarios

**Receiver can change channel when:**
- WiFi router manually changes channel (admin action)
- Router auto-selects different channel (interference avoidance)
- Receiver reboots and router has changed channel
- ⚠️ **Transmitter has NO mechanism to detect receiver channel change**

---

## 3. Peer Registration Channel Values

### All `add_peer()` Calls Audit

#### 3.1 Transmitter - Broadcast Peer

**File:** [espnow_peer_manager.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_common_utils/espnow_peer_manager.cpp#L70-L90)

```cpp
bool add_broadcast_peer() {
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    if (esp_now_is_peer_exist(broadcast_mac)) {
        return true;  // ⚠️ PROBLEM: Returns without checking channel
    }
    
    esp_now_peer_info_t broadcast_peer = {};
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    broadcast_peer.channel = 0;  // ⚠️ PROBLEM: Relies on "magic value"
    broadcast_peer.encrypt = false;
    broadcast_peer.ifidx = WIFI_IF_STA;
    
    esp_now_add_peer(&broadcast_peer);
}
```

**Issues:**
1. `channel = 0` means "use current WiFi channel at time of esp_now_add_peer() call"
2. If broadcast peer already exists, function returns early **without verifying channel**
3. Old peer from before channel lock persists with stale channel value

#### 3.2 Transmitter - Receiver Peer

**File:** [espnow_transmitter.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_transmitter/espnow_transmitter.cpp#L28-L45)

```cpp
bool ensure_peer_added(uint8_t channel) {
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, receiver_mac, 6);
    peer.ifidx = WIFI_IF_STA;
    peer.channel = channel;  // ✅ Explicit channel (g_lock_channel)
    peer.encrypt = false;
    
    if (esp_now_is_peer_exist(receiver_mac)) {
        esp_now_del_peer(receiver_mac);  // ✅ Removes old peer
    }
    esp_err_t result = esp_now_add_peer(&peer);
    return result == ESP_OK;
}
```

**Called from:** [espnow_transmitter.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_transmitter/espnow_transmitter.cpp#L280-L295)

```cpp
void discover_and_lock_channel() {
    uint8_t locked = 0;
    int found = hop_and_lock_channel(&locked);
    if (found > 0) {
        g_lock_channel = locked;
        set_channel(locked);
        ensure_peer_added(locked);  // ✅ Uses explicit locked channel
    }
}
```

**Status:** ✅ This works correctly - uses explicit `g_lock_channel` value

#### 3.3 Receiver - Transmitter Peer (from PROBE handler)

**File:** [espnow_standard_handlers.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_common_utils/espnow_standard_handlers.cpp#L28-L32)

```cpp
void handle_probe(const espnow_queue_msg_t* msg, void* context) {
    // Add peer if not already registered
    if (!EspnowPeerManager::is_peer_registered(msg->mac)) {
        uint8_t current_channel = WiFi.channel();  // ✅ Explicit channel
        EspnowPeerManager::add_peer(msg->mac, current_channel);
    }
}
```

**Status:** ✅ Uses explicit `WiFi.channel()` (receiver's channel is stable)

#### 3.4 Receiver - Transmitter Peer (from worker task)

**File:** [espnow_tasks.cpp](c:/Users/GrahamWillsher/ESP32Projects/espnowreciever_2/src/espnow/espnow_tasks.cpp#L524)

```cpp
if (!EspnowPeerManager::is_peer_registered(queue_msg.mac)) {
    EspnowPeerManager::add_peer(queue_msg.mac, 0);  // ⚠️ Uses channel = 0
}
```

**Status:** ⚠️ Uses `channel = 0` but receiver channel is stable (connected to router)

#### 3.5 Transmitter - Receiver Peer (from message handler)

**File:** [message_handler.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp#L1069)

```cpp
if (!EspnowPeerManager::is_peer_registered(receiver_mac_)) {
    EspnowPeerManager::add_peer(receiver_mac_);  // Uses default channel = 0
}
```

**Status:** ⚠️ Uses default `channel = 0` parameter

### Summary of Channel Values

| Location | Peer Type | Channel Value | Status |
|----------|-----------|---------------|--------|
| Transmitter broadcast peer | Broadcast (FF:FF:FF:FF:FF:FF) | `0` (magic value) | ❌ **PROBLEMATIC** |
| Transmitter receiver peer (discovery) | Receiver MAC | `g_lock_channel` (explicit) | ✅ **CORRECT** |
| Transmitter receiver peer (message handler) | Receiver MAC | `0` (default) | ⚠️ **INCONSISTENT** |
| Receiver transmitter peer (PROBE handler) | Transmitter MAC | `WiFi.channel()` (explicit) | ✅ **CORRECT** |
| Receiver transmitter peer (worker task) | Transmitter MAC | `0` (default) | ⚠️ **ACCEPTABLE** (receiver channel stable) |

---

## 4. Channel Locking Mechanisms

### Transmitter: Multi-layered Locking

**Variables:**
- `g_lock_channel` (volatile uint8_t) - stores discovered/locked channel

**Functions:**

1. **`set_channel(uint8_t ch)`**  
   File: [espnow_transmitter.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_transmitter/espnow_transmitter.cpp#L25-L27)
   ```cpp
   bool set_channel(uint8_t ch) {
       return esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) == ESP_OK;
   }
   ```

2. **`force_and_verify_channel(uint8_t target_channel)`**  
   File: [discovery_task.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp#L128-L153)
   ```cpp
   bool force_and_verify_channel(uint8_t target_channel) {
       // Force set channel
       if (!set_channel(target_channel)) return false;
       
       // Delay for WiFi driver stabilization (150ms)
       delay(150);
       
       // Verify channel was actually set
       uint8_t actual_ch = 0;
       wifi_second_chan_t second;
       esp_wifi_get_channel(&actual_ch, &second);
       
       if (actual_ch != target_channel) {
           LOG_ERROR("Channel verification failed");
           return false;
       }
       
       return true;
   }
   ```

**Channel Locking Checkpoints:**

| Checkpoint | File | When |
|------------|------|------|
| Initial discovery | espnow_transmitter.cpp | `discover_and_lock_channel()` → `g_lock_channel` set |
| Discovery restart | discovery_task.cpp | `force_and_verify_channel(g_lock_channel)` |
| Timeout watchdog | message_handler.cpp | Checks channel before restart, resets if drifted |
| Peer state audit | discovery_task.cpp | Logs channel mismatches (diagnostic only) |

### Receiver: Passive Channel Management

**Variables:**
- `ESPNow::wifi_channel` (volatile int) - set once at boot from `WiFi.channel()`

**No Active Locking:**
- Receiver **never calls** `esp_wifi_set_channel()`
- Channel determined by WiFi router connection
- ESP-NOW passively uses whatever channel WiFi is on

**Rationale:**
- Receiver maintains WiFi connection for web server
- Cannot change channel without disconnecting from router
- Channel must match router's channel for WiFi connectivity

---

## 5. Scenario Analysis

### 5.1 Cold Boot - Receiver First ✅

**Timeline:**
```
T=0s    Receiver boots → connects to router (channel 6) → ESP-NOW on channel 6
T=5s    Transmitter boots → WiFi defaults to channel 1
T=6s    Transmitter starts channel scan (1-13)
T=7s    Transmitter sends PROBE on channel 6
T=7.1s  Receiver receives PROBE → sends ACK (channel=6)
T=7.2s  Transmitter receives ACK → g_lock_channel = 6 → sets WiFi to channel 6
T=8s    Transmitter adds receiver peer (channel=6)
T=9s    Discovery task starts → adds broadcast peer (channel=0 → becomes 6)
T=10s   ✅ Both devices on channel 6 - communication established
```

**Result:** ✅ **SUCCESS** - Transmitter discovers and locks to receiver's channel

### 5.2 Cold Boot - Transmitter First ❌

**Timeline:**
```
T=0s    Transmitter boots → WiFi defaults to channel 1
T=1s    Transmitter starts channel scan (1-13)
T=2s    Scans channels 1-13 → no receiver found
T=3s    discover_and_lock_channel() → g_lock_channel = 1 (default)
T=4s    Discovery task starts → broadcasts on channel 1
T=10s   Receiver boots → connects to router (channel 6)
T=15s   Transmitter broadcasts PROBE on channel 1
T=16s   ❌ Receiver on channel 6, doesn't hear PROBE on channel 1
T=20s   Receiver discovery starts → broadcasts on channel 6
T=21s   ❌ Transmitter on channel 1, doesn't hear announcement on channel 6
T=30s+  ❌ Deadlock - both devices broadcasting on different channels
```

**Result:** ❌ **FAILURE** - Devices on different channels, no discovery possible

**Manual Intervention Required:**
- Reboot transmitter (will scan and find receiver on channel 6)
- OR reboot both devices simultaneously
- OR reboot receiver first

### 5.3 Reconnection After Power Loss

**Scenario:** Both devices running normally (channel 6), receiver loses power

**Timeline:**
```
T=0s    Normal operation (both on channel 6)
T=1s    Receiver power lost
T=11s   Transmitter watchdog detects timeout (10s)
T=11.1s Transmitter checks channel (still 6) → restarts discovery
T=11.2s Discovery restart → cleanup peers → add_broadcast_peer()
T=11.3s ⚠️ PROBLEM: Broadcast peer already exists (stale channel?)
T=12s   Transmitter sends PROBE via broadcast peer
T=12.1s ❌ ESP-NOW error: "Peer channel != home channel"
T=20s   Receiver power restored → boots on channel 6
T=30s   Receiver sends announcement on channel 6
T=30.1s Transmitter receives announcement → re-establishes connection
```

**Result:** ⚠️ **EVENTUAL SUCCESS** but with 10-20 second delay and errors

**File Reference:** [discovery_task.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp#L31-L70)

### 5.4 WiFi Router Changes Channel

**Scenario:** Router admin changes channel from 6 → 11 (or auto-channel selection)

**Timeline:**
```
T=0s    Normal operation (transmitter: g_lock_channel=6, receiver: channel 6)
T=1s    Router changes to channel 11
T=2s    Receiver WiFi disconnects
T=3s    Receiver reconnects to router → now on channel 11
T=3.1s  ESPNow::wifi_channel updated to 11
T=4s    Transmitter sends data on channel 6
T=4.1s  ❌ Receiver on channel 11, doesn't receive on channel 6
T=14s   Transmitter watchdog timeout (10s no messages)
T=14.1s Transmitter restarts discovery → still locked to channel 6
T=15s   Transmitter sends PROBE on channel 6
T=20s   ❌ Still deadlocked - transmitter on 6, receiver on 11
```

**Result:** ❌ **PERMANENT FAILURE** - Requires transmitter reboot to re-scan

**Root Cause:** Transmitter has **no mechanism** to detect receiver channel change

### 5.5 Discovery Task Restart

**Scenario:** Discovery task restart during normal operation

**File:** [discovery_task.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp#L31-L70)

**Current Flow:**
```cpp
void DiscoveryTask::restart() {
    // STEP 1: Remove ALL ESP-NOW peers
    cleanup_all_peers();  // ⚠️ Should remove broadcast peer
    
    // STEP 2: Force channel lock and verify
    if (!force_and_verify_channel(g_lock_channel)) {
        LOG_ERROR("Channel verification failed");
        return;
    }
    
    // STEP 3: Restart discovery task
    EspnowDiscovery::instance().restart();
}
```

**Problem:**
```cpp
void cleanup_all_peers() {
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_del_peer(broadcast_mac);  // ✅ Removes broadcast peer
    }
    // Remove receiver peer too
    if (esp_now_is_peer_exist(receiver_mac)) {
        esp_now_del_peer(receiver_mac);
    }
}
```

**Expected:** Broadcast peer removed → re-added with correct channel  
**Observed:** Sometimes broadcast peer persists with stale channel

**File:** [ESPNOW_CHANNEL_MISMATCH_ANALYSIS.md](c:/users/grahamwillsher/esp32projects/esp32common/docs/Migration/ESPNOW_CHANNEL_MISMATCH_ANALYSIS.md#L70-L90)

---

## 6. Critical Code Path Analysis

### 6.1 Handle Probe (Receiver Side)

**File:** [espnow_standard_handlers.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_common_utils/espnow_standard_handlers.cpp#L15-L60)

```cpp
void handle_probe(const espnow_queue_msg_t* msg, void* context) {
    const probe_t* p = reinterpret_cast<const probe_t*>(msg->data);
    
    // Add peer if not registered
    if (!EspnowPeerManager::is_peer_registered(msg->mac)) {
        uint8_t current_channel = WiFi.channel();  // ✅ Uses receiver's channel
        EspnowPeerManager::add_peer(msg->mac, current_channel);
    }
    
    // Send ACK response
    send_ack_response(msg->mac, p->seq, WiFi.channel());  // ✅ Tells transmitter receiver's channel
}
```

**Channel Flow:**
1. Receiver gets PROBE from transmitter
2. Registers transmitter as peer using `WiFi.channel()` (receiver's current channel)
3. Sends ACK with `WiFi.channel()` in payload
4. Transmitter receives ACK → extracts channel → locks to that channel

**Status:** ✅ **CORRECT** - Receiver uses its current WiFi channel consistently

### 6.2 Discovery Task Restart

**File:** [discovery_task.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp#L31-L70)

```cpp
void DiscoveryTask::restart() {
    // STEP 1: Remove ALL ESP-NOW peers
    cleanup_all_peers();
    
    // STEP 2: Force channel lock
    if (!force_and_verify_channel(g_lock_channel)) {
        LOG_ERROR("Failed to lock channel");
        // Retry with exponential backoff...
    }
    
    // STEP 3: Restart discovery task
    EspnowDiscovery::instance().restart();
    task_handle_ = EspnowDiscovery::instance().get_task_handle();
    
    // STEP 4: Verify
    uint8_t verify_ch = 0;
    esp_wifi_get_channel(&verify_ch, &second);
    if (verify_ch != g_lock_channel) {
        LOG_ERROR("Post-restart channel mismatch");
    }
}
```

**Calls:** [espnow_discovery.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_common_utils/espnow_discovery.cpp)

```cpp
void EspnowDiscovery::restart() {
    stop();   // vTaskDelete(task)
    start();  // xTaskCreate(new task)
}

void EspnowDiscovery::task_impl(void* parameter) {
    add_broadcast_peer();  // ⚠️ PROBLEM HERE
    
    while (true) {
        probe_t announce;
        announce.type = msg_probe;
        announce.seq = seq++;
        
        esp_now_send(broadcast_mac, (uint8_t*)&announce, sizeof(announce));
        vTaskDelay(interval);
    }
}
```

**Problem Flow:**
1. `restart()` calls `cleanup_all_peers()` → removes broadcast peer ✅
2. `force_and_verify_channel(11)` → sets WiFi to channel 11 ✅
3. New discovery task starts → calls `add_broadcast_peer()`
4. `add_broadcast_peer()` checks if peer exists:
   - If exists: returns early (doesn't update channel) ❌
   - If not exists: adds with `channel = 0` (uses current WiFi channel)
5. Timing race: WiFi driver may not have fully updated to channel 11 yet
6. Broadcast peer registered with wrong channel (e.g., channel 1 or 6)
7. PROBE messages sent → ESP-NOW error: "Peer channel != home channel"

**Status:** ❌ **PROBLEMATIC** - Broadcast peer persistence and timing issues

### 6.3 WiFi Setup (Receiver)

**File:** [wifi_setup.cpp](c:/Users/GrahamWillsher/ESP32Projects/espnowreciever_2/src/config/wifi_setup.cpp#L1-L60)

```cpp
void setupWiFi() {
    // Configure static IP
    WiFi.config(LOCAL_IP, GATEWAY, SUBNET, DNS1, DNS2);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASSWORD);  // ⚠️ Connects to router
    
    // Wait for connection
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        ESPNow::wifi_channel = WiFi.channel();  // ⚠️ Channel from router
        Serial.println(WiFi.localIP());
        Serial.println(ESPNow::wifi_channel);  // e.g., "6" or "11"
    }
}
```

**Channel Determination:**
- `WiFi.begin()` → connects to router
- Router determines channel (admin configured or auto-selected)
- `WiFi.channel()` returns router's channel
- **Receiver has NO control** over channel selection

**Implications:**
- If router is on channel 11, receiver is on channel 11
- If router changes to channel 6, receiver follows (after reconnect)
- Transmitter must scan to find receiver's channel
- **Transmitter cannot force receiver to change channel**

**Status:** ✅ **BY DESIGN** - Receiver follows router, transmitter follows receiver

### 6.4 Discover and Lock Channel (Transmitter)

**File:** [espnow_transmitter.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_transmitter/espnow_transmitter.cpp#L185-L220)

```cpp
void discover_and_lock_channel() {
    uint8_t locked = 0;
    int found = hop_and_lock_channel(&locked);  // Scans channels 1-13
    
    if (found > 0) {
        // Receiver found!
        g_lock_channel = locked;
        set_channel(locked);
        
        // Verify
        uint8_t current_ch = 0;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&current_ch, &second);
        LOG_INFO("Current WiFi channel: %d (locked: %d)", current_ch, locked);
        
        // Add receiver peer with explicit channel
        ensure_peer_added(locked);  // ✅ Uses explicit channel
        
    } else {
        // No receiver found
        LOG_WARNING("No receiver found during initial discovery");
        
        // Fallback: use current WiFi channel (channel 1)
        uint8_t current_ch = 0;
        esp_wifi_get_channel(&current_ch, &second);
        g_lock_channel = current_ch;  // ⚠️ Defaults to channel 1
        LOG_INFO("Using WiFi channel %d for ESP-NOW", current_ch);
        
        // Don't add peer yet - will be added when receiver responds
    }
}
```

**Channel Scan Logic:**

```cpp
int hop_and_lock_channel(uint8_t* out_channel, uint8_t attempts_per_channel, uint16_t ack_wait_ms) {
    for (uint8_t ch : {1,2,3,4,5,6,7,8,9,10,11,12,13}) {
        set_channel(ch);  // Switch to channel
        ensure_peer_added(ch);  // Add broadcast peer on this channel
        
        for (uint8_t a = 0; a < attempts_per_channel; ++a) {
            g_ack_received = false;
            g_ack_seq = random();
            send_probe(g_ack_seq);  // Send PROBE with sequence number
            
            // Wait for ACK (120ms default)
            uint32_t start = millis();
            while (!g_ack_received && (millis() - start) < ack_wait_ms) {
                delay(1);
            }
            
            if (g_ack_received) {
                // ACK received! Extract channel from ACK payload
                *out_channel = g_lock_channel;  // Set by ACK handler
                return g_lock_channel;
            }
        }
    }
    
    // No receiver found on any channel
    return 0;
}
```

**ACK Handler Sets Channel:**

**File:** [espnow_standard_handlers.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_common_utils/espnow_standard_handlers.cpp#L85-L100)

```cpp
void handle_ack(const espnow_queue_msg_t* msg, void* context) {
    const ack_t* a = reinterpret_cast<const ack_t*>(msg->data);
    AckHandlerConfig* config = static_cast<AckHandlerConfig*>(context);
    
    // Update channel lock
    if (config && config->lock_channel) {
        *config->lock_channel = a->channel;  // Extract channel from ACK payload
        
        // Optionally set WiFi channel immediately
        if (config->set_wifi_channel) {
            esp_wifi_set_channel(a->channel, WIFI_SECOND_CHAN_NONE);
        }
    }
    
    // Set ACK received flag
    if (config && config->ack_received_flag) {
        *config->ack_received_flag = true;  // Breaks wait loop in hop_and_lock_channel()
    }
}
```

**Channel Discovery Flow:**
1. Transmitter sends PROBE on channel 1 → no response
2. Transmitter switches to channel 2 → sends PROBE → no response
3. ...continues through channels...
4. Transmitter switches to channel 6 → sends PROBE
5. Receiver (on channel 6) receives PROBE → sends ACK with channel=6 in payload
6. Transmitter receives ACK → `g_lock_channel = 6` → sets WiFi to channel 6
7. `hop_and_lock_channel()` returns 6
8. `discover_and_lock_channel()` → `ensure_peer_added(6)` → registers receiver peer on channel 6

**Status:** ✅ **CORRECT** - Properly scans and locks to receiver's channel

---

## 7. Root Causes Summary

### Primary Root Causes

1. **Broadcast Peer Persistence Bug**
   - **File:** [espnow_peer_manager.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_common_utils/espnow_peer_manager.cpp#L70-L75)
   - **Issue:** `add_broadcast_peer()` returns early if peer already exists, never validates/updates channel
   - **Impact:** Discovery restart can use stale broadcast peer with wrong channel
   - **Severity:** HIGH - causes "Peer channel != home channel" errors

2. **Channel = 0 Race Condition**
   - **File:** [espnow_peer_manager.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_common_utils/espnow_peer_manager.cpp#L80)
   - **Issue:** `channel = 0` relies on WiFi driver internal state at exact moment of `esp_now_add_peer()` call
   - **Impact:** Timing-sensitive peer registration can capture wrong channel
   - **Severity:** MEDIUM - intermittent, depends on timing

3. **No Router Channel Change Detection**
   - **File:** N/A (missing feature)
   - **Issue:** Transmitter cannot detect when receiver's channel changes (due to router)
   - **Impact:** Permanent deadlock if router changes channel while system running
   - **Severity:** HIGH - requires manual intervention (reboot)

4. **Boot Order Dependency**
   - **File:** [espnow_transmitter.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_transmitter/espnow_transmitter.cpp#L300-L305)
   - **Issue:** Transmitter defaults to channel 1 if no receiver found during scan
   - **Impact:** If transmitter boots first, it won't find receiver that boots later on different channel
   - **Severity:** MEDIUM - workaround is to always boot receiver first

5. **Inconsistent Peer Channel Values**
   - **File:** Multiple locations (see Section 3)
   - **Issue:** Some code uses `channel = 0`, some uses explicit channel, some uses `g_lock_channel`
   - **Impact:** Confusion, maintenance burden, potential for future bugs
   - **Severity:** LOW - mostly organizational, but increases bug risk

### Secondary Contributing Factors

- **No runtime peer channel validation**: Once peer registered, never re-validated
- **No automatic channel resync**: If channels drift, no mechanism to detect/correct
- **Limited error recovery**: ESP-NOW errors logged but not automatically resolved
- **Broadcast peer used for discovery**: More fragile than directed peer-to-peer

---

## 8. Architectural Recommendations

### 8.1 Short-Term Fixes (Immediate)

#### Fix #1: Remove Broadcast Peer Early Return

**File:** [espnow_peer_manager.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_common_utils/espnow_peer_manager.cpp#L70-L90)

**Current:**
```cpp
bool add_broadcast_peer() {
    if (esp_now_is_peer_exist(broadcast_mac)) {
        return true;  // ❌ PROBLEM
    }
    // ... add peer with channel = 0
}
```

**Recommended:**
```cpp
bool add_broadcast_peer() {
    uint8_t current_channel = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_channel, &second);
    
    if (esp_now_is_peer_exist(broadcast_mac)) {
        // Peer exists - verify channel matches
        esp_now_peer_info_t peer;
        if (esp_now_get_peer(broadcast_mac, &peer) == ESP_OK) {
            if (peer.channel != current_channel && peer.channel != 0) {
                // Channel mismatch - remove and re-add
                MQTT_LOG_DEBUG("PEER_MGR", "Broadcast peer channel mismatch (%d != %d), updating",
                              peer.channel, current_channel);
                esp_now_del_peer(broadcast_mac);
            } else {
                return true;  // Peer exists with correct channel
            }
        }
    }
    
    // Add broadcast peer with explicit channel
    esp_now_peer_info_t broadcast_peer = {};
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    broadcast_peer.channel = current_channel;  // ✅ Use explicit channel
    broadcast_peer.encrypt = false;
    broadcast_peer.ifidx = WIFI_IF_STA;
    
    esp_err_t result = esp_now_add_peer(&broadcast_peer);
    if (result == ESP_OK) {
        MQTT_LOG_DEBUG("PEER_MGR", "Broadcast peer added on channel %d", current_channel);
        return true;
    } else {
        MQTT_LOG_ERROR("PEER_MGR", "Failed to add broadcast peer: %s", esp_err_to_name(result));
        return false;
    }
}
```

**Benefits:**
- Validates existing broadcast peer channel
- Updates peer if channel mismatch detected
- Uses explicit channel instead of `channel = 0` magic value
- Eliminates race condition

#### Fix #2: Add Peer Channel Validation Function

**File:** Create new utility in `espnow_peer_manager.h`

```cpp
/**
 * @brief Validate peer channel matches current WiFi channel
 * @param mac MAC address of peer
 * @return true if peer channel is valid, false if mismatch
 */
bool validate_peer_channel(const uint8_t* mac);

/**
 * @brief Synchronize peer channel with current WiFi channel
 * @param mac MAC address of peer
 * @return true if sync successful
 */
bool sync_peer_channel(const uint8_t* mac);
```

**Implementation:**
```cpp
bool validate_peer_channel(const uint8_t* mac) {
    if (!mac || !esp_now_is_peer_exist(mac)) return false;
    
    uint8_t current_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_ch, &second);
    
    esp_now_peer_info_t peer;
    if (esp_now_get_peer(mac, &peer) != ESP_OK) return false;
    
    // channel = 0 is considered valid (means "use current channel")
    if (peer.channel == 0 || peer.channel == current_ch) {
        return true;
    }
    
    char mac_str[18];
    format_mac(mac, mac_str);
    MQTT_LOG_WARNING("PEER_MGR", "Peer %s channel mismatch: peer=%d, wifi=%d",
                    mac_str, peer.channel, current_ch);
    return false;
}

bool sync_peer_channel(const uint8_t* mac) {
    if (!validate_peer_channel(mac)) {
        uint8_t current_ch = 0;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&current_ch, &second);
        
        return update_peer_channel(mac, current_ch);
    }
    return true;  // Already valid
}
```

**Usage in discovery_task.cpp:**
```cpp
bool DiscoveryTask::validate_state() {
    bool valid = true;
    
    // Validate WiFi channel
    uint8_t current_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_ch, &second);
    
    if (current_ch != g_lock_channel) {
        LOG_ERROR("WiFi channel mismatch");
        valid = false;
    }
    
    // Validate broadcast peer channel
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!EspnowPeerManager::validate_peer_channel(broadcast_mac)) {
        LOG_ERROR("Broadcast peer channel mismatch");
        EspnowPeerManager::sync_peer_channel(broadcast_mac);  // Auto-fix
        valid = false;
    }
    
    // Validate receiver peer channel
    if (!EspnowPeerManager::validate_peer_channel(receiver_mac)) {
        LOG_ERROR("Receiver peer channel mismatch");
        EspnowPeerManager::sync_peer_channel(receiver_mac);  // Auto-fix
        valid = false;
    }
    
    return valid;
}
```

#### Fix #3: Add Channel Verification After WiFi Channel Set

**File:** [discovery_task.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp#L128-L153)

**Current:**
```cpp
bool force_and_verify_channel(uint8_t target_channel) {
    if (!set_channel(target_channel)) return false;
    delay(150);  // Wait for WiFi driver
    
    uint8_t actual_ch = 0;
    esp_wifi_get_channel(&actual_ch, &second);
    
    if (actual_ch != target_channel) {
        LOG_ERROR("Channel verification failed");
        return false;
    }
    
    return true;
}
```

**Recommended:**
```cpp
bool force_and_verify_channel(uint8_t target_channel) {
    LOG_INFO("[DISCOVERY] Forcing channel lock to %d...", target_channel);
    
    // Set channel
    if (!set_channel(target_channel)) {
        LOG_ERROR("[DISCOVERY] Failed to set channel");
        return false;
    }
    
    // Wait for WiFi driver stabilization
    delay(150);
    
    // Verify WiFi channel
    uint8_t actual_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&actual_ch, &second);
    
    if (actual_ch != target_channel) {
        LOG_ERROR("[DISCOVERY] WiFi channel verification failed: expected=%d, actual=%d",
                  target_channel, actual_ch);
        return false;
    }
    
    // Synchronize broadcast peer channel
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (esp_now_is_peer_exist(broadcast_mac)) {
        if (!EspnowPeerManager::sync_peer_channel(broadcast_mac)) {
            LOG_ERROR("[DISCOVERY] Failed to sync broadcast peer channel");
            return false;
        }
    }
    
    // Synchronize receiver peer channel (if registered)
    if (receiver_mac[0] != 0 && esp_now_is_peer_exist(receiver_mac)) {
        if (!EspnowPeerManager::sync_peer_channel(receiver_mac)) {
            LOG_ERROR("[DISCOVERY] Failed to sync receiver peer channel");
            return false;
        }
    }
    
    LOG_INFO("[DISCOVERY] Channel locked and verified: %d", actual_ch);
    return true;
}
```

**Benefits:**
- Ensures all peers use correct channel after WiFi channel change
- Auto-corrects peer channel mismatches
- Reduces "Peer channel != home channel" errors

### 8.2 Medium-Term Improvements

#### Improvement #1: Receiver Channel Change Detection

**Problem:** Transmitter cannot detect when receiver's WiFi channel changes (router channel change)

**Solution:** Add channel monitoring to receiver, notify transmitter of channel changes

**Receiver Side (espnowreciever_2):**

Create new packet type for channel change notifications:

**File:** `espnow_common.h`
```cpp
enum msg_type_e : uint8_t {
    msg_data = 0,
    msg_probe = 1,
    msg_ack = 2,
    msg_packet = 3,
    msg_channel_change = 4,  // NEW
};

typedef struct {
    uint8_t type;       // msg_channel_change
    uint8_t new_channel;
    uint32_t seq;
} channel_change_notification_t;
```

**File:** `wifi_setup.cpp` (or create `wifi_monitor.cpp`)
```cpp
void monitor_wifi_channel_task(void* parameter) {
    uint8_t last_channel = WiFi.channel();
    
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // Check every second
        
        uint8_t current_channel = WiFi.channel();
        
        if (current_channel != last_channel) {
            LOG_WARN("[WIFI_MON] Channel changed: %d → %d", last_channel, current_channel);
            
            // Update global variable
            ESPNow::wifi_channel = current_channel;
            
            // Send notification to transmitter
            if (ESPNow::transmitter_connected) {
                channel_change_notification_t notif;
                notif.type = msg_channel_change;
                notif.new_channel = current_channel;
                notif.seq = millis();
                
                esp_now_send(ESPNow::transmitter_mac, (uint8_t*)&notif, sizeof(notif));
                LOG_INFO("[WIFI_MON] Sent channel change notification to transmitter");
            }
            
            last_channel = current_channel;
        }
    }
}

// Call in main.cpp setup():
xTaskCreate(monitor_wifi_channel_task, "wifi_monitor", 2048, NULL, 1, NULL);
```

**Transmitter Side (ESPnowtransmitter2):**

**File:** `message_handler.cpp`
```cpp
void handle_channel_change(const espnow_queue_msg_t* msg, void* context) {
    if (!msg || msg->len < sizeof(channel_change_notification_t)) return;
    
    const channel_change_notification_t* notif = 
        reinterpret_cast<const channel_change_notification_t*>(msg->data);
    
    LOG_WARN("[CHANNEL] Receiver channel changed to %d (was %d)", 
             notif->new_channel, g_lock_channel);
    
    // Update locked channel
    g_lock_channel = notif->new_channel;
    
    // Force channel change
    if (set_channel(notif->new_channel)) {
        LOG_INFO("[CHANNEL] Transmitter channel updated to %d", notif->new_channel);
        
        // Update all peers
        EspnowPeerManager::sync_peer_channel(receiver_mac);
        
        const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        EspnowPeerManager::sync_peer_channel(broadcast_mac);
        
    } else {
        LOG_ERROR("[CHANNEL] Failed to change transmitter channel");
        // Trigger discovery restart as fallback
        DiscoveryTask::instance().restart();
    }
}

// Register handler in setup_message_routes():
router_.register_handler(msg_channel_change, handle_channel_change, nullptr);
```

**Benefits:**
- Automatic recovery from router channel changes
- No transmitter reboot required
- Proactive notification before timeout

#### Improvement #2: Bidirectional Channel Negotiation

**Current:** Transmitter scans to find receiver (unidirectional)

**Proposed:** Both devices announce on current channel, negotiate to common channel

**Rationale:**
- Handles "transmitter boots first" scenario
- Faster discovery (no full channel scan needed)
- More robust to timing issues

**Algorithm:**
```
TRANSMITTER (boots first on channel 1):
1. Broadcasts announcement on channel 1: "I'm here on channel 1"
2. No response (receiver not booted yet)
3. Continues broadcasting on channel 1

RECEIVER (boots later on channel 6):
1. Connects to router → channel 6
2. Broadcasts announcement on channel 6: "I'm here on channel 6"
3. No response from transmitter (on different channel)
4. Starts periodic channel scan:
   - Listens on channel 1 for 100ms
   - Hears transmitter announcement!
   - Sends directed message to transmitter: "I'm on channel 6, switch to me"
5. Transmitter receives message → switches to channel 6
6. Bidirectional communication established
```

**Implementation Notes:**
- Receiver must scan channels to find transmitter
- Receiver cannot change its own channel (tied to router)
- Transmitter must follow receiver's channel
- Adds complexity but solves boot order dependency

#### Improvement #3: Use Explicit Channel Everywhere

**Goal:** Eliminate all uses of `channel = 0` magic value

**Files to Modify:**
1. `espnow_peer_manager.cpp` - `add_broadcast_peer()`
2. `espnow_tasks.cpp` - receiver peer registration
3. `message_handler.cpp` - transmitter peer registration

**Benefits:**
- Removes timing-sensitive behavior
- Makes channel management explicit and traceable
- Easier to debug channel issues
- More deterministic behavior

**Example:**
```cpp
// Before (implicit)
EspnowPeerManager::add_peer(mac, 0);  // What channel will this use?

// After (explicit)
uint8_t current_ch = WiFi.channel();
EspnowPeerManager::add_peer(mac, current_ch);  // Clear what channel is used
```

### 8.3 Long-Term Architectural Changes

#### Option A: Unified Channel Manager

Create centralized channel management service:

**File:** `channel_manager.h`
```cpp
class ChannelManager {
public:
    static ChannelManager& instance();
    
    // Channel operations
    bool set_channel(uint8_t channel);
    uint8_t get_channel() const;
    bool verify_channel() const;
    
    // Peer synchronization
    bool sync_all_peers();
    bool sync_peer(const uint8_t* mac);
    
    // Monitoring
    void start_monitoring();
    void register_channel_change_callback(std::function<void(uint8_t)> callback);
    
    // Diagnostics
    void audit_channel_state();
    
private:
    uint8_t locked_channel_{0};
    std::vector<std::function<void(uint8_t)>> callbacks_;
};
```

**Benefits:**
- Single source of truth for channel management
- Centralized peer synchronization
- Automatic peer updates on channel changes
- Easier to maintain and debug

#### Option B: Remove Broadcast Peer Dependency

**Current:** Discovery uses broadcast peer (FF:FF:FF:FF:FF:FF) for PROBE messages

**Alternative:** Use directed peer-to-peer once MAC addresses known

**Rationale:**
- Broadcast peer adds complexity (channel synchronization)
- Directed messages more reliable
- Can use receiver MAC once discovered

**Implementation:**
- During initial discovery: scan channels, use temporary broadcast peer
- After discovery: remove broadcast peer, use only receiver MAC
- Announcements: send to receiver MAC instead of broadcast
- Eliminates persistent broadcast peer channel issues

**Challenges:**
- Need receiver MAC for discovery (chicken-egg problem)
- May require device pairing/configuration step
- Less flexible for multi-receiver scenarios

---

## 9. Specific Code Fixes

### Fix #1: Update add_broadcast_peer()

**File:** [espnow_peer_manager.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_common_utils/espnow_peer_manager.cpp#L70-L90)

**Current Code:**
```cpp
bool add_broadcast_peer() {
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    if (esp_now_is_peer_exist(broadcast_mac)) {
        return true;
    }
    
    esp_now_peer_info_t broadcast_peer = {};
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    broadcast_peer.channel = 0;
    broadcast_peer.encrypt = false;
    broadcast_peer.ifidx = WIFI_IF_STA;
    
    esp_err_t result = esp_now_add_peer(&broadcast_peer);
    // ...
}
```

**Fixed Code:**
```cpp
bool add_broadcast_peer() {
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    // Get current WiFi channel explicitly
    uint8_t current_channel = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_channel, &second);
    
    // If peer exists, verify channel matches
    if (esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_peer_info_t peer;
        if (esp_now_get_peer(broadcast_mac, &peer) == ESP_OK) {
            // Check if channel matches (channel=0 is acceptable, means "use current")
            if (peer.channel != 0 && peer.channel != current_channel) {
                MQTT_LOG_WARNING("PEER_MGR", 
                    "Broadcast peer channel mismatch (peer=%d, wifi=%d) - removing and re-adding",
                    peer.channel, current_channel);
                esp_now_del_peer(broadcast_mac);
                // Fall through to re-add with correct channel
            } else {
                MQTT_LOG_DEBUG("PEER_MGR", "Broadcast peer already exists with correct channel");
                return true;
            }
        }
    }
    
    // Create broadcast peer with explicit channel
    esp_now_peer_info_t broadcast_peer = {};
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    broadcast_peer.channel = current_channel;  // Use explicit channel, not 0
    broadcast_peer.encrypt = false;
    broadcast_peer.ifidx = WIFI_IF_STA;
    
    esp_err_t result = esp_now_add_peer(&broadcast_peer);
    if (result == ESP_OK) {
        MQTT_LOG_INFO("PEER_MGR", "Broadcast peer added on channel %d", current_channel);
        return true;
    } else {
        MQTT_LOG_ERROR("PEER_MGR", "Failed to add broadcast peer: %s", esp_err_to_name(result));
        return false;
    }
}
```

**Changes:**
1. Get current WiFi channel explicitly before checking peer existence
2. If peer exists, validate its channel matches current WiFi channel
3. If channel mismatch, remove old peer and re-add with correct channel
4. Use explicit `current_channel` instead of `channel = 0` magic value
5. Enhanced logging for debugging

---

### Fix #2: Add Peer Channel Validation Utilities

**File:** [espnow_peer_manager.h](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_common_utils/espnow_peer_manager.h)

**Add to namespace:**
```cpp
namespace EspnowPeerManager {
    // ... existing functions ...
    
    /**
     * @brief Validate peer's channel matches current WiFi channel
     * @param mac MAC address of peer to validate
     * @return true if peer channel is valid (matches WiFi or is 0), false if mismatch
     */
    bool validate_peer_channel(const uint8_t* mac);
    
    /**
     * @brief Synchronize peer's channel with current WiFi channel
     * @param mac MAC address of peer to sync
     * @return true if sync successful
     */
    bool sync_peer_channel(const uint8_t* mac);
    
    /**
     * @brief Audit all registered peers and log channel mismatches
     */
    void audit_all_peers();
}
```

**File:** [espnow_peer_manager.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESP32common/espnow_common_utils/espnow_peer_manager.cpp)

**Add implementations:**
```cpp
bool validate_peer_channel(const uint8_t* mac) {
    if (!mac || !esp_now_is_peer_exist(mac)) {
        return false;
    }
    
    uint8_t current_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_ch, &second);
    
    esp_now_peer_info_t peer;
    if (esp_now_get_peer(mac, &peer) != ESP_OK) {
        return false;
    }
    
    // channel = 0 is valid (means "use current channel")
    if (peer.channel == 0 || peer.channel == current_ch) {
        return true;
    }
    
    char mac_str[18];
    format_mac(mac, mac_str);
    MQTT_LOG_WARNING("PEER_MGR", "Peer %s channel mismatch: peer=%d, wifi=%d",
                    mac_str, peer.channel, current_ch);
    return false;
}

bool sync_peer_channel(const uint8_t* mac) {
    if (!mac || !esp_now_is_peer_exist(mac)) {
        return false;
    }
    
    // Check if sync needed
    if (validate_peer_channel(mac)) {
        return true;  // Already valid
    }
    
    // Get current channel
    uint8_t current_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_ch, &second);
    
    char mac_str[18];
    format_mac(mac, mac_str);
    MQTT_LOG_INFO("PEER_MGR", "Syncing peer %s to channel %d", mac_str, current_ch);
    
    // Update peer channel (removes and re-adds)
    return update_peer_channel(mac, current_ch);
}

void audit_all_peers() {
    uint8_t current_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_ch, &second);
    
    MQTT_LOG_INFO("PEER_MGR", "=== Peer Channel Audit ===");
    MQTT_LOG_INFO("PEER_MGR", "WiFi Channel: %d", current_ch);
    
    // Check broadcast peer
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_peer_info_t peer;
        if (esp_now_get_peer(broadcast_mac, &peer) == ESP_OK) {
            const char* status = (peer.channel == 0 || peer.channel == current_ch) ? "OK" : "MISMATCH";
            MQTT_LOG_INFO("PEER_MGR", "  Broadcast: channel=%d [%s]", peer.channel, status);
        }
    } else {
        MQTT_LOG_INFO("PEER_MGR", "  Broadcast: NOT REGISTERED");
    }
    
    // Note: Cannot enumerate all peers via ESP-NOW API
    // Would need to maintain list of registered peers separately
    
    MQTT_LOG_INFO("PEER_MGR", "======================");
}
```

---

### Fix #3: Enhanced Discovery Restart with Peer Sync

**File:** [discovery_task.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESPnowtransmitter2/espnowtransmitter2/src/espnow/discovery_task.cpp#L128-L153)

**Update force_and_verify_channel:**
```cpp
bool DiscoveryTask::force_and_verify_channel(uint8_t target_channel) {
    LOG_INFO("[DISCOVERY] Forcing channel lock to %d...", target_channel);
    
    // Step 1: Set WiFi channel
    if (!set_channel(target_channel)) {
        LOG_ERROR("[DISCOVERY]   ✗ Failed to set channel to %d", target_channel);
        return false;
    }
    
    LOG_DEBUG("[DISCOVERY]   - Channel set command executed");
    
    // Step 2: Wait for WiFi driver stabilization
    delay(150);
    
    // Step 3: Verify WiFi channel was set
    uint8_t actual_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&actual_ch, &second);
    
    if (actual_ch != target_channel) {
        LOG_ERROR("[DISCOVERY]   ✗ Channel verification failed: expected=%d, actual=%d",
                  target_channel, actual_ch);
        metrics_.channel_mismatches++;
        return false;
    }
    
    LOG_INFO("[DISCOVERY]   ✓ WiFi channel verified: %d", actual_ch);
    
    // Step 4: Synchronize broadcast peer channel
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (esp_now_is_peer_exist(broadcast_mac)) {
        if (!EspnowPeerManager::validate_peer_channel(broadcast_mac)) {
            LOG_WARN("[DISCOVERY]   ⚠ Broadcast peer channel mismatch, syncing...");
            if (!EspnowPeerManager::sync_peer_channel(broadcast_mac)) {
                LOG_ERROR("[DISCOVERY]   ✗ Failed to sync broadcast peer channel");
                return false;
            }
            LOG_INFO("[DISCOVERY]   ✓ Broadcast peer channel synced");
        }
    }
    
    // Step 5: Synchronize receiver peer channel (if registered)
    if ((receiver_mac[0] != 0 || receiver_mac[1] != 0) && 
        esp_now_is_peer_exist(receiver_mac)) {
        if (!EspnowPeerManager::validate_peer_channel(receiver_mac)) {
            LOG_WARN("[DISCOVERY]   ⚠ Receiver peer channel mismatch, syncing...");
            if (!EspnowPeerManager::sync_peer_channel(receiver_mac)) {
                LOG_ERROR("[DISCOVERY]   ✗ Failed to sync receiver peer channel");
                return false;
            }
            LOG_INFO("[DISCOVERY]   ✓ Receiver peer channel synced");
        }
    }
    
    LOG_INFO("[DISCOVERY]   ✓ Channel locked and verified: %d", actual_ch);
    return true;
}
```

**Changes:**
1. Added peer channel validation after WiFi channel set
2. Auto-syncs broadcast peer if channel mismatch detected
3. Auto-syncs receiver peer if registered and channel mismatch
4. Enhanced logging with step-by-step status
5. More robust error handling

---

### Fix #4: Consistent Use of Explicit Channels

**File:** [espnow_tasks.cpp](c:/Users/GrahamWillsher/ESP32Projects/espnowreciever_2/src/espnow/espnow_tasks.cpp#L524)

**Current:**
```cpp
if (!EspnowPeerManager::is_peer_registered(queue_msg.mac)) {
    EspnowPeerManager::add_peer(queue_msg.mac, 0);
}
```

**Fixed:**
```cpp
if (!EspnowPeerManager::is_peer_registered(queue_msg.mac)) {
    uint8_t current_channel = WiFi.channel();
    EspnowPeerManager::add_peer(queue_msg.mac, current_channel);
}
```

**File:** [message_handler.cpp](c:/Users/GrahamWillsher/ESP32Projects/ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_handler.cpp#L1069)

**Current:**
```cpp
if (!EspnowPeerManager::is_peer_registered(receiver_mac_)) {
    EspnowPeerManager::add_peer(receiver_mac_);  // Uses default 0
}
```

**Fixed:**
```cpp
if (!EspnowPeerManager::is_peer_registered(receiver_mac_)) {
    EspnowPeerManager::add_peer(receiver_mac_, g_lock_channel);
}
```

**Rationale:**
- Eliminates reliance on `channel = 0` magic value
- Makes channel usage explicit and traceable
- Reduces timing-sensitive behavior
- Easier to debug

---

## 10. Testing Recommendations

### Test Case 1: Normal Boot (Receiver First)

**Steps:**
1. Power on receiver, wait for WiFi connection
2. Verify receiver logs show channel (e.g., "WiFi Channel: 6")
3. Power on transmitter
4. Verify transmitter discovers receiver on correct channel
5. Verify `g_lock_channel` matches receiver channel
6. Check for ESP-NOW communication errors

**Expected:**
- Transmitter discovers receiver within 5-10 seconds
- Both devices on same channel
- No "Peer channel != home channel" errors
- Successful bidirectional communication

### Test Case 2: Boot Order Reversed (Transmitter First)

**Steps:**
1. Power on transmitter first
2. Verify transmitter completes channel scan (1-13)
3. Verify transmitter defaults to channel 1 (no receiver found)
4. Power on receiver (connects to router on channel 6)
5. Monitor transmitter logs for discovery

**Expected (Current Behavior):**
- Transmitter on channel 1, receiver on channel 6
- No discovery (different channels)
- Manual intervention required (reboot transmitter)

**Expected (After Fixes):**
- Bidirectional discovery should eventually find each other
- Or clear error message indicating boot order issue

### Test Case 3: Discovery Restart

**Steps:**
1. Establish normal communication (both devices connected)
2. Power off receiver
3. Wait 10+ seconds for transmitter timeout
4. Verify transmitter restarts discovery
5. Monitor for "Peer channel != home channel" errors
6. Power on receiver
7. Verify reconnection

**Expected (After Fixes):**
- Discovery restart without channel mismatch errors
- Broadcast peer channel validated/synced
- Successful reconnection within 10-20 seconds

### Test Case 4: Router Channel Change

**Steps:**
1. Establish normal communication on channel 6
2. Change router channel to 11 (via router admin)
3. Receiver reconnects on channel 11
4. Monitor transmitter behavior

**Expected (Current Behavior):**
- Transmitter still on channel 6
- Connection lost, timeout occurs
- Discovery restart still uses channel 6
- Permanent deadlock (requires transmitter reboot)

**Expected (After Medium-Term Fixes):**
- Receiver detects channel change
- Sends notification to transmitter
- Transmitter switches to channel 11
- Connection re-established automatically

### Test Case 5: Simultaneous Boot

**Steps:**
1. Power on both devices simultaneously
2. Monitor discovery process
3. Check for timing races or failures

**Expected:**
- May require multiple discovery attempts
- Should eventually discover (within 30-60 seconds)
- No permanent deadlock

### Test Case 6: Peer Channel Audit

**Steps:**
1. Establish normal communication
2. Call `EspnowPeerManager::audit_all_peers()` on transmitter
3. Verify log output shows correct channels

**Expected:**
- WiFi channel matches g_lock_channel
- Broadcast peer channel matches (or is 0)
- Receiver peer channel matches (or is 0)
- No mismatches reported

---

## 11. Implementation Priority

### Priority 1 (Critical - Implement Immediately)

1. **Fix add_broadcast_peer()** - Section 9, Fix #1
   - Validates existing peer channel
   - Uses explicit channel instead of 0
   - Auto-corrects channel mismatches
   - **Impact:** Eliminates most "Peer channel != home channel" errors

2. **Add Peer Channel Validation** - Section 9, Fix #2
   - `validate_peer_channel()`
   - `sync_peer_channel()`
   - **Impact:** Runtime peer channel validation and correction

3. **Enhanced Discovery Restart** - Section 9, Fix #3
   - Syncs peer channels after WiFi channel set
   - More robust verification
   - **Impact:** Eliminates discovery restart channel errors

### Priority 2 (Important - Implement Soon)

4. **Consistent Explicit Channels** - Section 9, Fix #4
   - Replace all `channel = 0` with explicit values
   - **Impact:** More predictable behavior, easier debugging

5. **Documentation and Logging**
   - Add channel value to all relevant log messages
   - Document boot order requirements
   - **Impact:** Easier troubleshooting for users

### Priority 3 (Enhancement - Future Work)

6. **Router Channel Change Detection** - Section 8.2, Improvement #1
   - Receiver monitors WiFi channel
   - Notifies transmitter of changes
   - **Impact:** Handles router channel changes automatically

7. **Bidirectional Discovery** - Section 8.2, Improvement #2
   - Both devices scan for each other
   - **Impact:** Eliminates boot order dependency

8. **Unified Channel Manager** - Section 8.3, Option A
   - Centralized channel management service
   - **Impact:** Long-term maintainability and robustness

---

## 12. Conclusion

### Root Cause Summary

The ESP-NOW channel mismatch issue stems from **three interconnected problems**:

1. **Broadcast peer persistence**: Old broadcast peers survive discovery restart with stale channel values, and `add_broadcast_peer()` returns early without validating channel
2. **Channel = 0 race condition**: Reliance on "magic value" creates timing-sensitive behavior where peer registration captures wrong WiFi channel
3. **No runtime validation**: Once peers are registered, their channels are never re-validated or synchronized with WiFi channel changes

### Boot Order Dependency

**Does receiver need to boot first?** 

In the current architecture, **YES** - receiver should boot first for reliable discovery:
- Receiver channel determined by router (cannot be changed)
- Transmitter scans to find receiver
- If transmitter boots first, it defaults to channel 1 and cannot find receiver on different channel
- Workaround: Reboot transmitter after receiver is up, or implement bidirectional discovery

### Immediate Actions

Implement the **Priority 1 fixes**:
1. Update `add_broadcast_peer()` to validate and sync channel
2. Add `validate_peer_channel()` and `sync_peer_channel()` utilities
3. Enhance `force_and_verify_channel()` to sync peer channels

These fixes will eliminate most "Peer channel != home channel" errors and make the system significantly more robust.

### Long-Term Strategy

Consider implementing:
- Router channel change detection (proactive notification)
- Bidirectional discovery (eliminates boot order dependency)
- Unified channel manager (centralized control)
- Remove broadcast peer dependency (use directed peers only)

---

**Review Completed:** February 10, 2026  
**Next Steps:** Implement Priority 1 fixes and test with all scenarios
