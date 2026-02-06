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

**ESP-NOW messages**:
- `msg_battery_settings` (READ current settings)
- `msg_battery_settings_update` (WRITE new settings)
- `msg_settings_update_ack` (ACK from transmitter)

**Files to create**:
- `lib/webserver/pages/battery_settings_page.cpp`
- `lib/webserver/pages/battery_settings_page.h`
- Update `lib/webserver/page_definitions.cpp` (register route)

---

#### 3. Charger Settings Page (1 day)
**Target**: `/charger_settings` (NEW)

**Content**:
- Max charge power (W)
- Target charge voltage (V)
- End-of-charge current (A)
- Charger type selection (dropdown: Eltek, Victron, Generic)
- **Save button** → ESP-NOW msg_charger_settings_update

**ESP-NOW messages**:
- `msg_charger_settings`
- `msg_charger_settings_update`
- `msg_settings_update_ack`

**Files to create**:
- `lib/webserver/pages/charger_settings_page.cpp`
- `lib/webserver/pages/charger_settings_page.h`

---

#### 4. Inverter Settings Page (1 day)
**Target**: `/inverter_settings` (NEW)

**Content**:
- Max discharge power (W)
- Inverter type selection (dropdown: SMA, Fronius, SolarEdge, Generic)
- AC voltage setpoint (V)
- Frequency setpoint (Hz)
- **Save button** → ESP-NOW msg_inverter_settings_update

**ESP-NOW messages**:
- `msg_inverter_settings`
- `msg_inverter_settings_update`
- `msg_settings_update_ack`

**Files to create**:
- `lib/webserver/pages/inverter_settings_page.cpp`
- `lib/webserver/pages/inverter_settings_page.h`

---

#### 5. System Settings Page (1 day)
**Target**: `/settings` (enhance existing `settings_page.cpp`)

**Add to existing page**:
- MQTT enabled (checkbox)
- MQTT server (text input)
- MQTT port (number input)
- MQTT username (text input)
- MQTT password (password input)
- SD card logging enabled (checkbox)
- CAN logging enabled (checkbox)
- **Keep existing**: Debug level, system info

**ESP-NOW messages**:
- `msg_system_settings`
- `msg_system_settings_update`
- `msg_settings_update_ack`

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
- Auto-refresh via SSE
- Pagination (50 events per page)

**ESP-NOW messages**:
- `msg_event_history` (array of events)
- `msg_clear_events` (clear event log)

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

## Testing Checklist (Phase 3)

- [ ] All new pages render correctly on desktop browser
- [ ] All new pages render correctly on mobile browser
- [ ] Navigation works between all pages
- [ ] Save buttons trigger ESP-NOW messages
- [ ] Loading spinners show while waiting for data
- [ ] Placeholder text shown for unavailable data
- [ ] SSE updates work for real-time data
- [ ] Forms validate input (range checks, required fields)
- [ ] Error messages display when save fails
- [ ] Success messages display when save succeeds
- [ ] Page reload preserves settings
- [ ] Graceful degradation when transmitter disconnected

---

## Dummy Data Handling (Phase 3)

Since Phase 3 comes BEFORE Phase 4 (core integration), all pages must handle **dummy data** from transmitter's dummy data generator:

1. **Monitor page**: Show dummy battery voltage ~48V, current ±50A, SOC cycling 20-80%
2. **Settings pages**: Load dummy settings, save to dummy NVS (logs only)
3. **Events page**: Show dummy events (startup, warning, error samples)
4. **Cells page**: Show dummy cell voltages 3.2-3.7V with realistic deviation

**Dummy data generator** (from Phase 1) will provide:
- Realistic voltage/current/SOC patterns
- Simulated charge/discharge cycles
- Occasional warning/fault events
- Cell voltage variation

**Goal**: Fully functional web interface ready for real data in Phase 4.

---

## Notes

- **Receiver handles ALL web UI** - transmitter has no webserver
- **ESP-NOW is communication layer** - replaces HTTP requests to Battery Emulator
- **Settings are bidirectional** - receiver can read AND write transmitter settings
- **Real-time updates via SSE** - no polling required
- **Mobile-first design** - responsive layout for all pages
- **Graceful degradation** - show placeholders when data unavailable
- **Dummy data first** - test UI before real hardware integration

---

**Status**: ✓ COMPLETE  
**Next**: Create DATA_LAYER_MAPPING.md
