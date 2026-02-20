#ifndef INVERTER_CONFIG_H
#define INVERTER_CONFIG_H

/**
 * Transmitter Inverter Support Configuration
 * 
 * This file defines which Battery Emulator inverter types are compiled
 * for the transmitter. Modbus-based inverters require eModbus library
 * and are excluded by default to reduce dependencies.
 * 
 * Supported Inverter Categories:
 * - CAN-based inverters: Fully supported (no external dependencies)
 * - Modbus-based inverters: Excluded (require eModbus library not in scope for Phase 1)
 */

// ============================================
// CAN-BASED INVERTERS (Fully Supported)
// ============================================

#define SUPPORT_AFORE_CAN 1
#define SUPPORT_BYD_CAN 1
#define SUPPORT_FERROAMP_CAN 1
#define SUPPORT_FOXESS_CAN 1
#define SUPPORT_GROWATT_HV_CAN 1
#define SUPPORT_GROWATT_LV_CAN 1
#define SUPPORT_GROWATT_WIT_CAN 1
#define SUPPORT_PYLON_CAN 1
#define SUPPORT_PYLON_LV_CAN 1
#define SUPPORT_SCHNEIDER_CAN 1
#define SUPPORT_SMA_BYD_H_CAN 1
#define SUPPORT_SMA_BYD_HVS_CAN 1
#define SUPPORT_SMA_LV_CAN 1
#define SUPPORT_SMA_TRIPOWER_CAN 1
#define SUPPORT_SOFAR_CAN 1
#define SUPPORT_SOL_ARK_LV_CAN 1
#define SUPPORT_SOLAX_CAN 1
#define SUPPORT_SOLXPOW_CAN 1
#define SUPPORT_SUNGROW_CAN 1

// ============================================
// MODBUS-BASED INVERTERS (Excluded for Phase 1)
// ============================================
// These require eModbus library which is not
// in the transmitter's scope. They can be added
// in Phase 2 if needed.

#define SUPPORT_BYD_MODBUS 0        // Needs eModbus
#define SUPPORT_KOSTAL_RS485 0      // Needs eModbus
#define SUPPORT_GROWATT_MODBUS 0    // Needs eModbus
#define SUPPORT_FRONIUS_MODBUS 0    // Needs eModbus
#define SUPPORT_SOLARMAX_RS485 0    // Needs eModbus
#define SUPPORT_SMA_MODBUS 0        // Needs eModbus
#define SUPPORT_SOFAR_MODBUS 0      // Needs eModbus
#define SUPPORT_VICTRON_MODBUS 0    // Needs eModbus
#define SUPPORT_PHOCOS_CAN 0        // Uses ModbusServer.h

#endif // INVERTER_CONFIG_H
