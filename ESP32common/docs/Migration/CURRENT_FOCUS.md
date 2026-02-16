# Stage 1 Implementation: Current Focus (Updated with MQTT Details)

## What Changed Today

### 1. Architecture Clarification ✓
- **Transmitter (Olimex ESP32-POE2 + Waveshare RS485/CAN HAT B)**: Battery/inverter control via CAN, operates independently
- **Receiver (LilyGo T-Display-S3)**: UI only, can be offline without impact
- **Hardware Details**: GPIO and board settings in [HARDWARE_HAL.md](HARDWARE_HAL.md) and [HARDWARE_CONFIG_SUMMARY.md](HARDWARE_CONFIG_SUMMARY.md)
- **Key insight**: Monitoring is secondary; control is primary
- **Data rates**: Low (1–5s summaries, on-request for details)

### 2. Cell Data Viability Analysis ✓
- **96 cells × 2 bytes = 192 bytes** (fits in 250-byte ESP-NOW limit)
- **Single battery**: Cell data viable via ESP-NOW if <1 Hz
- **Dual battery (384 bytes)**: Must use MQTT (exceeds ESP-NOW limit)
- **Recommendation**: Cell data on-request only; store via MQTT for persistence

### 3. Transport Strategy ✓
| Data Type | ESP-NOW | MQTT | Rationale |
|-----------|---------|------|-----------|
| System/Battery/Inverter status | ✓ | ✓ | Low-rate ESP-NOW for UI; optional MQTT for history |
| Settings | ✓ | — | Interactive feedback via ESP-NOW |
| Cell data | On-request only | ✓ | Store via MQTT (durable) |
| Events | ✓ (immediate) | ✓ (durable) | ESP-NOW alert + MQTT log |

### 4. Actual MQTT Topics from Battery Emulator v9.2.4 ✓

**Primary Topics** (Receiver subscribes to):
- `BE/info` - Battery status + system status (5s interval)
- `BE/spec_data` - Cell voltages battery 1 (on-demand)
- `BE/spec_data_2` - Cell voltages battery 2 (on-demand, dual-battery only)
- `BE/status` - Availability/LWT (online/offline)

**JSON Schemas** documented in [MQTT_TOPICS_REFERENCE.md](MQTT_TOPICS_REFERENCE.md)

### 5. Receiver MQTT Configuration ✓
**New NVS Keys**:
```
mqtt_en        - Enable MQTT (bool)
mqtt_broker    - Broker IP:port (string)
mqtt_user      - Username if required (string)
mqtt_pass      - Password if required (string)
mqtt_topic     - Topic prefix, default "BE" (string)
```

**Web UI Settings Page** - Add MQTT configuration section with:
- Broker IP/port input
- Username/password (optional)
- Topic prefix (default "BE")
- Connection status indicator
- Test button

### 6. Receiver MQTT Implementation ✓
**New Library**: Create `espnowreciever_2/lib/mqtt_client/`
- `mqtt_client.h/cpp` - MQTT client wrapper using esp_mqtt_client
- `mqtt_handlers.h/cpp` - JSON parsing + cache update handlers
- Subscribe to: `BE/info`, `BE/spec_data`, `BE/spec_data_2`, `BE/status`

**Data Flow**:
```
Transmitter → MQTT Broker → Receiver MQTT Client → TransmitterManager Cache → Web UI (SSE)
```

### 7. Cell Monitor Layout from Battery Emulator ✓
**Based on**: `Software/src/devboard/webserver/cellmonitor_html.cpp`

**Features**:
- Grid layout: 96 cells displayed in 2 columns (48% width each)
- Bar graph visualization: Cells normalized to voltage range
- Color coding:
  - Blue = idle/normal
  - Red = min/max voltage
  - Cyan = active balancing
  - Red text = low voltage
- Stats section: Max/min/delta voltages, active balancing cells
- Responsive: Flexbox layout, works on mobile/tablet
- Legend: Color meanings for voltage status

### 8. Documentation Created ✓
**Files**:
1. **[STAGE_1_DATA_LAYER_EXPANDED.md](STAGE_1_DATA_LAYER_EXPANDED.md)** (Comprehensive)
   - Complete architecture overview (dual-board)
   - 7 ESP-NOW packet definitions with struct sizes
   - MQTT configuration + implementation details
   - MQTT message handlers + SSE integration
   - Cell monitor page implementation
   - 6-phase implementation checklist (A–F)
   - Legacy code removal checklist
   - Bandwidth estimates

2. **[MQTT_TOPICS_REFERENCE.md](MQTT_TOPICS_REFERENCE.md)** (Reference)
   - Actual MQTT topics from Battery Emulator v9.2.4
   - Complete JSON payload schemas
   - Topic rate, retention, conditions
   - Receiver cache structure
   - NVS configuration keys
   - Troubleshooting guide
   - Battery Emulator code references

---

## Implementation Breakdown

### Phase A: Packet Definition (ESP-NOW)
Define C struct packets: 50–150 bytes each, all <250 bytes total  
Time: ~2 hours

### Phase B: Transmitter Senders (ESP-NOW)
Implement status senders: 1–5s intervals, on-change logic  
Time: ~4 hours

### Phase C: Receiver Handlers (ESP-NOW)
Parse packets, update cache, wire SSE  
Time: ~3 hours

### Phase D: MQTT + Web UI Integration ⭐ **LARGEST PHASE**
- [ ] MQTT client library (mqtt_client)
- [ ] MQTT handlers (parse JSON, update cache)
- [ ] MQTT settings page in web UI
- [ ] MQTT battery section in monitor page
- [ ] Cell monitor page (grid + bars)
- [ ] SSE push for MQTT updates
- [ ] Legacy code removal
Time: ~8–10 hours (largest effort)

### Phase E: Testing (Smoke Tests)
Verify packets, rates, data flow, SSE updates  
Time: ~2 hours

### Phase F: Documentation
Create PACKET_DEFINITIONS.md, DATA_TRANSFER_DESIGN.md  
Time: ~1 hour

---

## Status

✓ **Architecture** - Finalized (dual-board, separation of concerns)  
✓ **Transport** - Decided (ESP-NOW + MQTT)  
✓ **Data Analysis** - Complete (cell data viable, bandwidth negligible)  
✓ **Packet Definitions** - Designed (7 packets, all <250 bytes)  
✓ **MQTT Topics** - Extracted from Battery Emulator v9.2.4  
✓ **Cell Monitor Layout** - Based on Battery Emulator cellmonitor.cpp  
✓ **Documentation** - Complete (STAGE_1_DATA_LAYER_EXPANDED.md + MQTT_TOPICS_REFERENCE.md)  

⏳ **Packet Structs** - Ready to implement (Phase A)  
⏳ **Transmitter Senders** - Ready to implement (Phase B)  
⏳ **Receiver Handlers (ESP-NOW)** - Ready to implement (Phase C)  
⏳ **MQTT + UI Integration** - Ready to implement (Phase D - largest)  
⏳ **Testing** - Ready to execute (Phase E)  

---

## Key Files to Create/Modify

### Create (New)
- `espnowreciever_2/lib/mqtt_client/mqtt_client.h` - MQTT client
- `espnowreciever_2/lib/mqtt_client/mqtt_client.cpp`
- `espnowreciever_2/lib/mqtt_client/mqtt_handlers.h` - Handlers
- `espnowreciever_2/lib/mqtt_client/mqtt_handlers.cpp`
- `espnowreciever_2/lib/webserver/pages/cells_page.cpp` - Cell monitor (new page)
- `docs/PACKET_DEFINITIONS.md` - Struct reference
- `docs/DATA_TRANSFER_DESIGN.md` - Protocol design

### Modify
- `espnowreciever_2/lib/webserver/pages/monitor_page.cpp` - Add MQTT battery section
- `espnowreciever_2/lib/webserver/pages/settings_page.cpp` - Add MQTT config section
- `espnowreciever_2/lib/webserver/transmitter_manager.cpp` - Add MQTT data handling
- `espnowreciever_2/src/main.cpp` - Initialize MQTT client
- `esnowtransmitter2/src/main.cpp` - Add status senders (Phase B)

### Delete (Legacy Code)
- Any dummy data generators
- Any old MQTT implementations
- Test data seeders
- Placeholder handlers

---

## Critical Design Notes

1. **MQTT is NOT for real-time control** - Only for logging/monitoring
2. **Cell data is LOW-RATE** - Enable only if needed (saves bandwidth)
3. **Dual-battery systems** - Use MQTT for cell data (ESP-NOW packet too large)
4. **Legacy code removal is critical** - Only live data sources (ESP-NOW + MQTT)
5. **Battery Emulator topics are canonical** - Use exact topic names, don't invent new ones

---

## Questions? Context?

- **MQTT Configuration**: See [MQTT_TOPICS_REFERENCE.md](MQTT_TOPICS_REFERENCE.md)
- **Packet Structures**: See [STAGE_1_DATA_LAYER_EXPANDED.md](STAGE_1_DATA_LAYER_EXPANDED.md)
- **Cell Layout**: See Battery Emulator `cellmonitor_html.cpp` or STAGE_1 document
- **Data Rates**: ESP-NOW ~83 bytes/sec, MQTT ~11 bytes/sec (negligible)

Ready to start Phase A (packet structs)? Let me know!
