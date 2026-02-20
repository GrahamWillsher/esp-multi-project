#ifndef INVERTERS_H
#define INVERTERS_H

#include "InverterProtocol.h"
extern InverterProtocol* inverter;

// ============================================
// CAN-BASED INVERTERS (Always Safe)
// No external dependencies beyond Arduino core
// ============================================

#include "AFORE-CAN.h"
#include "BYD-CAN.h"
#include "FERROAMP-CAN.h"
#include "FOXESS-CAN.h"
#include "GROWATT-HV-CAN.h"
#include "GROWATT-LV-CAN.h"
#include "GROWATT-WIT-CAN.h"
#include "PYLON-CAN.h"
#include "PYLON-LV-CAN.h"
#include "SCHNEIDER-CAN.h"
#include "SMA-BYD-H-CAN.h"
#include "SMA-BYD-HVS-CAN.h"
#include "SMA-LV-CAN.h"
#include "SMA-TRIPOWER-CAN.h"
#include "SOFAR-CAN.h"
#include "SOL-ARK-LV-CAN.h"
#include "SOLAX-CAN.h"
#include "SOLXPOW-CAN.h"
#include "SUNGROW-CAN.h"

// ============================================
// MODBUS-BASED INVERTERS (Conditional)
// ============================================
// These require eModbus library and are only
// included if explicitly enabled via config flags

#if !defined(SUPPORT_BYD_MODBUS)
  #define SUPPORT_BYD_MODBUS 0
#endif
#if SUPPORT_BYD_MODBUS
  #include "BYD-MODBUS.h"
#endif

#if !defined(SUPPORT_KOSTAL_RS485)
  #define SUPPORT_KOSTAL_RS485 0
#endif
#if SUPPORT_KOSTAL_RS485
  #include "KOSTAL-RS485.h"
#endif

#if !defined(SUPPORT_GROWATT_MODBUS)
  #define SUPPORT_GROWATT_MODBUS 0
#endif
#if SUPPORT_GROWATT_MODBUS
  #include "GROWATT-MODBUS.h"
#endif

#if !defined(SUPPORT_FRONIUS_MODBUS)
  #define SUPPORT_FRONIUS_MODBUS 0
#endif
#if SUPPORT_FRONIUS_MODBUS
  #include "FRONIUS-MODBUS.h"
#endif

#if !defined(SUPPORT_SOLARMAX_RS485)
  #define SUPPORT_SOLARMAX_RS485 0
#endif
#if SUPPORT_SOLARMAX_RS485
  #include "SOLARMAX-RS485.h"
#endif

#if !defined(SUPPORT_SMA_MODBUS)
  #define SUPPORT_SMA_MODBUS 0
#endif
#if SUPPORT_SMA_MODBUS
  #include "SMA-MODBUS.h"
#endif

#if !defined(SUPPORT_SOFAR_MODBUS)
  #define SUPPORT_SOFAR_MODBUS 0
#endif
#if SUPPORT_SOFAR_MODBUS
  #include "SOFAR-MODBUS.h"
#endif

#if !defined(SUPPORT_VICTRON_MODBUS)
  #define SUPPORT_VICTRON_MODBUS 0
#endif
#if SUPPORT_VICTRON_MODBUS
  #include "VICTRON-MODBUS.h"
#endif

#if !defined(SUPPORT_PHOCOS_CAN)
  #define SUPPORT_PHOCOS_CAN 0
#endif
#if SUPPORT_PHOCOS_CAN
  #include "PHOCOS-CAN.h"
#endif

// Call to initialize the build-time selected inverter. Safe to call even though inverter was not selected.
bool setup_inverter();

extern uint16_t user_selected_pylon_send;
extern uint16_t user_selected_inverter_cells;
extern uint16_t user_selected_inverter_modules;
extern uint16_t user_selected_inverter_cells_per_module;
extern uint16_t user_selected_inverter_voltage_level;
extern uint16_t user_selected_inverter_ah_capacity;
extern uint16_t user_selected_inverter_battery_type;
extern bool user_selected_inverter_ignore_contactors;
extern bool user_selected_pylon_30koffset;
extern bool user_selected_pylon_invert_byteorder;
extern bool user_selected_inverter_deye_workaround;
#endif
