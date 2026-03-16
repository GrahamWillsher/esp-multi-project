#ifndef INVERTERS_H
#define INVERTERS_H

#include "InverterProtocol.h"
#include <inverter_config.h>

// Fallback defaults (FULL behavior) when config header is not present/complete
#if !defined(SUPPORT_AFORE_CAN)
  #define SUPPORT_AFORE_CAN 1
#endif
#if !defined(SUPPORT_BYD_CAN)
  #define SUPPORT_BYD_CAN 1
#endif
#if !defined(SUPPORT_FERROAMP_CAN)
  #define SUPPORT_FERROAMP_CAN 1
#endif
#if !defined(SUPPORT_FOXESS_CAN)
  #define SUPPORT_FOXESS_CAN 1
#endif
#if !defined(SUPPORT_GROWATT_HV_CAN)
  #define SUPPORT_GROWATT_HV_CAN 1
#endif
#if !defined(SUPPORT_GROWATT_LV_CAN)
  #define SUPPORT_GROWATT_LV_CAN 1
#endif
#if !defined(SUPPORT_GROWATT_WIT_CAN)
  #define SUPPORT_GROWATT_WIT_CAN 1
#endif
#if !defined(SUPPORT_PYLON_CAN)
  #define SUPPORT_PYLON_CAN 1
#endif
#if !defined(SUPPORT_PYLON_LV_CAN)
  #define SUPPORT_PYLON_LV_CAN 1
#endif
#if !defined(SUPPORT_SCHNEIDER_CAN)
  #define SUPPORT_SCHNEIDER_CAN 1
#endif
#if !defined(SUPPORT_SMA_BYD_H_CAN)
  #define SUPPORT_SMA_BYD_H_CAN 1
#endif
#if !defined(SUPPORT_SMA_BYD_HVS_CAN)
  #define SUPPORT_SMA_BYD_HVS_CAN 1
#endif
#if !defined(SUPPORT_SMA_LV_CAN)
  #define SUPPORT_SMA_LV_CAN 1
#endif
#if !defined(SUPPORT_SMA_TRIPOWER_CAN)
  #define SUPPORT_SMA_TRIPOWER_CAN 1
#endif
#if !defined(SUPPORT_SOFAR_CAN)
  #define SUPPORT_SOFAR_CAN 1
#endif
#if !defined(SUPPORT_SOL_ARK_LV_CAN)
  #define SUPPORT_SOL_ARK_LV_CAN 1
#endif
#if !defined(SUPPORT_SOLAX_CAN)
  #define SUPPORT_SOLAX_CAN 1
#endif
#if !defined(SUPPORT_SOLXPOW_CAN)
  #define SUPPORT_SOLXPOW_CAN 1
#endif
#if !defined(SUPPORT_SUNGROW_CAN)
  #define SUPPORT_SUNGROW_CAN 1
#endif

#if !defined(SUPPORT_BYD_MODBUS)
  #define SUPPORT_BYD_MODBUS 0
#endif
#if !defined(SUPPORT_KOSTAL_RS485)
  #define SUPPORT_KOSTAL_RS485 0
#endif

extern InverterProtocol* inverter;

// ============================================
// CAN-BASED INVERTERS (Always Safe)
// No external dependencies beyond Arduino core
// ============================================

#if SUPPORT_AFORE_CAN
  #include "AFORE-CAN.h"
#endif
#if SUPPORT_BYD_CAN
  #include "BYD-CAN.h"
#endif
#if SUPPORT_FERROAMP_CAN
  #include "FERROAMP-CAN.h"
#endif
#if SUPPORT_FOXESS_CAN
  #include "FOXESS-CAN.h"
#endif
#if SUPPORT_GROWATT_HV_CAN
  #include "GROWATT-HV-CAN.h"
#endif
#if SUPPORT_GROWATT_LV_CAN
  #include "GROWATT-LV-CAN.h"
#endif
#if SUPPORT_GROWATT_WIT_CAN
  #include "GROWATT-WIT-CAN.h"
#endif
#if SUPPORT_PYLON_CAN
  #include "PYLON-CAN.h"
#endif
#if SUPPORT_PYLON_LV_CAN
  #include "PYLON-LV-CAN.h"
#endif
#if SUPPORT_SCHNEIDER_CAN
  #include "SCHNEIDER-CAN.h"
#endif
#if SUPPORT_SMA_BYD_H_CAN
  #include "SMA-BYD-H-CAN.h"
#endif
#if SUPPORT_SMA_BYD_HVS_CAN
  #include "SMA-BYD-HVS-CAN.h"
#endif
#if SUPPORT_SMA_LV_CAN
  #include "SMA-LV-CAN.h"
#endif
#if SUPPORT_SMA_TRIPOWER_CAN
  #include "SMA-TRIPOWER-CAN.h"
#endif
#if SUPPORT_SOFAR_CAN
  #include "SOFAR-CAN.h"
#endif
#if SUPPORT_SOL_ARK_LV_CAN
  #include "SOL-ARK-LV-CAN.h"
#endif
#if SUPPORT_SOLAX_CAN
  #include "SOLAX-CAN.h"
#endif
#if SUPPORT_SOLXPOW_CAN
  #include "SOLXPOW-CAN.h"
#endif
#if SUPPORT_SUNGROW_CAN
  #include "SUNGROW-CAN.h"
#endif

// ============================================
// MODBUS-BASED INVERTERS (Conditional)
// ============================================
// These require eModbus library and are only
// included if explicitly enabled via config flags

#if SUPPORT_BYD_MODBUS
  #include "BYD-MODBUS.h"
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
