# CAN Hardware Rewiring Checklist

**Date Created**: February 19, 2026  
**Purpose**: Migrate CAN MISO from GPIO 19 (conflicts with Ethernet) to GPIO 4  
**Status**: Software Ready ✅ | Hardware Rewiring Required ⏭️

---

## Current System State

- ✅ **Software**: Updated to use GPIO 4 for CAN MISO
- ✅ **Firmware**: Flashed and running (version 2.0.0)
- ✅ **Ethernet**: Working (IP: 192.168.1.231)
- ✅ **MQTT**: Connected to broker (192.168.1.221:1883)
- ✅ **NTP**: Time synced
- ✅ **ESP-NOW**: Peer connected
- ⚠️ **CAN**: Non-functional (MISO still on wrong GPIO)

---

## Pre-Rewiring Checklist

### Safety First
- [ ] **Power off the ESP32-POE2** (disconnect PoE/USB)
- [ ] **Disconnect CAN bus wires** (CAN-H, CAN-L) from BMS
- [ ] **Ground yourself** (ESD protection)
- [ ] **Prepare workspace** (good lighting, magnifier if needed)

### Tools & Materials
- [ ] Small screwdriver (if CAN HAT has terminal blocks)
- [ ] Wire stripper/cutter (if rewiring needed)
- [ ] Multimeter (for continuity testing)
- [ ] Jumper wire (if original wire too short for GPIO 4)
- [ ] Label maker or tape (to mark wires)

---

## Current Wiring (BEFORE)

### CAN HAT to ESP32-POE2 (OLD - INCORRECT)
| CAN HAT Pin | Function | ESP32 GPIO | Status |
|-------------|----------|------------|--------|
| MISO        | Data In  | **GPIO 19** | ⚠️ **CONFLICTS WITH ETHERNET** |
| MOSI        | Data Out | GPIO 13    | ✅ Correct |
| SCK         | Clock    | GPIO 14    | ✅ Correct |
| CS          | Chip Sel | GPIO 15    | ✅ Correct |
| INT         | Interrupt| GPIO 32    | ✅ Correct |
| VCC         | Power    | 3.3V       | ✅ Correct |
| GND         | Ground   | GND        | ✅ Correct |

---

## Target Wiring (AFTER)

### CAN HAT to ESP32-POE2 (NEW - CORRECT)
| CAN HAT Pin | Function | ESP32 GPIO | Status |
|-------------|----------|------------|--------|
| MISO        | Data In  | **GPIO 4** | ✅ **NO CONFLICTS** |
| MOSI        | Data Out | GPIO 13    | ✅ Correct |
| SCK         | Clock    | GPIO 14    | ✅ Correct |
| CS          | Chip Sel | GPIO 15    | ✅ Correct |
| INT         | Interrupt| GPIO 32    | ✅ Correct |
| VCC         | Power    | 3.3V       | ✅ Correct |
| GND         | Ground   | GND        | ✅ Correct |

---

## Rewiring Procedure

### Step 1: Disconnect Old MISO Connection
- [ ] **Locate MISO wire** currently connected to GPIO 19 on ESP32-POE2
- [ ] **Photo documentation**: Take photo of current wiring (BEFORE)
- [ ] **Label the wire**: Mark with "MISO" or "GPIO 4" tag
- [ ] **Disconnect from GPIO 19**: Remove wire from GPIO 19 pin
- [ ] **Verify disconnection**: GPIO 19 should now be free

### Step 2: Connect New MISO Connection
- [ ] **Locate GPIO 4** on ESP32-POE2 header
  - Refer to ESP32-POE2 pinout diagram
  - GPIO 4 is available on the GPIO expansion header
- [ ] **Connect MISO to GPIO 4**: Insert wire into GPIO 4 pin
- [ ] **Verify secure connection**: Gently tug to ensure solid connection
- [ ] **Photo documentation**: Take photo of new wiring (AFTER)

### Step 3: Verify All Other Connections
- [ ] **MOSI (GPIO 13)**: Verify connected and secure
- [ ] **SCK (GPIO 14)**: Verify connected and secure
- [ ] **CS (GPIO 15)**: Verify connected and secure
- [ ] **INT (GPIO 32)**: Verify connected and secure
- [ ] **VCC (3.3V)**: Verify connected and secure
- [ ] **GND**: Verify connected and secure

### Step 4: Visual Inspection
- [ ] **No loose wires**: All connections tight
- [ ] **No shorts**: Wires not touching each other
- [ ] **Correct pins**: Double-check GPIO numbers
- [ ] **GPIO 19 free**: Confirm GPIO 19 has no CAN connection

---

## Post-Rewiring Testing

### Power-On Tests
- [ ] **Connect PoE/USB**: Power on ESP32-POE2
- [ ] **Serial monitor**: Open `pio device monitor`
- [ ] **Watch for boot messages**: System should boot normally
- [ ] **Check for errors**: No GPIO conflicts or SPI errors

### Network Tests (Should Still Work)
- [ ] **Ethernet gets IP**: Confirm 192.168.1.231 assigned
- [ ] **Ping device**: `ping 192.168.1.231` from PC
- [ ] **MQTT connects**: Check serial log for "Connected to broker"
- [ ] **NTP syncs**: Verify time synchronization
- [ ] **ESP-NOW active**: Peer connection maintained

### CAN Communication Tests
- [ ] **Reconnect CAN bus**: Connect CAN-H and CAN-L to BMS
- [ ] **Power on BMS**: Ensure BMS is powered and transmitting
- [ ] **Monitor serial output**: Look for CAN messages in logs
- [ ] **Verify CAN traffic**: Should see BMS data being received

**Expected Serial Log Output**:
```
[INFO][CAN] MCP2515 initialized
[INFO][CAN] CAN bus operational at 500 kbps
[INFO][BATTERY] Received frame from BMS: ID=0x123
[INFO][BATTERY] SOC: 85%, Voltage: 400.5V, Current: 12.3A
```

### Integration Tests
- [ ] **All systems active**: Ethernet + MQTT + CAN + ESP-NOW
- [ ] **CAN data forwarded**: BMS data appears in MQTT topics
- [ ] **ESP-NOW working**: Receiver gets CAN data
- [ ] **No GPIO errors**: Serial log clean of conflicts
- [ ] **Stable operation**: Run for 10+ minutes without issues

---

## Troubleshooting Guide

### Problem: CAN Still Not Working After Rewire

**Check these**:
1. **MISO actually on GPIO 4**: Use multimeter to verify continuity
2. **VCC/GND connected**: MCP2515 needs power
3. **CAN bus terminated**: 120Ω resistors on both ends
4. **BMS transmitting**: Verify BMS is powered and active
5. **Firmware correct**: Verify GPIO 4 config in code: `src/communication/can/can_driver.h` line 30

### Problem: Ethernet Stopped Working After Rewire

**Check these**:
1. **GPIO 19 disconnected**: Confirm MISO removed from GPIO 19
2. **No shorts**: Check for accidental connections to Ethernet pins
3. **Cable seated**: PoE cable fully inserted

### Problem: MCP2515 Not Initializing

**Serial log shows**: `[ERROR][CAN] MCP2515 initialization failed`

**Check these**:
1. **SPI wiring**: MOSI=13, SCK=14, CS=15, MISO=4
2. **CS pin**: GPIO 15 functioning as output
3. **INT pin**: GPIO 32 connected for interrupts
4. **Power**: 3.3V on MCP2515 VCC pin
5. **Crystal**: 8MHz crystal oscillating on MCP2515

---

## Verification Checklist

### Hardware Verification
- [ ] MISO disconnected from GPIO 19
- [ ] MISO connected to GPIO 4
- [ ] All other pins correct (MOSI=13, SCK=14, CS=15, INT=32)
- [ ] No loose connections
- [ ] No short circuits

### Software Verification
- [ ] Firmware version 2.0.0 running
- [ ] `can_driver.h` has `MISO_PIN = 4`
- [ ] `network_config.h` has `CAN_ENABLED = true`
- [ ] `network_config.h` has `BATTERY_EMULATOR_ENABLED = true`

### Functional Verification
- [ ] Ethernet: IP assigned, pingable
- [ ] MQTT: Connected to broker
- [ ] NTP: Time synced
- [ ] ESP-NOW: Peer connected
- [ ] CAN: MCP2515 initialized
- [ ] CAN: Messages received from BMS
- [ ] Integration: BMS data forwarded via MQTT/ESP-NOW

### Performance Verification
- [ ] No task watchdog timeouts
- [ ] No memory leaks (stable free heap)
- [ ] No SPI errors
- [ ] No GPIO conflicts
- [ ] System runs stable for 30+ minutes

---

## Success Criteria

✅ **Complete Success** = All of these:
1. Ethernet fully functional (ping, MQTT, NTP)
2. CAN fully functional (BMS messages received)
3. ESP-NOW fully functional (receiver gets data)
4. No GPIO conflicts in serial log
5. System stable for 30+ minutes
6. Battery data appears in MQTT topics

---

## Final Documentation

### After Successful Testing:
- [ ] **Update hardware diagram**: Document GPIO 4 MISO connection
- [ ] **Take photos**: Final wiring configuration
- [ ] **Record baseline**: Free heap, uptime after 30 min test
- [ ] **Archive old wiring**: Keep photos of GPIO 19 config for reference
- [ ] **Update README**: Mark hardware section as validated

### Notes Section (Use for observations during testing):

```
Date: __________
Time: __________
Performed by: __________

Rewiring Notes:
- MISO wire color: __________
- GPIO 4 location on board: __________
- Any issues encountered: __________

Test Results:
- Ethernet IP: __________
- MQTT broker: __________
- CAN messages/sec: __________
- ESP-NOW peer: __________
- System uptime stable: __________

Final Status: [ ] PASS  [ ] FAIL
```

---

## Emergency Rollback (If Needed)

If CAN rewiring causes unexpected issues:

1. **Power off immediately**
2. **Reconnect MISO to GPIO 19** (old configuration)
3. **Disable CAN in software**:
   - Edit `src/config/network_config.h`
   - Set `CAN_ENABLED = false`
   - Set `BATTERY_EMULATOR_ENABLED = false`
4. **Rebuild and flash**: `pio run -t upload`
5. **Verify Ethernet/MQTT work** (as they did before)
6. **Investigate issue** before retry

---

## Contact & Support

**Documentation References**:
- CAN GPIO Analysis: `CAN_ETHERNET_GPIO_CONFLICT_ANALYSIS.md`
- Hardware Details: `README.md` (Hardware section)
- Olimex Pinout: https://www.olimex.com/Products/IoT/ESP32/ESP32-POE2/
- MCP2515 Datasheet: Waveshare RS485/CAN HAT documentation

**Key Files**:
- CAN Driver: `src/communication/can/can_driver.h` (line 30: MISO_PIN)
- Network Config: `src/config/network_config.h` (CAN_ENABLED flag)
- Main Init: `src/main.cpp` (initialization order)

---

## Completion Sign-Off

**Hardware Rewiring Complete**: [ ] YES  [ ] NO  
**All Tests Passed**: [ ] YES  [ ] NO  
**System Stable**: [ ] YES  [ ] NO  
**Date Completed**: __________  
**Signature**: __________  

---

**Next Steps After Completion**:
1. Monitor system for 24 hours
2. Update project documentation with final configuration
3. Create backup of working firmware (`olimex_esp32_poe2_fw_2_0_0.bin`)
4. Consider merging `feature/battery-emulator-migration` to `main` branch
