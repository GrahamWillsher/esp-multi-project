# Battery Emulator Data Layer to ESP-NOW Mapping

**Created**: February 6, 2026  
**Phase**: 0 (Pre-Migration Setup)  
**Purpose**: Map Battery Emulator datalayer structures to ESP-NOW message protocol

---

## Source: Battery Emulator Datalayer

From `Battery-Emulator-9.2.4/Software/src/datalayer/datalayer.h`:

```cpp
class DataLayer {
 public:
  DATALAYER_BATTERY_TYPE battery;
  DATALAYER_BATTERY_TYPE battery2;     // Optional second battery
  DATALAYER_SHUNT_TYPE shunt;
  DATALAYER_CHARGER_TYPE charger;
  DATALAYER_SYSTEM_TYPE system;
};
```

---

## 1. Battery Data Mapping

### 1.1 Battery Info (Static/Semi-Static Data)

**Source**: `DATALAYER_BATTERY_INFO_TYPE`

| Field | Type | Description | Units | ESP-NOW Frequency |
|-------|------|-------------|-------|------------------|
| total_capacity_Wh | uint32_t | Total energy capacity | Wh | Once on connect, on settings change |
| reported_total_capacity_Wh | uint32_t | Capacity reported to inverter | Wh | On settings change |
| max_design_voltage_dV | uint16_t | Maximum pack voltage | dV (0.1V) | Once on connect |
| min_design_voltage_dV | uint16_t | Minimum pack voltage | dV | Once on connect |
| max_cell_voltage_mV | uint16_t | Max cell voltage limit | mV | On settings change |
| min_cell_voltage_mV | uint16_t | Min cell voltage limit | mV | On settings change |
| max_cell_voltage_deviation_mV | uint16_t | Max allowed cell deviation | mV | On settings change |
| number_of_cells | uint8_t | Total cells in pack | count | Once on connect |
| chemistry | enum | NCA/NMC/LFP/LTO | enum | On settings change |

**ESP-NOW Message**: `msg_battery_info`

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_battery_info
  uint32_t total_capacity_Wh;
  uint32_t reported_total_capacity_Wh;
  uint16_t max_design_voltage_dV;
  uint16_t min_design_voltage_dV;
  uint16_t max_cell_voltage_mV;
  uint16_t min_cell_voltage_mV;
  uint16_t max_cell_voltage_deviation_mV;
  uint8_t number_of_cells;
  uint8_t chemistry;                      // battery_chemistry_enum
  uint16_t checksum;
} battery_info_msg_t;  // 24 bytes
```

---

### 1.2 Battery Status (High-Frequency Real-Time Data)

**Source**: `DATALAYER_BATTERY_STATUS_TYPE`

| Field | Type | Description | Units | ESP-NOW Frequency |
|-------|------|-------------|-------|------------------|
| voltage_dV | uint16_t | Pack voltage | dV (0.1V) | 200ms |
| current_dA | int16_t | Pack current | dA (0.1A) | 200ms |
| active_power_W | int32_t | Instantaneous power | W | 200ms |
| real_soc | uint16_t | Real SOC | 0.01% | 200ms |
| reported_soc | uint16_t | SOC to inverter | 0.01% | 200ms |
| temperature_max_dC | int16_t | Max pack temp | dC (0.1°C) | 200ms |
| temperature_min_dC | int16_t | Min pack temp | dC (0.1°C) | 200ms |
| max_charge_power_W | uint32_t | Max allowed charge | W | 1000ms |
| max_discharge_power_W | uint32_t | Max allowed discharge | W | 1000ms |
| max_charge_current_dA | uint16_t | Max charge current | dA | 1000ms |
| max_discharge_current_dA | uint16_t | Max discharge current | dA | 1000ms |
| cell_max_voltage_mV | uint16_t | Highest cell voltage | mV | 1000ms |
| cell_min_voltage_mV | uint16_t | Lowest cell voltage | mV | 1000ms |
| soh_pptt | uint16_t | State of health | 0.01% | 10000ms (10s) |
| bms_status | enum | ACTIVE/FAULT/etc | enum | 200ms |
| balancing_status | enum | Balancing state | enum | 1000ms |

**ESP-NOW Message**: `msg_battery_status`

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_battery_status
  uint16_t voltage_dV;
  int16_t current_dA;
  int32_t active_power_W;
  uint16_t real_soc;
  uint16_t reported_soc;
  int16_t temperature_max_dC;
  int16_t temperature_min_dC;
  uint32_t max_charge_power_W;
  uint32_t max_discharge_power_W;
  uint16_t max_charge_current_dA;
  uint16_t max_discharge_current_dA;
  uint16_t cell_max_voltage_mV;
  uint16_t cell_min_voltage_mV;
  uint16_t soh_pptt;
  uint8_t bms_status;                     // bms_status_enum
  uint8_t balancing_status;               // balancing_status_enum
  uint16_t checksum;
} battery_status_msg_t;  // 40 bytes
```

**Update Frequency**: 200ms (5 Hz) - sent by low-priority ESP-NOW task

---

### 1.3 Battery Settings (User-Configurable)

**Source**: `DATALAYER_BATTERY_SETTINGS_TYPE`

| Field | Type | Description | Units | Bidirectional |
|-------|------|-------------|-------|---------------|
| max_percentage | uint16_t | SOC scaling max | 0.01% | Yes - write from receiver |
| min_percentage | int16_t | SOC scaling min | 0.01% | Yes - write from receiver |
| max_user_set_charge_dA | uint16_t | Max charge current | dA | Yes |
| max_user_set_discharge_dA | uint16_t | Max discharge current | dA | Yes |
| max_user_set_charge_voltage_dV | uint16_t | Max charge voltage | dV | Yes |
| max_user_set_discharge_voltage_dV | uint16_t | Min discharge voltage | dV | Yes |
| soc_scaling_active | bool | Enable SOC scaling | bool | Yes |
| user_set_voltage_limits_active | bool | Use custom voltage limits | bool | Yes |

**ESP-NOW Message (READ)**: `msg_battery_settings`

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_battery_settings
  uint16_t max_percentage;
  int16_t min_percentage;
  uint16_t max_user_set_charge_dA;
  uint16_t max_user_set_discharge_dA;
  uint16_t max_user_set_charge_voltage_dV;
  uint16_t max_user_set_discharge_voltage_dV;
  uint8_t soc_scaling_active;            // bool as uint8_t
  uint8_t user_set_voltage_limits_active; // bool as uint8_t
  uint16_t checksum;
} battery_settings_msg_t;  // 18 bytes
```

**ESP-NOW Message (WRITE)**: `msg_battery_settings_update`

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_battery_settings_update
  uint8_t setting_id;                    // Which setting to update
  uint32_t value;                        // New value
  uint16_t checksum;
} battery_settings_update_msg_t;  // 8 bytes
```

**Settings ID Enum**:
```cpp
enum BatterySettingID {
  SETTING_MAX_PERCENTAGE = 0,
  SETTING_MIN_PERCENTAGE = 1,
  SETTING_MAX_CHARGE_CURRENT = 2,
  SETTING_MAX_DISCHARGE_CURRENT = 3,
  SETTING_MAX_CHARGE_VOLTAGE = 4,
  SETTING_MIN_DISCHARGE_VOLTAGE = 5,
  SETTING_SOC_SCALING_ACTIVE = 6,
  SETTING_VOLTAGE_LIMITS_ACTIVE = 7,
  SETTING_CHEMISTRY = 8,
  SETTING_CAPACITY_WH = 9
};
```

---

### 1.4 Cell Voltages (Low-Frequency Large Data)

**Source**: `battery.status.cell_voltages_mV[MAX_AMOUNT_CELLS]`

| Field | Type | Description | Max Size | ESP-NOW Strategy |
|-------|------|-------------|----------|-----------------|
| cell_voltages_mV | uint16_t[] | All cell voltages | 200 cells × 2 bytes = 400 bytes | Chunked transfer |
| cell_balancing_status | bool[] | Balancing status | 200 cells / 8 = 25 bytes | Chunked transfer |

**ESP-NOW Message**: `msg_cell_voltages` (chunked)

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_cell_voltages
  uint8_t chunk_index;                   // 0-based chunk number
  uint8_t total_chunks;                  // Total chunks for this transfer
  uint8_t cells_in_chunk;                // How many cells in this chunk
  uint16_t cell_voltages_mV[20];         // Max 20 cells per chunk
  uint8_t balancing_status[3];           // Bitfield for 20 cells (20 bits = 3 bytes)
  uint16_t checksum;
} cell_voltages_msg_t;  // 49 bytes per chunk
```

**Strategy**: 
- Up to 200 cells → 10 chunks of 20 cells each
- Send at 1 Hz (one complete set every second)
- Receiver reassembles chunks into full array

**Update Frequency**: 1 Hz (complete set every second)

---

## 2. Charger Data Mapping

### 2.1 Charger Status

**Source**: `DATALAYER_CHARGER_TYPE`

| Field | Type | Description | Units | ESP-NOW Frequency |
|-------|------|-------------|-------|------------------|
| charger_setpoint_HV_VDC | float | Target HV voltage | V | 1000ms |
| charger_setpoint_HV_IDC | float | Target HV current | A | 1000ms |
| charger_setpoint_HV_IDC_END | float | End-of-charge current | A | 1000ms |
| charger_stat_HVcur | float | Measured HV current | A | 200ms |
| charger_stat_HVvol | float | Measured HV voltage | V | 200ms |
| charger_stat_ACcur | float | Measured AC current | A | 200ms |
| charger_stat_ACvol | float | Measured AC voltage | V | 200ms |
| charger_stat_LVcur | float | Measured LV current | A | 200ms |
| charger_stat_LVvol | float | Measured LV voltage | V | 200ms |
| charger_HV_enabled | bool | HV output enabled | bool | 200ms |
| charger_aux12V_enabled | bool | 12V output enabled | bool | 200ms |

**ESP-NOW Message**: `msg_charger_status`

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_charger_status
  uint16_t setpoint_HV_voltage_dV;       // float → uint16_t (dV)
  uint16_t setpoint_HV_current_dA;       // float → uint16_t (dA)
  uint16_t setpoint_HV_current_end_dA;   
  uint16_t measured_HV_current_dA;
  uint16_t measured_HV_voltage_dV;
  uint16_t measured_AC_current_dA;
  uint16_t measured_AC_voltage_dV;
  uint16_t measured_LV_current_dA;
  uint16_t measured_LV_voltage_dV;
  uint8_t hv_enabled;                    // bool
  uint8_t aux12v_enabled;                // bool
  uint16_t checksum;
} charger_status_msg_t;  // 24 bytes
```

**Update Frequency**: 200ms (5 Hz)

---

## 3. System Data Mapping

### 3.1 System Status

**Source**: `DATALAYER_SYSTEM_STATUS_TYPE` + `DATALAYER_SYSTEM_INFO_TYPE`

| Field | Type | Description | ESP-NOW Frequency |
|-------|------|-------------|------------------|
| contactors_engaged | uint8_t | 0=open, 1=closed, 2=fault | 200ms |
| precharge_status | enum | Precharge state | 200ms |
| bms_reset_status | enum | BMS reset state | 1000ms |
| battery_allows_contactor_closing | bool | Battery permits close | 200ms |
| inverter_allows_contactor_closing | bool | Inverter permits close | 200ms |
| CPU_temperature | float | ESP32 temp | 5000ms |
| equipment_stop_active | bool | E-stop active | 200ms |
| start_precharging | bool | Precharge in progress | 200ms |

**ESP-NOW Message**: `msg_system_status`

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_system_status
  uint8_t contactors_engaged;            // 0/1/2
  uint8_t precharge_status;              // PrechargeState enum
  uint8_t bms_reset_status;              // BMSResetState enum
  uint8_t battery_allows_contactor_closing;
  uint8_t inverter_allows_contactor_closing;
  int16_t cpu_temperature_dC;            // float → int16_t (dC)
  uint8_t equipment_stop_active;
  uint8_t start_precharging;
  uint16_t checksum;
} system_status_msg_t;  // 13 bytes
```

**Update Frequency**: 200ms (5 Hz)

---

### 3.2 System Info

| Field | Type | Description | ESP-NOW Frequency |
|-------|------|-------------|------------------|
| battery_protocol | char[64] | Battery type string | Once on connect |
| inverter_brand | char[8] | Inverter brand | Once on connect |

**ESP-NOW Message**: `msg_system_info`

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_system_info
  char battery_protocol[64];
  char inverter_brand[8];
  uint16_t checksum;
} system_info_msg_t;  // 75 bytes
```

**Update Frequency**: Once on connection, or on request

---

## 4. Page Subscription Messages (On-Demand Data Flow)

**Purpose**: Enable on-demand data flow - transmitter only sends dynamic data when receiver has a page open

### 4.1 Subscribe to Page

**Message Type**: `msg_subscribe_page` (0x0A)

**Sent by**: Receiver

**When**: User opens a webpage requiring dynamic data (Monitor, Cells, Events)

**Structure**:

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;          // msg_subscribe_page (0x0A)
  uint8_t page_id;       // Which page to subscribe to
  uint16_t checksum;
} subscribe_page_msg_t;  // Total: 4 bytes
```

**Page IDs**:

```cpp
enum PageSubscription {
  PAGE_MONITOR = 0,           // Main status page (battery/charger/system status)
  PAGE_CELLS = 1,             // Cell voltage monitor
  PAGE_EVENTS = 2,            // Event log viewer
  PAGE_BATTERY_SETTINGS = 3,  // Battery settings (static data, one-time fetch)
  PAGE_CHARGER_SETTINGS = 4,  // Charger settings (static data, one-time fetch)
  PAGE_INVERTER_SETTINGS = 5, // Inverter settings (static data, one-time fetch)
  PAGE_SYSTEM_SETTINGS = 6    // System settings (static data, one-time fetch)
};
```

**Transmitter Response**:
1. Add page to subscription list
2. Start sending relevant data for that page:
   - `PAGE_MONITOR` → send battery_status, charger_status, system_status every 200ms
   - `PAGE_CELLS` → send cell_voltages chunks every 1000ms
   - `PAGE_EVENTS` → send event_history every 5000ms
   - Settings pages → send corresponding settings message ONCE

---

### 4.2 Unsubscribe from Page

**Message Type**: `msg_unsubscribe_page` (0x0B)

**Sent by**: Receiver

**When**: User closes webpage, navigates away, or after 60 seconds of inactivity

**Structure**:

```cpp
typedef struct __attribute__((packed)) {
  uint8_t type;          // msg_unsubscribe_page (0x0B)
  uint8_t page_id;       // Which page to unsubscribe from
  uint16_t checksum;
} unsubscribe_page_msg_t;  // Total: 4 bytes
```

**Transmitter Response**:
1. Remove page from subscription list
2. Stop sending data for that page
3. If no pages subscribed, send NO dynamic data (only respond to commands)

---

### 4.3 Subscription Manager (Transmitter)

**Implementation**:

```cpp
class SubscriptionManager {
private:
  struct Subscription {
    uint8_t page_id;
    uint8_t receiver_mac[6];
    uint32_t last_activity_ms;  // For timeout detection
  };
  std::vector<Subscription> subscriptions;
  
public:
  void subscribe(uint8_t page_id, const uint8_t* receiver_mac) {
    // Add or update subscription
    for (auto& sub : subscriptions) {
      if (sub.page_id == page_id && 
          memcmp(sub.receiver_mac, receiver_mac, 6) == 0) {
        sub.last_activity_ms = millis();  // Refresh timeout
        return;
      }
    }
    // New subscription
    Subscription new_sub;
    new_sub.page_id = page_id;
    memcpy(new_sub.receiver_mac, receiver_mac, 6);
    new_sub.last_activity_ms = millis();
    subscriptions.push_back(new_sub);
    
    LOG_INFO("Page %d subscribed", page_id);
  }
  
  void unsubscribe(uint8_t page_id, const uint8_t* receiver_mac) {
    subscriptions.erase(
      std::remove_if(subscriptions.begin(), subscriptions.end(),
        [&](const Subscription& sub) {
          return sub.page_id == page_id && 
                 memcmp(sub.receiver_mac, receiver_mac, 6) == 0;
        }),
      subscriptions.end()
    );
    
    LOG_INFO("Page %d unsubscribed", page_id);
  }
  
  bool is_subscribed(uint8_t page_id) {
    return std::any_of(subscriptions.begin(), subscriptions.end(),
      [page_id](const Subscription& sub) { return sub.page_id == page_id; });
  }
  
  void check_timeouts() {
    uint32_t now = millis();
    const uint32_t TIMEOUT_MS = 60000;  // 60 seconds
    
    subscriptions.erase(
      std::remove_if(subscriptions.begin(), subscriptions.end(),
        [now, TIMEOUT_MS](const Subscription& sub) {
          if (now - sub.last_activity_ms > TIMEOUT_MS) {
            LOG_WARN("Page %d subscription timed out", sub.page_id);
            return true;
          }
          return false;
        }),
      subscriptions.end()
    );
  }
};
```

**Usage in ESP-NOW Data Sender Task** (Priority 1, Core 1):

```cpp
void espnow_data_sender_task(void* parameter) {
  SubscriptionManager sub_mgr;
  
  while (true) {
    // Only send if pages are subscribed
    if (sub_mgr.is_subscribed(PAGE_MONITOR)) {
      send_battery_status();   // 200ms
      send_charger_status();   // 200ms
      send_system_status();    // 200ms
    }
    
    if (sub_mgr.is_subscribed(PAGE_CELLS)) {
      send_cell_voltages_chunk(chunk_index);  // 1000ms
    }
    
    if (sub_mgr.is_subscribed(PAGE_EVENTS)) {
      send_event_history();  // 5000ms
    }
    
    // Check for subscription timeouts
    sub_mgr.check_timeouts();
    
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
```

**Key Principle**: **If no one is looking at a webpage, no dynamic data is sent** → Saves bandwidth and CPU cycles

---

## 5. Event Log Mapping

**Source**: Events system (`Software/src/devboard/utils/events.h`)

**ESP-NOW Message**: `msg_event_history`

```cpp
typedef struct __attribute__((packed)) {
  uint32_t timestamp;                    // Unix timestamp
  uint8_t severity;                      // 0=info, 1=warning, 2=error
  char message[64];                      // Event description
} event_entry_t;  // 69 bytes

typedef struct __attribute__((packed)) {
  uint8_t type;                          // msg_event_history
  uint8_t chunk_index;
  uint8_t total_chunks;
  uint8_t events_in_chunk;               // Max 3 events per chunk
  event_entry_t events[3];               // 3 × 69 = 207 bytes
  uint16_t checksum;
} event_history_msg_t;  // 213 bytes (LARGE - may need splitting)
```

**Alternative**: Use request/response pattern
- Receiver sends `msg_event_request` with start_index and count
- Transmitter responds with `msg_event_response` containing requested events

---

## 6. ESP-NOW Bandwidth Analysis

### Message Sizes Summary

| Message | Size | Frequency | Bandwidth (bytes/sec) |
|---------|------|-----------|---------------------|
| battery_status | 40 | 200ms (5 Hz) | 200 |
| charger_status | 24 | 200ms (5 Hz) | 120 |
| system_status | 13 | 200ms (5 Hz) | 65 |
| cell_voltages (chunked) | 49 × 10 | 1 Hz | 490 |
| battery_settings | 18 | On change | ~1 |
| battery_info | 24 | On connect | ~0 |
| system_info | 75 | On connect | ~0 |

**Total Continuous Bandwidth**: ~876 bytes/sec

**ESP-NOW Capacity**: 
- Max throughput: ~250 kbps = ~31,250 bytes/sec
- **Utilization**: 876 / 31,250 = **2.8%** ← Plenty of headroom!

---

## 7. Transmitter Implementation Notes

### 6.1 Data Sender Task (Low Priority)

```cpp
void espnow_data_sender_task(void* parameter) {
  const TickType_t interval = pdMS_TO_TICKS(200); // 200ms base interval
  TickType_t last_wake = xTaskGetTickCount();
  static uint8_t cycle = 0;
  
  while (true) {
    if (receiver_connected) {
      // Every cycle (200ms):
      send_battery_status();      // 40 bytes
      vTaskDelay(pdMS_TO_TICKS(20));
      
      send_charger_status();      // 24 bytes
      vTaskDelay(pdMS_TO_TICKS(20));
      
      send_system_status();       // 13 bytes
      vTaskDelay(pdMS_TO_TICKS(20));
      
      // Every 5 cycles (1000ms):
      if (cycle % 5 == 0) {
        send_battery_settings();   // 18 bytes
      }
      
      // Staggered cell voltages (one chunk per cycle)
      static uint8_t cell_chunk = 0;
      send_cell_voltages_chunk(cell_chunk);
      cell_chunk = (cell_chunk + 1) % 10;  // 10 chunks total
      
      cycle++;
    }
    
    vTaskDelayUntil(&last_wake, interval);
  }
}
```

### 7.2 Control Loop (Does NOT send ESP-NOW)

```cpp
void battery_control_loop(void* parameter) {
  // ONLY updates datalayer - NO ESP-NOW sends!
  
  while (true) {
    // Read CAN/sensors → Update datalayer
    update_battery_status();
    update_charger_status();
    update_system_status();
    
    // Run control logic
    update_contactors();
    check_safety_limits();
    
    // NO ESP-NOW here - separate task handles it!
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
  }
}
```

---

## 8. Receiver Implementation Notes

### 7.1 Message Handlers

Each ESP-NOW message type needs a handler in `espnowreciever_2/src/espnow/`:

- `battery_handlers.cpp`: handle_battery_status(), handle_battery_info(), handle_battery_settings()
- `charger_handlers.cpp`: handle_charger_status()
- `system_handlers.cpp`: handle_system_status(), handle_system_info()
- `cell_handlers.cpp`: handle_cell_voltages_chunk()
- `event_handlers.cpp`: handle_event_history()

### 7.2 Data Storage

Global state in `espnowreciever_2/src/globals.cpp`:

```cpp
// Battery data
struct BatteryData {
  uint16_t voltage_dV;
  int16_t current_dA;
  uint16_t soc_pptt;
  // ... all fields from battery_status_msg_t
} g_battery_data;

// Cell voltages (reassembled from chunks)
uint16_t g_cell_voltages[200];
uint8_t g_cell_count;
bool g_cell_data_complete;

// Charger data
struct ChargerData {
  // ... fields from charger_status_msg_t
} g_charger_data;

// System data
struct SystemData {
  // ... fields from system_status_msg_t
} g_system_data;
```

### 8.3 SSE Notifications

When data updated, notify web clients:

```cpp
void handle_battery_status(const espnow_queue_msg_t* msg) {
  const battery_status_msg_t* data = (battery_status_msg_t*)msg->data;
  
  // Validate checksum
  if (!validate_checksum(data, sizeof(*data))) return;
  
  // Store in globals
  g_battery_data.voltage_dV = data->voltage_dV;
  g_battery_data.current_dA = data->current_dA;
  // ... all fields
  
  // Notify SSE clients
  notify_sse_battery_updated();
}
```

---

## 9. Phase 1 Testing Checklist

- [ ] All message structures pack correctly (no padding issues)
- [ ] Checksums calculate and validate correctly
- [ ] Battery status sent every 200ms from transmitter
- [ ] Receiver logs all received messages
- [ ] Web page displays battery voltage/current/SOC
- [ ] Cell voltage chunks reassemble correctly (all 200 cells)
- [ ] Settings update (write) works bidirectionally
- [ ] ACK received after settings save
- [ ] No memory leaks over 1 hour
- [ ] ESP-NOW task does NOT affect control loop timing

---

**Status**: ✓ COMPLETE  
**Next**: Create SETTINGS_MAPPING.md
