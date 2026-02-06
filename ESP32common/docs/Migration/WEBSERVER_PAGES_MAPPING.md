# Battery Emulator Web Pages Migration Mapping

**Created**: February 6, 2026  
**Phase**: 0 (Pre-Migration Setup)  
**Purpose**: Map Battery Emulator web pages to ESP32Projects receiver destination

---

## Battery Emulator Web Pages (Source)

Based on analysis of `Battery-Emulator-9.2.4/Software/src/devboard/webserver/`:

| Page File | Route | Description | Priority | Content |
|-----------|-------|-------------|----------|---------|
| index_html.cpp | `/` | Main status dashboard | HIGH | Live battery metrics, SOC, power, voltage, current, contactors |
| settings_html.cpp | `/settings` | System settings | HIGH | Battery limits, MQTT, WiFi, capacity settings |
| advanced_battery_html.cpp | `/advanced_battery` | Advanced battery settings | MEDIUM | Chemistry, cell config, SOC scaling |
| cellmonitor_html.cpp | `/cellmonitor` | Individual cell voltages | MEDIUM | Cell voltage table, balancing status, min/max cells |
| events_html.cpp | `/events` | Event log viewer | LOW | Error history, warnings, timestamps |
| debug_logging_html.cpp | `/debug_logging` | Debug logging control | LOW | Debug level, logging settings |
| can_logging_html.cpp | `/can_logging` | CAN message logging | LOW | CAN traffic capture and display |
| can_replay_html.cpp | `/can_replay` | CAN replay tool | LOW | Upload and replay CAN logs |

### Additional API Endpoints
- `POST /saveSettings` - Save settings to NVS
- `POST /factoryReset` - Factory reset device
- `POST /reboot` - Reboot device
- `GET /get_firmware_info` - Firmware version info
- `GET /metrics` - Prometheus metrics (optional)

---

## Receiver Current Pages (Destination)

From `espnowreciever_2/lib/webserver/pages/`:

| File | Route | Description |
|------|-------|-------------|
| monitor_page.cpp | `/` | Basic monitor (SOC, power, test mode) |
| ota_page.cpp | `/ota` | Firmware OTA upload |
| debug_page.cpp | `/debug` | Debug level control |
| settings_page.cpp | `/settings` | Basic system settings |
| systeminfo_page.cpp | `/systeminfo` | Device info |
| reboot_page.cpp | `/reboot` | Reboot control |

---

## Migration Mapping

### Architecture Overview: Two-Device Settings

**CRITICAL**: Settings are split across TWO devices:

| Setting Category | Device | Storage | Examples |
|-----------------|--------|---------|----------|
| **Transmitter Settings** | ESP32-POE-ISO | Transmitter NVS | Battery limits, MQTT, Ethernet, CAN config |
| **Receiver Settings** | LilyGo T-Display-S3 | Receiver NVS | Display brightness, WiFi, web auth |

**Network Architecture**:
- **Transmitter**: Ethernet only (static or DHCP selectable)
- **Receiver**: WiFi only (for web UI access)
- **MQTT**: Transmitter publishes telemetry via Ethernet
- **ESP-NOW**: Communication bridge between devices

--- (DYNAMIC - only when page subscribed):
- `msg_battery_status` (voltage, current, SOC, temp, power) - 200ms
- `msg_charger_status` (HV/LV voltage/current, AC voltage/current) - 200ms
- `msg_inverter_status` (AC voltage, frequency, power) - 200ms
- `msg_system_status` (contactors, BMS status) - 200ms

**ESP-NOW messages needed** (STATIC - once on first connection):
- `msg_battery_info` (capacity, chemistry, cell count, limits) - once
- `msg_system_info` (firmware version, hardware type) - once

**Page Lifecycle**:
1. User navigates to Monitor page
2. JavaScript sends `/api/subscribe?page=monitor`
3. Receiver sends `msg_subscribe_page(PAGE_MONITOR)` to transmitter
4. Transmitter starts sending battery/charger/inverter/system status every 200ms
5. Receiver forwards to SSE clients
6. User navigates away or closes browser
7. JavaScript sends `/api/unsubscribe?page=monitor` (or timeout after 60 seconds)
8. Receiver sends `msg_unsubscribe_page(PAGE_MONITOR)`
9. Transmitter STOPS sending data

#### Static Data (Collected Once on First Connection)
**Characteristics**:
- Retrieved ONCE when receiver first connects to transmitter
- Stored in receiver memory (not NVS)
- Does NOT change during operation
- Does NOT require periodic updates

**Examples**:
- Battery info (capacity, chemistry, number of cells, max/min voltages)
- System info (firmware version, hardware type, device ID)
- Transmitter network info (MAC address, hostname)
- Available features (has CAN, has charger, has inverter)

**ESP-NOW Message**: `msg_static_data_request` → `msg_static_data_response`
**Frequency**: Once on connection, or on manual refresh

---

#### Dynamic Data (Updated Only When Page Active)
**Characteristics**:
- Retrieved ONLY when specific webpage is open
- Updated continuously via SSE while page is active
- NO data sent when page is closed
- Minimizes bandwidth and CPU usage
- **CRITICAL**: Dynamic data sending runs on **separate task (Priority 1)** and **MUST NOT interfere** with main control loop (Priority 4)

**Examples**:
- Battery status (voltage, current, SOC, temperature) - for Monitor page
- Cell voltages (all cells) - for Cell Monitor page
- Events/logs - for Events page
- Charger status - for Monitor/Charger pages
- Inverter status - for Monitor/Inverter pages

**ESP-NOW Message**: `msg_subscribe_page` → periodic `msg_battery_status`, `msg_charger_status`, etc.
**Frequency**: 
- Subscribe when page opens
- Unsubscribe when page closes
- SSE updates at 200ms-1000ms while subscribed

---

### On-Demand Data Flow

**Principle**: **If no one is looking at a webpage, no dynamic data is sent**

```
User opens Monitor page → Receiver sends msg_subscribe_page(MONITOR) → 
Transmitter starts sending battery_status/charger_status/system_status every 200ms →
Receiver forwards to SSE clients →
User closes page → Receiver sends msg_unsubscribe_page(MONITOR) →
Transmitter STOPS sending data for that page
```

**Page Subscription Types**:

| Page | Subscription | Data Messages | Update Rate |
|------|-------------|---------------|-------------|
| Monitor | SUBSCRIBE_MONITOR | battery_status, charger_status, inverter_status, system_status | 200ms |
| Battery Settings | SUBSCRIBE_BATTERY_SETTINGS | battery_settings (once) | On open only |
| Charger Settings | SUBSCRIBE_CHARGER_SETTINGS | charger_settings (once) | On open only |
| Cell Monitor | SUBSCRIBE_CELLS | cell_voltages (chunked) | 1000ms |
| Events | SUBSCRIBE_EVENTS | event_history (chunked) | 5000ms |
| System Info | None (static data) | system_info (once) | On open only |

**ESP-NOW Messages**:

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_subscribe_page
  uint8_t page_id;                       // Which page to subscribe to
  uint16_t checksum;
} subscribe_page_msg_t;

typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_unsubscribe_page
  uint8_t page_id;                       // Which page to unsubscribe from
  uint16_t checksum;
} unsubscribe_page_msg_t;

enum PageSubscription {
  PAGE_MONITOR = 0,
  PAGE_CELLS = 1,
  PAGE_EVENTS = 2,
  PAGE_BATTERY_SETTINGS = 3,  // One-time data fetch
  PAGE_CHARGER_SETTINGS = 4,
  PAGE_INVERTER_SETTINGS = 5,
  PAGE_SYSTEM_SETTINGS = 6
};
```

---

### Phase 3 Implementation Plan

#### 1. Main Status Page (2 days)
**Target**: `/` (enhance existing `monitor_page.cpp`)

**Add to existing page**:
- Battery voltage (dV → V)
- Battery current (dA → A)
- Battery temperature (min/max in °C)
- Charger status (HV voltage, HV current, AC voltage, AC current)
- Inverter status (AC voltage, frequency, power)
- Contactor states (main+, main-, precharge, charger)
- Real-time charts (SOC history, power flow graph)
- **Keep existing**: Test mode toggle, current monitor display

**ESP-NOW messages needed**:
- `msg_battery_status` (voltage, current, SOC, temp, power)
- `msg_charger_status` (HV/LV voltage/current, AC voltage/current)
- `msg_inverter_status` (AC voltage, frequency, power)
- `msg_system_status` (contactors, BMS status)

**Files to modify**:
- `lib/webserver/pages/monitor_page.cpp`
- `lib/webserver/api/api_handlers.cpp` (add `/api/battery_full_status`)
- `src/espnow/battery_handlers.cpp` (message handlers)

---

#### 2. Battery Settings Page (2 days)
**Target**: `/battery_settings` (NEW)
**Device**: Transmitter settings (ESP32-POE-ISO)

**Content**:
- Total capacity (Wh)
- Max charge current (A)
- Max discharge current (A)
- Max charge voltage (V)
- Min discharge voltage (V)
- SOC high limit (%)
- SOC low limit (%)
- SOC scaling active (checkbox)
- Chemistry selection (dropdown: NCA, NMC, LFP, LTO)
- **Save button** → ESP-NOW msg_battery_settings_update

**Data Type**: STATIC (load once when page opens)

**Settings Save Flow**:
1. User clicks Save → Receiver sends `msg_battery_settings_update` via ESP-NOW
2. **Transmitter receives message and writes to its own NVS**
3. Transmitter sends `msg_settings_update_ack` back to receiver
4. Receiver displays success/error message to user

**ESP-NOW messages**:
- `msg_battery_settings` (READ current settings) - once on page open
- `msg_battery_settings_update` (WRITE new settings) - receiver sends to transmitter
- `msg_settings_update_ack` (ACK from transmitter) - after **transmitter** completes NVS write

**Files to create**:
- `lib/webserver/pages/battery_settings_page.cpp`
- `lib/webserver/pages/battery_settings_page.h`
- Update `lib/webserver/page_definitions.cpp` (register route)

---

#### 3. Charger Settings Page (1 day)
**Target**: `/charger_settings` (NEW)
**Device**: Transmitter settings (ESP32-POE-ISO)

**Content**:
- Max charge power (W)
- Target charge voltage (V)
- End-of-charge current (A)
- Charger type selection (dropdown: Eltek, Victron, Generic)
- **Save button** → ESP-NOW msg_charger_settings_update

**Data Type**: STATIC (load once when page opens)

**Settings Save Flow**: Receiver → ESP-NOW → **Transmitter writes to its own NVS** → ACK → Receiver confirms

**ESP-NOW messages**:
- `msg_charger_settings` - once on page open
- `msg_charger_settings_update` - receiver sends to transmitter
- `msg_settings_update_ack` - after **transmitter** completes NVS write

**Files to create**:
- `lib/webserver/pages/charger_settings_page.cpp`
- `lib/webserver/pages/charger_settings_page.h`

---

#### 4. Inverter Settings Page (1 day)
**Target**: `/inverter_settings` (NEW)
**Device**: Transmitter settings (ESP32-POE-ISO)

**Content**:
- Max discharge power (W)
- Inverter type selection (dropdown: SMA, Fronius, SolarEdge, Generic)
- AC voltage setpoint (V)
- Frequency setpoint (Hz)
- **Save button** → ESP-NOW msg_inverter_settings_update

**Data Type**: STATIC (load once when page opens)

**Settings Save Flow**: Receiver → ESP-NOW → **Transmitter writes to its own NVS** → ACK → Receiver confirms

**ESP-NOW messages**:
- `msg_inverter_settings` - once on page open
- `msg_inverter_settings_update` - receiver sends to transmitter
- `msg_settings_update_ack` - after **transmitter** completes NVS write

**Files to create**:
- `lib/webserver/pages/inverter_settings_page.cpp`
- `lib/webserver/pages/inverter_settings_page.h`

---

#### 5. System Settings Page (1 day)
**Target**: `/settings` (enhance existing `settings_page.cpp`)
**Device**: BOTH transmitter and receiver settings

**Transmitter Settings** (ESP32-POE-ISO):
- **Ethernet Configuration**:
  - DHCP / Static IP (radio buttons)
  - Static IP address (4 fields, shown if Static selected)
  - Gateway (4 fields, shown if Static selected)
  - Subnet mask (4 fields, shown if Static selected)
  - DNS server (4 fields, shown if Static selected)
- **MQTT Configuration**:
  - MQTT enabled (checkbox)
  - MQTT server (text input)
  - MQTT port (number input, default 1883)
  - MQTT username (text input)
  - MQTT password (password input)
  - MQTT timeout (ms)
  - Home Assistant discovery (checkbox)
  - Transmit cell voltages (checkbox)
- SD card logging enabled (checkbox)
- CAN logging enabled (checkbox)

**Receiver Settings** (LilyGo T-Display-S3):
- Display brightness (slider 0-100%)
- Display timeout (seconds)
- WiFi SSID (text input)
- WiFi password (password input)
- Web authentication enabled (checkbox)
- Web username (text input)
- Web password (password input)
- **Keep existing**: Debug level

**Data Type**: STATIC (load once, save triggers update)

**ESP-NOW messages**:
- `msg_system_settings` (READ transmitter settings) - once on page open
- `msg_system_settings_update` (WRITE transmitter settings) - on save
- `msg_settings_update_ack` - after transmitter save
- Receiver settings saved locally (no ESP-NOW needed)

**Files to modify**:
- `lib/webserver/pages/settings_page.cpp`

---

#### 6. Events/Logs Page (2 days)
**Target**: `/events` (NEW)

**Content**:
- Table of events with columns:
  - Timestamp (formatted date/time)
  - Severity (icon: ℹ️ info, ⚠️ warning, ❌ error)
  - Message (text description)
- Filter by severity (buttons: All, Info, Warning, Error)
- Clear all button (with confirmation)
- Auto-refresh via SSE (only while page is open)
- Pagination (50 events per page)

**Data Type**: DYNAMIC (subscribe when page opens)

**Page Lifecycle**:
1. User opens Events page
2. Subscribe to PAGE_EVENTS
3. Transmitter sends `msg_event_history` every 5 seconds (only new events)
4. SSE updates events table
5. User closes page → unsubscribe → transmitter stops sending

**ESP-NOW messages**:
- `msg_subscribe_page(PAGE_EVENTS)` - on page open
- `msg_event_history` (array of events, chunked if needed) - every 5s while subscribed
- `msg_unsubscribe_page(PAGE_EVENTS)` - on page close
- `msg_clear_events` (clear event log) - on button click

**Files to create**:
- `lib/webserver/pages/events_page.cpp`
- `lib/webserver/pages/events_page.h`
- `lib/webserver/api/events_api.cpp` (API endpoint for filtering)

---

#### 7. Cell Monitor Page (2 days)
**Target**: `/cells` (NEW)

**Content**:
- Table of cell voltages (all cells)
- Columns: Cell #, Voltage (mV), Delta from average (mV), Balancing status
- Highlight min/max cells (color coding)
- Cell voltage distribution chart (histogram)
- Temperature distribution (if multi-point temp sensors)
- Handle unavailable data gracefully (show "Waiting for cell data...")

**ESP-NOW messages**:
- `msg_cell_voltages` (array of uint16_t, up to 200 cells)
- `msg_cell_balancing` (bitfield of balancing status)

**Files to create**:
- `lib/webserver/pages/cells_page.cpp`
- `lib/webserver/pages/cells_page.h`

**Note**: Cell data may require multiple ESP-NOW packets due to size. Use chunking or request/response pattern.

---

#### 8. Navigation Update (0.5 days)
**Target**: Update navigation bar on all pages

**New navigation structure**:
```
Monitor    Settings        Diagnostics      System
  |           |                |               |
  ├─ /        ├─ /battery_     ├─ /cells       ├─ /systeminfo
              │    settings    ├─ /events      ├─ /ota
              ├─ /charger_     └─ /debug       └─ /reboot
              │    settings
              ├─ /inverter_
              │    settings
              └─ /settings
```

**Files to modify**:
- `lib/webserver/common/nav_buttons.cpp`
- All page headers (add nav bar)

---

## Deferred Pages (LOW PRIORITY)

These pages from Battery Emulator are **NOT** migrated in Phase 3:

| Page | Reason | Future Plan |
|------|--------|-------------|
| `/debug_logging` | Redundant with `/debug` | Merge functionality if needed |
| `/can_logging` | Specialized testing tool | Add in Phase 8 if needed |
| `/can_replay` | Specialized testing tool | Add in Phase 8 if needed |
| `/advanced_battery` | Settings already covered | May add later if requested |

---

## Updated Testing Checklist (Phase 3)

- [ ] All new pages render correctly on desktop browser
- [ ] All new pages render correctly on mobile browser
- [ ] Navigation works between all pages
- [ ] **Subscribe message sent when page opens**
- [ ] **Unsubscribe message sent when page closes**
- [ ] **Dynamic data only sent for subscribed pages**
- [ ] **Static data collected once on first connection**
- [ ] **Ethernet DHCP/Static selection works**
- [ ] **Static IP fields only shown when Static selected**
- [ ] **Transmitter settings clearly separated from receiver settings**
- [ ] Save buttons trigger ESP-NOW messages
- [ ] Loading spinners show while waiting for data
- [ ] Placeholder text shown for unavailable data
- [ ] SSE updates work for real-time data (only when subscribed)
- [ ] Forms validate input (range checks, required fields)
- [ ] Error messages display when save fails
- [ ] Success messages display when save succeeds
- [ ] Page reload preserves settings
- [ ] Graceful degradation when transmitter disconnected
- [ ] **Subscription timeout after 60 seconds of inactivity**
- [ ] **No ESP-NOW traffic when no pages are open**

---

## Dummy Data Handling (Phase 3)

Since Phase 3 comes BEFORE Phase 4 (core integration), all pages must handle **dummy data** from transmitter's dummy data generator:

1. **Monitor page**: Show dummy battery voltage ~48V, current ±50A, SOC cycling 20-80%
2. **Settings pages**: Load dummy settings (static data), save to dummy NVS (logs only)
3. **Events page**: Show dummy events (startup, warning, error samples) - subscribe/unsubscribe working
4. **Cells page**: Show dummy cell voltages 3.2-3.7V with realistic deviation - subscribe/unsubscribe working

**Dummy data generator** (from Phase 1) will provide:
- Realistic voltage/current/SOC patterns
- Simulated charge/discharge cycles
- Occasional warning/fault events
- Cell voltage variation
- **Page subscription awareness** - only sends data for subscribed pages

**Goal**: Fully functional web interface with subscribe/unsubscribe working, ready for real data in Phase 4.

---

## New Requirements Summary

### 1. Two-Device Settings Architecture
- ✅ Transmitter settings: Battery, MQTT, Ethernet, CAN
- ✅ Receiver settings: Display, WiFi, Web auth
- ✅ Clear delineation in System Settings page

### 2. Ethernet Configuration (Transmitter)
- ✅ DHCP / Static IP selection (radio buttons)
- ✅ Static IP fields (IP, Gateway, Subnet, DNS)
- ✅ Conditional display (only show when Static selected)

### 3. Static vs Dynamic Data
- ✅ Static data: Battery info, system info (once on startup)
- ✅ Dynamic data: Battery/charger/inverter status (only when page active)
- ✅ Clear classification in documentation

### 4. On-Demand Data Flow
- ✅ Subscribe/unsubscribe messages defined
- ✅ Page lifecycle documented
- ✅ No data sent when page not active
- ✅ SSE updates only for subscribed pages

### 5. Missing Elements Identified

#### A. Transmitter Subscription Manager
**New Component Needed**: `ESPnowtransmitter2/src/espnow/subscription_manager.cpp`

```cpp
class SubscriptionManager {
public:
  void subscribe(uint8_t page_id, const uint8_t* receiver_mac);
  void unsubscribe(uint8_t page_id, const uint8_t* receiver_mac);
  bool is_subscribed(uint8_t page_id);
  void check_timeouts();  // Unsubscribe if no activity for 60s
  
private:
  struct Subscription {
    uint8_t page_id;
    uint8_t receiver_mac[6];
    uint32_t last_activity_ms;
  };
  std::vector<Subscription> subscriptions;
};
```

**Usage in data sender task**:
```cpp
// CRITICAL: This task runs at Priority 1 (LOW) on Core 1
// It MUST NOT interfere with control loop (Priority 4, Core 0)
void espnow_data_sender_task(void* parameter) {
  while (true) {
    // Only send if receiver is subscribed to Monitor page
    if (subscription_manager.is_subscribed(PAGE_MONITOR)) {
      send_battery_status();        // Non-blocking, queues message
      send_charger_status();         // Non-blocking, queues message
      send_system_status();          // Non-blocking, queues message
    }
    
    // Only send if receiver is subscribed to Cells page
    if (subscription_manager.is_subscribed(PAGE_CELLS)) {
      send_cell_voltages_chunk(cell_chunk);  // Non-blocking
    }
    
    // Check for subscription timeouts
    subscription_manager.check_timeouts();
    
    vTaskDelay(pdMS_TO_TICKS(200));  // Can slip to 1000ms if needed
  }
}

// Task creation (in main.cpp):
// xTaskCreatePinnedToCore(
//   espnow_data_sender_task,
//   "espnow_sender",
//   4096,
//   NULL,
//   1,          // Priority 1 (LOW) - never preempts control loop
//   NULL,
//   1           // Core 1 - separate from control loop on Core 0
// );
```

#### B. Receiver Page Lifecycle Handlers
**New Component Needed**: `espnowreciever_2/lib/webserver/api/subscription_api.cpp`

```cpp
// API endpoint: /api/subscribe?page=monitor
static esp_err_t api_subscribe_handler(httpd_req_t *req) {
  char query[64];
  httpd_req_get_url_query_str(req, query, sizeof(query));
  
  char page_str[16];
  httpd_query_key_value(query, "page", page_str, sizeof(page_str));
  
  uint8_t page_id = get_page_id_from_string(page_str);
  
  // Send subscribe message to transmitter
  subscribe_page_msg_t msg;
  msg.type = msg_subscribe_page;
  msg.page_id = page_id;
  msg.checksum = calculate_checksum(&msg, sizeof(msg) - 2);
  
  esp_now_send(transmitter_mac, (uint8_t*)&msg, sizeof(msg));
  
  httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// API endpoint: /api/unsubscribe?page=monitor
static esp_err_t api_unsubscribe_handler(httpd_req_t *req) {
  // Similar to subscribe, but sends msg_unsubscribe_page
}
```

**JavaScript on page load/unload**:
```javascript
// In monitor_page.cpp JavaScript
window.addEventListener('load', function() {
  fetch('/api/subscribe?page=monitor');
  
  // Start SSE
  var eventSource = new EventSource('/events');
  eventSource.onmessage = function(e) {
    var data = JSON.parse(e.data);
    updateMonitorPage(data);
  };
});

window.addEventListener('beforeunload', function() {
  fetch('/api/unsubscribe?page=monitor');
});

// Also unsubscribe on timeout (60 seconds of no updates)
var inactivityTimer;
function resetInactivityTimer() {
  clearTimeout(inactivityTimer);
  inactivityTimer = setTimeout(function() {
    fetch('/api/unsubscribe?page=monitor');
  }, 60000);  // 60 seconds
}

// Reset timer on any user activity
document.addEventListener('mousemove', resetInactivityTimer);
document.addEventListener('keypress', resetInactivityTimer);
```

#### C. Static Data Collection on First Connection
**New Component Needed**: `ESPnowtransmitter2/src/espnow/static_data.cpp`

```cpp
void send_all_static_data(const uint8_t* receiver_mac) {
  // Send battery info (once)
  battery_info_msg_t battery_info;
  battery_info.type = msg_battery_info;
  battery_info.total_capacity_Wh = datalayer.battery.info.total_capacity_Wh;
  battery_info.chemistry = datalayer.battery.info.chemistry;
  battery_info.number_of_cells = datalayer.battery.info.number_of_cells;
  // ... populate all fields
  esp_now_send(receiver_mac, (uint8_t*)&battery_info, sizeof(battery_info));
  
  vTaskDelay(pdMS_TO_TICKS(50));  // Spacing between messages
  
  // Send system info (once)
  system_info_msg_t system_info;
  system_info.type = msg_system_info;
  strcpy(system_info.battery_protocol, datalayer.system.info.battery_protocol);
  strcpy(system_info.inverter_brand, datalayer.system.info.inverter_brand);
  strcpy(system_info.firmware_version, FW_VERSION);
  // ... populate all fields
  esp_now_send(receiver_mac, (uint8_t*)&system_info, sizeof(system_info));
  
  // Send all settings (once)
  send_battery_settings(receiver_mac);
  vTaskDelay(pdMS_TO_TICKS(50));
  send_charger_settings(receiver_mac);
  vTaskDelay(pdMS_TO_TICKS(50));
  send_inverter_settings(receiver_mac);
  vTaskDelay(pdMS_TO_TICKS(50));
  send_system_settings(receiver_mac);
}

// Called when receiver first connects (in discovery handler)
// TIMING: Runs in ESP-NOW receive callback (separate task from control loop)
void handle_discovery_message(const espnow_queue_msg_t* msg) {
  // ... existing discovery logic
  
  // Send all static data to new receiver
  // This runs on ESP-NOW task (Priority 2), does NOT block control loop
  send_all_static_data(msg->mac);
  
  LOG_INFO("Static data sent to receiver");
}
```

**Receiver storage**:
```cpp
// espnowreciever_2/src/static_data_storage.cpp
struct StaticData {
  // Battery info
  uint32_t battery_capacity_Wh;
  uint8_t battery_chemistry;
  uint8_t battery_cell_count;
  uint16_t battery_max_voltage_dV;
  uint16_t battery_min_voltage_dV;
  
  // System info
  char firmware_version[32];
  char battery_protocol[64];
  char inverter_brand[16];
  uint32_t device_uptime_s;
  
  // Flags
  bool static_data_received;
  uint32_t static_data_timestamp;
} g_static_data;

void handle_battery_info(const battery_info_msg_t* msg) {
  g_static_data.battery_capacity_Wh = msg->total_capacity_Wh;
  g_static_data.battery_chemistry = msg->chemistry;
  g_static_data.battery_cell_count = msg->number_of_cells;
  // ... store all fields
  
  LOG_INFO("Static battery info received");
}

void handle_system_info(const system_info_msg_t* msg) {
  strcpy(g_static_data.firmware_version, msg->firmware_version);
  strcpy(g_static_data.battery_protocol, msg->battery_protocol);
  // ... store all fields
  
  g_static_data.static_data_received = true;
  g_static_data.static_data_timestamp = millis();
  
  LOG_INFO("Static system info received - all static data complete");
}
```

#### D. Ethernet Static/DHCP Selection UI
**New Component**: System Settings page enhancement

```html
<!-- In settings_page.cpp -->
<div class="settings-section">
  <h3>Transmitter Network Settings (ESP32-POE-ISO)</h3>
  
  <div class="setting-row">
    <label>Network Configuration:</label>
    <input type="radio" id="dhcp" name="net_mode" value="dhcp" onchange="toggleStaticIP()" checked>
    <label for="dhcp">DHCP (Automatic)</label>
    <input type="radio" id="static" name="net_mode" value="static" onchange="toggleStaticIP()">
    <label for="static">Static IP</label>
  </div>
  
  <div id="static-ip-fields" style="display:none;">
    <div class="setting-row">
      <label>IP Address:</label>
      <input type="number" id="ip1" min="0" max="255" value="192">
      <input type="number" id="ip2" min="0" max="255" value="168">
      <input type="number" id="ip3" min="0" max="255" value="1">
      <input type="number" id="ip4" min="0" max="255" value="100">
    </div>
    
    <div class="setting-row">
      <label>Gateway:</label>
      <input type="number" id="gw1" min="0" max="255" value="192">
      <input type="number" id="gw2" min="0" max="255" value="168">
      <input type="number" id="gw3" min="0" max="255" value="1">
      <input type="number" id="gw4" min="0" max="255" value="1">
    </div>
    
    <div class="setting-row">
      <label>Subnet:</label>
      <input type="number" id="sn1" min="0" max="255" value="255">
      <input type="number" id="sn2" min="0" max="255" value="255">
      <input type="number" id="sn3" min="0" max="255" value="255">
      <input type="number" id="sn4" min="0" max="255" value="0">
    </div>
    
    <div class="setting-row">
      <label>DNS:</label>
      <input type="number" id="dns1" min="0" max="255" value="8">
      <input type="number" id="dns2" min="0" max="255" value="8">
      <input type="number" id="dns3" min="0" max="255" value="8">
      <input type="number" id="dns4" min="0" max="255" value="8">
    </div>
  </div>
</div>

<script>
function toggleStaticIP() {
  var staticFields = document.getElementById('static-ip-fields');
  var staticRadio = document.getElementById('static');
  staticFields.style.display = staticRadio.checked ? 'block' : 'none';
}
</script>
```

---

## Updated Testing Checklist (Phase 3)

- [ ] All new pages render correctly on desktop browser
- [ ] All new pages render correctly on mobile browser
- [ ] Navigation works between all pages
- [ ] **Subscribe message sent when page opens**
- [ ] **Unsubscribe message sent when page closes**
- [ ] **Dynamic data only sent for subscribed pages**
- [ ] **Static data collected once on first connection**
- [ ] **Ethernet DHCP/Static selection works**
- [ ] **Static IP fields only shown when Static selected**
- [ ] **Transmitter settings clearly separated from receiver settings**
- [ ] Save buttons trigger ESP-NOW messages
- [ ] Loading spinners show while waiting for data
- [ ] Placeholder text shown for unavailable data
- [ ] SSE updates work for real-time data (only when subscribed)
- [ ] Forms validate input (range checks, required fields)
- [ ] Error messages display when save fails
- [ ] Success messages display when save succeeds
- [ ] Page reload preserves settings
- [ ] Graceful degradation when transmitter disconnected
- [ ] **Subscription timeout after 60 seconds of inactivity**
- [ ] **No ESP-NOW traffic when no pages are open**

---

## Critical Timing Requirements

**MAIN CONTROL LOOP PROTECTION**:

The main battery control loop runs at **Priority 4 (HIGHEST)** on **Core 0** with a **10ms cycle time**. This loop MUST NEVER be delayed or blocked by ESP-NOW communication, MQTT communication, or ANY other operation on the transmitter. **ALL other transmitter operations MUST be lower priority than the main control loop.**

**Task Priority Separation**:

| Task | Priority | Core | Cycle Time | Can Be Delayed? |
|------|----------|------|------------|----------------|
| Battery control loop | 4 (HIGHEST) | 0 | 10ms | ❌ NO - critical |
| CAN bus handling | 3 | 0 | 100ms | ❌ NO - real-time |
| ESP-NOW receive | 2 | 1 | Event-driven | ⚠️ Can queue |
| ESP-NOW send (dynamic data) | 1 (LOW) | 1 | 200ms-1000ms | ✅ YES - not critical |
| MQTT/Ethernet | 0 | 1 | 1000ms | ✅ YES - not critical |

**Design Guarantees**:
1. **ALL transmitter operations** except control loop run at Priority 3 or lower - can NEVER preempt control loop (Priority 4)
2. **Dynamic data sending** (Priority 1, Core 1) and **MQTT** (Priority 0, Core 1) are lowest priority tasks
3. ESP-NOW and MQTT use **non-blocking queue operations** - if queue is full, message is dropped (not critical)
4. Control loop has **8100μs margin** (81% free time) in worst case
5. All ESP-NOW and MQTT traffic is on **Core 1**, completely separate from control loop on **Core 0**
6. If ESP-NOW or MQTT tasks are delayed, they can slip indefinitely **without any impact** on control loop or system safety

**Settings Write Flow** (Non-Critical Path):
1. User edits setting on web page
2. Receiver sends ESP-NOW message (queued, non-blocking)
3. Transmitter ESP-NOW receive handler (Priority 2) receives message
4. **Transmitter writes to its own NVS** (happens in ESP-NOW task Priority 2, not control loop)
5. Transmitter sends ACK back to receiver
6. **Control loop is NEVER blocked** - all ESP-NOW, MQTT, and NVS operations run at lower priority

**Testing Requirements**:
- ✅ Control loop timing MUST stay under 1900μs even with maximum ESP-NOW + MQTT + Ethernet traffic
- ✅ Zero timing violations over 24-hour test period
- ✅ ESP-NOW, MQTT, and Ethernet tasks can be delayed indefinitely without affecting control loop
- ✅ Settings writes (NVS) happen in separate task (Priority 2), never in control loop
- ✅ **ALL transmitter operations** (ESP-NOW, MQTT, Ethernet, NVS, logging) are lower priority than control loop

---

## Notes

- **Receiver handles ALL web UI** - transmitter has no webserver
- **ESP-NOW is communication layer** - replaces HTTP requests to Battery Emulator
- **Settings are bidirectional** - receiver can READ transmitter settings, WRITE commands sent via ESP-NOW, **transmitter writes to its own NVS**
- **Real-time updates via SSE** - no polling required
- **Mobile-first design** - responsive layout for all pages
- **Graceful degradation** - show placeholders when data unavailable
- **Dummy data first** - test UI before real hardware integration
- **Control loop protection** - ALL transmitter operations (ESP-NOW, MQTT, Ethernet, NVS, logging) run at lower priority than control loop (Priority 4, Core 0), ensuring ZERO interference with battery control

---

**Status**: ✓ COMPLETE  
**Next**: Create DATA_LAYER_MAPPING.md
