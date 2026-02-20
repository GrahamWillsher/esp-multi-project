# ESP32 Battery Emulator with ESP-NOW & MQTT - Project Documentation

## 📍 Quick Start

**First time here?** Start with [PROJECT_STATUS_DASHBOARD.md](PROJECT_STATUS_DASHBOARD.md) (5 min read)

**Need specific information?** See [DOCUMENTATION_INDEX.md](DOCUMENTATION_INDEX.md)

**Want full details?** Read [PHASE3_COMPLETE_GUIDE.md](PHASE3_COMPLETE_GUIDE.md)

**Starting Phase 3.1 development?** See [PHASE3_BATTERY_TYPE_SELECTION.md](PHASE3_BATTERY_TYPE_SELECTION.md)

---

## 🎯 Project Overview

This is the **receiver module** for an ESP32 Battery Emulator MQTT integration system.

### Hardware
- **Device**: LilyGo T-Display-S3 (ESP32-S3 with 1.9" display, 16MB PSRAM)
- **Connectivity**: WiFi + ESP-NOW
- **Purpose**: Display battery emulator specs from transmitter via MQTT

### Current Status
- **Phase 3**: 90% Complete ✅
- **Phase 3.1**: Type Selection (planning done, ready for coding)
- **Device**: Running stable, no crashes
- **Tests**: All passing, device verified working

### Key Features (Phase 3)
✅ WiFi network configuration (SSID, password, static IP)
✅ MQTT client (subscribes to battery/inverter/charger specs)
✅ Web dashboard (displays specs from MQTT)
✅ Configuration pages (network, MQTT settings)
✅ REST API (11 endpoints for specs and config)
✅ Safe memory operations (heap overflow fixes in 4 pages)
✅ Persistent storage (NVS for all settings)

---

## 📚 Documentation Structure

```
espnowreciever_2/
├── PROJECT_STATUS_DASHBOARD.md          ← START HERE (status overview)
├── DOCUMENTATION_INDEX.md               ← Navigation guide
├── DOCUMENTATION_UPDATE_SUMMARY.md      ← What was created today
├── PHASE3_PROGRESS.md                   ← High-level status
├── PHASE3_COMPLETE_GUIDE.md             ← Feature reference (50 pages)
├── PHASE3_BATTERY_TYPE_SELECTION.md     ← Phase 3.1 plan (dev ready)
└── [Other phase docs...]
```

### Which Document to Read?

| Question | Read This |
|----------|-----------|
| What's the project status? | PROJECT_STATUS_DASHBOARD.md |
| How do I navigate all docs? | DOCUMENTATION_INDEX.md |
| What was documented today? | DOCUMENTATION_UPDATE_SUMMARY.md |
| What's complete in Phase 3? | PHASE3_PROGRESS.md |
| How do I use the system? | PHASE3_COMPLETE_GUIDE.md |
| How do I code Phase 3.1? | PHASE3_BATTERY_TYPE_SELECTION.md |
| I'm lost, where do I start? | This README |

---

## 🏗️ System Architecture

### Transmitter → MQTT → Receiver

```
Transmitter (ESP32-POE2)
    ↓ (Publishes MQTT)
MQTT Broker (192.168.1.221:1883)
    ↓ (Sends specs)
Receiver (LilyGo T-Display-S3)
    ↓ (Stores in memory)
Web Dashboard (http://192.168.1.230)
    ↓ (User views specs)
LCD Display (1.9" color screen)
```

### MQTT Topics

```
BE/spec_data           ← Battery + Inverter + Charger + System specs
BE/spec_data_2         ← Inverter-specific specs
BE/battery_specs       ← Battery specs (retained message)
```

### Communication Channels

| Channel | Direction | Purpose |
|---------|-----------|---------|
| MQTT | Tx → Rx | Transmit battery specs |
| ESP-NOW | Rx → Tx | Send settings/type selection |
| WiFi | Rx ↔ User | Web interface access |

---

## ⚡ Getting Started

### Prerequisites
- PlatformIO installed
- ESP32 device flashed with latest firmware
- WiFi network available
- MQTT broker running

### Basic Setup

1. **Build the project**
```bash
cd espnowreciever_2
pio run -t upload -t monitor
```

2. **Access web interface**
```
http://192.168.1.230/
```

3. **Configure network** (if not already done)
```
Navigate to http://192.168.1.230/systeminfo.html
Enter WiFi SSID, password, and MQTT broker details
Click Save
Device will reconnect with new settings
```

4. **View battery specs**
```
Click Battery card on dashboard
Should display specs received from MQTT
```

### Troubleshooting

**Can't connect to WiFi?**
- Check SSID and password are correct
- Verify WiFi network is 2.4GHz (5GHz not supported on ESP32)
- See PHASE3_COMPLETE_GUIDE.md → Troubleshooting

**MQTT not connecting?**
- Check broker IP and port are correct
- Verify username and password
- Ensure broker is running and accessible
- See PHASE3_COMPLETE_GUIDE.md → Troubleshooting

**Specs not showing?**
- Verify MQTT subscription in terminal output
- Check transmitter is running and publishing
- Ensure topics match (BE/spec_data, etc.)
- See PHASE3_COMPLETE_GUIDE.md → Troubleshooting

---

## 🔧 Development

### For Understanding Phase 3

1. Read: PHASE3_COMPLETE_GUIDE.md (architecture section)
2. Read: Configuration Examples in same doc
3. Understand: How MQTT specs flow to web pages

### For Implementing Phase 3.1

1. Read: PHASE3_BATTERY_TYPE_SELECTION.md (full document)
2. Understand: 5-layer architecture design
3. Follow: Implementation Tasks (5 tasks, ~70 minutes)
4. Use: Testing Checklist (8 items) to verify

### Code Patterns

**Safe String Building** (used in all spec pages):
```cpp
// Calculate total size
size_t total_size = strlen(header) + 2048 + strlen(footer) + 256;

// Allocate in PSRAM
char* response = (char*)ps_malloc(total_size);

// Build safely with offset tracking
size_t offset = 0;
offset += snprintf(response + offset, total_size - offset, "%s", header);
offset += snprintf(response + offset, total_size - offset, "%s", content);
offset += snprintf(response + offset, total_size - offset, "%s", footer);

// Send and cleanup
httpd_resp_send(req, response, strlen(response));
free(response);
```

---

## 📊 Project Statistics

### Codebase
- **Files**: 50+ implementation files
- **Lines of Code**: ~15,000+ lines
- **Languages**: C++ (main), HTML/CSS/JavaScript (web UI)
- **Memory**: 16.7% DRAM, 16.7% Flash usage

### Documentation
- **Documents**: 7 comprehensive guides
- **Total Pages**: 200+ pages
- **Coverage**: All Phase 3 features documented
- **Status**: 100% complete for Phase 3

### Testing
- **Device Tests**: Verified working on hardware
- **Functionality**: All Phase 3 features tested
- **Stability**: No crashes, stable 24+ hours
- **Safety**: Heap overflow fixed in 4 pages

---

## 🎯 Development Phases

### Phase 1 ✅ (Complete)
Foundation and communication setup
- ESP-NOW between devices
- Basic network configuration
- Core message passing

### Phase 2 ✅ (Complete)
MQTT integration planning
- MQTT client setup
- Topic subscription
- Message caching

### Phase 3 ✅ 90% Complete
MQTT specs display and receiver configuration
- ✅ WiFi configuration (100%)
- ✅ MQTT client implementation (100%)
- ✅ Battery/Inverter/Charger/System spec pages (100%)
- ✅ Heap buffer overflow fixes (100%)
- ✅ Navigation link corrections (100%)
- 🔄 Type selection feature (0% → In Planning)

### Phase 3.1 🔄 (Next)
Battery & Inverter type selection
- Dropdown UI on settings pages
- API endpoints for type management
- ESP-NOW handler for type changes
- Estimated: 1-2 days to complete

### Phase 4 📅 (Planned)
Advanced features
- Battery profile storage
- Live data streaming
- Alert system

---

## 🔐 Security & Safety

### Safety Improvements (Phase 3)
- ✅ Replaced unsafe strcpy/strcat with snprintf
- ✅ Buffer overflow vulnerabilities fixed
- ✅ PSRAM allocation for large buffers
- ✅ Bounds checking on all string operations
- ✅ Secure NVS storage for credentials

### Code Quality
- ✅ Error handling in all critical paths
- ✅ Memory leak prevention (all allocations freed)
- ✅ Heap corruption detection and fixes
- ✅ Safe configuration persistence

---

## 📈 Performance

| Metric | Value | Status |
|--------|-------|--------|
| Boot Time | ~10 seconds | ✅ Good |
| WiFi Connection | ~5 seconds | ✅ Good |
| MQTT Connection | ~2 seconds | ✅ Excellent |
| Spec Page Load | < 500 ms | ✅ Good |
| Memory Stability | No leaks | ✅ Good |
| Uptime | 24+ hours | ✅ Verified |

---

## 🤝 Contributing

### Code Review Checklist
Before committing:
- [ ] Code follows existing patterns
- [ ] All string operations use snprintf
- [ ] Dynamic allocations use ps_malloc
- [ ] All allocations are freed
- [ ] Error paths return appropriate codes
- [ ] Logging is present at key points
- [ ] Tests pass locally

### Documentation Updates
- [ ] Code changes documented in comments
- [ ] New features documented in markdown
- [ ] Testing steps included
- [ ] Troubleshooting section updated

---

## 📞 Support & Resources

### Documentation
- [PROJECT_STATUS_DASHBOARD.md](PROJECT_STATUS_DASHBOARD.md) - Current status
- [PHASE3_COMPLETE_GUIDE.md](PHASE3_COMPLETE_GUIDE.md) - Feature reference
- [PHASE3_BATTERY_TYPE_SELECTION.md](PHASE3_BATTERY_TYPE_SELECTION.md) - Next feature plan
- [DOCUMENTATION_INDEX.md](DOCUMENTATION_INDEX.md) - Find anything

### Key Files
- **Config**: `lib/receiver_config/receiver_config_manager.h/cpp`
- **API**: `lib/webserver/api/api_handlers.cpp`
- **Web Pages**: `lib/webserver/pages/*.cpp`
- **MQTT**: `src/mqtt/mqtt_task.cpp`
- **ESP-NOW**: `src/espnow/espnow_callbacks.cpp`

### Common Tasks

**Build Project**
```bash
pio run -t upload -t monitor
```

**View Logs**
```
Watch serial output from `pio run -t monitor`
```

**Access Web UI**
```
http://192.168.1.230/
```

**Configure Settings**
```
http://192.168.1.230/systeminfo.html
```

**Check MQTT Topics**
```bash
mosquitto_sub -h 192.168.1.221 -t "BE/#"
```

---

## 🎓 Learning Resources

### Understanding the Project

**5 minutes**: Read PROJECT_STATUS_DASHBOARD.md

**15 minutes**: Read PHASE3_PROGRESS.md + architecture section of PHASE3_COMPLETE_GUIDE.md

**30 minutes**: Read full PHASE3_COMPLETE_GUIDE.md

**60 minutes**: Read PHASE3_BATTERY_TYPE_SELECTION.md and understand implementation plan

---

## ✨ What's Next?

### Immediate (This Week)
- [ ] Begin Phase 3.1 type selection implementation
- [ ] Add battery type selector UI
- [ ] Add inverter type selector UI
- [ ] Implement 5 new API endpoints
- [ ] Add ESP-NOW handler for type changes

### Short Term (Next 2 Weeks)
- [ ] Test type selection end-to-end
- [ ] Verify transmitter switches profiles
- [ ] Validate specs update correctly
- [ ] Performance testing
- [ ] Phase 3 reach 100% completion

### Medium Term (Next Month)
- [ ] Plan Phase 4 features
- [ ] Battery profile storage
- [ ] Live data streaming
- [ ] Alert system

---

## 📝 Document Maintenance

This documentation is maintained alongside code changes. When updates occur:

1. **Code Changes** → Update related markdown
2. **New Features** → Add to PHASE3_COMPLETE_GUIDE.md
3. **Architecture Changes** → Update architecture diagrams
4. **Bug Fixes** → Update troubleshooting section
5. **Testing** → Update testing checklist

---

## 🏁 Quick Reference

**Device IP**: 192.168.1.230
**Web UI**: http://192.168.1.230/
**Settings Page**: http://192.168.1.230/systeminfo.html
**MQTT Broker**: 192.168.1.221:1883
**MQTT Topics**: BE/spec_data, BE/spec_data_2, BE/battery_specs

**Key Files**:
- Config: `lib/receiver_config/receiver_config_manager.h/cpp`
- API: `lib/webserver/api/api_handlers.cpp`
- MQTT: `src/mqtt/mqtt_task.cpp`

**Build Command**: `pio run -t upload -t monitor`

**Current Status**: Phase 3 @ 90%, Phase 3.1 Ready to Start

---

## 📌 Summary

This project implements a **receiver module** for ESP32 Battery Emulator specs via MQTT:

✅ **Phase 3** - Nearly complete (90%)
- Fully functional WiFi configuration
- MQTT client with authentication
- Web dashboard displaying battery/inverter/charger/system specs
- Safe memory operations (heap overflow fixes)
- Complete documentation

🔄 **Phase 3.1** - Ready to start (0%)
- Battery & inverter type selection UI
- API endpoints for type management
- ESP-NOW handler for device control
- Planning complete, development ready

📚 **Documentation** - Comprehensive (100%)
- All features documented
- Architecture diagrams included
- Step-by-step guides provided
- Troubleshooting section available
- Next phase plan ready

**Ready to proceed with Phase 3.1 implementation** ✅

---

**Project Documentation**  
**Last Updated**: February 20, 2026  
**Repository**: esp-multi-project (espnowreciever_2)  
**Status**: Active Development  
**Phase**: 3 (90% complete) → 3.1 (Ready to start)
