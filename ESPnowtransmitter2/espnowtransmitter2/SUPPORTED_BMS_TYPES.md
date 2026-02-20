# Supported BMS Types - Complete Reference

**Total Supported:** 45 BMS implementations from Battery Emulator 9.2.4  
**Updated:** February 17, 2026  
**Status:** All types integrated into battery_manager.cpp

---

## BMS Type Enumeration

Use these values in `src/system_settings.h` to select which BMS to use:

### European & Premium

| Value | BMS Type | Class | Notes |
|-------|----------|-------|-------|
| 1 | BMW_I3 | BMW_I3_BATTERY | BMW i3 all generations |
| 2 | BMW_IX | BMW_IX_BATTERY | BMW iX (newest) |
| 3 | BMW_PHEV | BMW_PHEV_BATTERY | BMW PHEV models |
| 4 | BMW_SBOX | BMW_SBOX | BMW SBOX |
| 30 | RANGE_ROVER_PHEV | RANGE_ROVER_PHEV_BATTERY | Jaguar/Range Rover PHEV |
| 18 | JAGUAR_IPACE | JAGUAR_IPACE_BATTERY | Jaguar I-PACE |
| 44 | VOLVO_SPA | VOLVO_SPA_BATTERY | Volvo XC60/XC90/XC40 |
| 45 | VOLVO_SPA_HYBRID | VOLVO_SPA_HYBRID_BATTERY | Volvo Plug-in Hybrid |

### Asian Manufacturers

| Value | BMS Type | Class | Notes |
|-------|----------|-------|-------|
| 6 | BYD_ATTO_3 | BYD_ATTO_3_BATTERY | BYD Yuan Plus / Atto 3 |
| 25 | MG_5 | MG_5_BATTERY | MG5 EV |
| 26 | MG_HS_PHEV | MG_HS_PHEV_BATTERY | MG HS PHEV |
| 15 | GEELY_GEOMETRY_C | GEELY_GEOMETRY_C_BATTERY | Geely Geometry C |
| 16 | HYUNDAI_IONIQ_28 | HYUNDAI_IONIQ_28_BATTERY | Hyundai Ioniq 28kWh |
| 39 | SANTA_FE_PHEV | SANTA_FE_PHEV_BATTERY | Hyundai Santa Fe PHEV |
| 19 | KIA_64FD | KIA_64FD_BATTERY | Kia 64kWh |
| 20 | KIA_E_GMP | KIA_E_GMP_BATTERY | Kia E-GMP platform (EV6, EV9) |
| 21 | KIA_HYUNDAI_64 | KIA_HYUNDAI_64_BATTERY | Kia/Hyundai 64kWh shared |
| 22 | KIA_HYUNDAI_HYBRID | KIA_HYUNDAI_HYBRID_BATTERY | Kia/Hyundai Hybrid |
| 17 | IMIEV_CZERO_ION | IMIEV_CZERO_ION_BATTERY | Mitsubishi i-MiEV, Peugeot iOn, Citroen C-Zero |

### Japanese Manufacturers

| Value | BMS Type | Class | Notes |
|-------|----------|-------|-------|
| 27 | NISSAN_LEAF | NISSAN_LEAF_BATTERY | Nissan Leaf all generations |
| 9 | CMFA_EV | CMFA_EV_BATTERY | Toyota/Subaru shared BMS |

### Chinese/Lithium Specialist

| Value | BMS Type | Class | Notes |
|-------|----------|-------|-------|
| 29 | **PYLON_BATTERY** | **PYLON_BATTERY** | **Pylon LiFePO4 (RECOMMENDED FOR PHASE 1)** |
| 11 | DALY_BMS | DALY_BMS | Daly BMS protocol |
| 37 | RJXZS_BMS | RJXZS_BMS | RJXZS BMS protocol |
| 40 | SIMPBMS | SIMPBMS_BATTERY | SimpleBMS protocol |
| 7 | CELLPOWER_BMS | CELLPOWER_BMS | Cellpower BMS |
| 28 | ORION_BMS | ORION_BMS | Orion BMS |
| 38 | SAMSUNG_SDI_LV | SAMSUNG_SDI_LV_BATTERY | Samsung SDI LV BMS |
| 31 | RELION_LV | RELION_LV_BATTERY | Relion LV LiFePO4 |
| 41 | SONO | SONO_BATTERY | Sono BMS |

### American & Western

| Value | BMS Type | Class | Notes |
|-------|----------|-------|-------|
| 42 | TESLA_BATTERY | TESLA_BATTERY | Tesla Model S/3/X/Y |
| 36 | RIVIAN | RIVIAN_BATTERY | Rivian R1T/R1S |
| 5 | BOLT_AMPERA | BOLT_AMPERA_BATTERY | Chevrolet Bolt / Opel Ampera |
| 13 | FORD_MACH_E | FORD_MACH_E_BATTERY | Ford Mustang Mach-E |
| 23 | MAXUS_EV80 | MAXUS_EV80_BATTERY | Maxus EV80 van |

### VW/Audi Platform

| Value | BMS Type | Class | Notes |
|-------|----------|-------|-------|
| 24 | MEB | MEB_BATTERY | VW/Audi MEB platform (ID.4, ID.5, Etron GT, etc.) |
| 10 | CMP_SMART_CAR | CMP_SMART_CAR_BATTERY | Smart platform |

### Renault

| Value | BMS Type | Class | Notes |
|-------|----------|-------|-------|
| 32 | RENAULT_KANGOO | RENAULT_KANGOO_BATTERY | Renault Kangoo EV |
| 33 | RENAULT_TWIZY | RENAULT_TWIZY | Renault Twizy |
| 34 | RENAULT_ZOE_GEN1 | RENAULT_ZOE_GEN1_BATTERY | Renault Zoe Gen 1 |
| 35 | RENAULT_ZOE_GEN2 | RENAULT_ZOE_GEN2_BATTERY | Renault Zoe Gen 2 |

### Specialist/Industrial

| Value | BMS Type | Class | Notes |
|-------|----------|-------|-------|
| 8 | CHADEMO_BATTERY | CHADEMO_BATTERY | CHAdeMO charging protocol |
| 12 | ECMP | ECMP_BATTERY | ECMP protocol |
| 14 | FOXESS | FOXESS_BATTERY | Foxess energy storage |

### Development/Testing

| Value | BMS Type | Class | Notes |
|-------|----------|-------|-------|
| 43 | TEST_FAKE_BATTERY | TEST_FAKE_BATTERY | Fake battery for testing (generates synthetic data) |
| 0 | NONE | N/A | No BMS selected (disabled) |

---

## Quick Selection Guide

### For Phase 1 (Recommended)
Use **value 29 = PYLON_BATTERY**
- Most common in energy storage systems
- Well-documented CAN protocol
- 500 kbps standard
- LiFePO4 (proven long life)
- Phase 1 focus: monitoring only

### By Vehicle Make

**BMW:** Use 1 (I3), 2 (IX), 3 (PHEV), or 4 (SBOX)  
**Nissan:** Use 27 (Leaf)  
**Tesla:** Use 42 (Model S/3/X/Y)  
**Volkswagen/Audi:** Use 24 (MEB - ID.4/ID.5/E-Tron GT)  
**Kia/Hyundai:** Use 20 (E-GMP - EV6/IONIQ 6/EV9) or 21 (64kWh shared)  
**Renault:** Use 34 (Zoe Gen1) or 35 (Zoe Gen2)  
**Volvo:** Use 44 (SPA) or 45 (SPA Hybrid)  
**BYD:** Use 6 (Atto 3)  
**Pylon (LiFePO4):** Use 29 (RECOMMENDED FOR PHASE 1)  

### By Protocol Family

**CAN (Most Common):**
- Pylon (29)
- Tesla (42)
- Nissan Leaf (27)
- BYD (6)
- BMW (1, 2, 3, 4)
- Others

**Modbus/RS485 (Alternative):**
- Daly (11)
- RJXZS (37)
- SimpleBMS (40)

**Proprietary:**
- ECMP (12)
- Cellpower (7)
- Others

---

## How to Change BMS Type

### Option 1: Configuration File (Recommended for Phase 1)
Edit `src/system_settings.h`:
```cpp
#define SELECTED_BMS_TYPE 29  // Change this number
```

Then rebuild:
```bash
cd espnowtransmitter2
platformio run -t upload
```

### Option 2: Runtime Selection (Phase 2+)
Modify main.cpp to load from settings or user input.

---

## Adding Multi-Battery Support

To support two BMS types simultaneously (Phase 2+):

1. In `battery_manager.h`, add:
```cpp
Battery* battery2 = nullptr;
BatteryType secondary_battery_type_;
```

2. In `battery_manager.cpp`, add:
```cpp
bool init_secondary(BatteryType type) {
  battery2 = create_battery(type);
  battery2->setup();
  return true;
}
```

3. In main.cpp or settings, enable second battery during init.

---

## Phase 1 Deployment Checklist

- [ ] Select BMS type appropriate for your hardware
- [ ] Update `SELECTED_BMS_TYPE` in system_settings.h
- [ ] Verify CAN speed matches BMS (usually 500 kbps)
- [ ] Connect battery to CAN bus
- [ ] Compile and flash transmitter
- [ ] Monitor serial log for: "BMS initialized: [BMS_NAME]"
- [ ] Verify datalayer updates (voltage/current/SOC/temp)
- [ ] Test transmission to receiver
- [ ] Confirm receiver displays data correctly

---

## Notes

- All 45 BMS types are pre-compiled (no conditional compilation needed)
- Each BMS implementation includes:
  - CAN message parsing (specific to each protocol)
  - Datalayer population (voltage, current, SOC, temperature, etc.)
  - Auto-registration with CommunicationManager
  - HTML configuration pages (for web UI, Phase 2+)

- Phase 1 goal: Get selected BMS working with transmitter/receiver  
- Phase 2 goal: Add inverter control, contactor logic, multi-battery support  
- Phase 3+ goal: Add charger control, balancing, OTA config updates

---

**Current Selection:** Pylon LiFePO4 (value 29)  
**Can be changed anytime by:** Updating system_settings.h and rebuilding

For questions about specific BMS compatibility, check Battery Emulator documentation or source code in:
`lib/battery_emulator_src/battery/[BMS_NAME]-BATTERY.h`
