/**
 * @file battery_manager.h
 * @brief Battery Emulator integration manager - FULL MIGRATION
 * 
 * Supports COMPLETE Battery Emulator functionality:
 * - 50+ BMS implementations
 * - 15+ Inverter protocols (CAN, RS485, Modbus)
 * - 2 Charger types
 * - 3 Shunt types
 * - Multi-battery configurations
 * - Complete system control (transmitter) and monitoring (receiver)
 * 
 * This is NOT a partial migration - includes all Battery Emulator components.
 */

#ifndef _BATTERY_MANAGER_H
#define _BATTERY_MANAGER_H

#include <cstdint>
#include "battery_emulator/battery/Battery.h"
#include "battery_emulator/battery/BATTERIES.h"
#include "battery_emulator/battery/Shunt.h"
#include "battery_emulator/charger/CHARGERS.h"
#include "battery_emulator/inverter/INVERTERS.h"
#include "battery_emulator/datalayer/datalayer.h"

// Forward declaration (will include in .cpp only)
class CommunicationManager;

// NOTE: Do NOT redefine enums - they are defined in Battery Emulator:
// - BatteryType: defined in battery/Battery.h
// - InverterType: defined in inverter/INVERTERS.h  
// - ChargerType: defined in charger/CanCharger.h
// - ShuntTypeEnum: defined in battery/Shunt.h
// These are already included above via battery/BATTERIES.h, charger/CHARGERS.h, inverter/INVERTERS.h

/* ============================================================================
   Battery Manager - Full Battery Emulator Integration
   ============================================================================ */

/**
 * @brief Battery Manager - Singleton orchestrating full Battery Emulator integration
 * 
 * Manages:
 * - Primary and secondary batteries (multi-battery support)
 * - Inverter protocol handling
 * - Charger integration
 * - Shunt/current sensor management
 * - Complete datalayer synchronization
 * - All CAN message routing
 */
class BatteryManager {
 public:
  static BatteryManager& instance();
  
  // ========== PRIMARY BATTERY INITIALIZATION ==========
  
  /**
   * @brief Initialize primary battery
   * @param battery_type The BatteryType enum value
   * @return true if successful
   */
  bool init_primary_battery(BatteryType battery_type);
  
  /**
   * @brief Initialize secondary battery (multi-battery support)
   * @param battery_type The BatteryType enum value
   * @return true if successful
   */
  bool init_secondary_battery(BatteryType battery_type);
  
  // ========== INVERTER INITIALIZATION ==========
  
  /**
   * @brief Initialize inverter protocol
   * @param inverter_type The InverterProtocolType enum value
   * @return true if successful
   */
  bool init_inverter(InverterProtocolType inverter_type);
  
  // ========== CHARGER INITIALIZATION ==========
  
  /**
   * @brief Initialize charger protocol
   * @param charger_type The ChargerType enum value
   * @return true if successful
   */
  bool init_charger(ChargerType charger_type);
  
  // ========== SHUNT INITIALIZATION ==========
  
  /**
   * @brief Initialize shunt/current sensor
   * @param shunt_type The ShuntType value
   * @return true if successful
   */
  bool init_shunt(ShuntType shunt_type);
  
  // ========== CAN MESSAGE HANDLING ==========
  
  /**
   * @brief Process incoming CAN message
   * Routes to all registered receivers (batteries, inverters, chargers, shunts)
   */
  void process_can_message(uint32_t can_id, const uint8_t* data, uint8_t dlc);
  
  /**
   * @brief Update all registered transmitters (periodic CAN messages)
   */
  void update_transmitters(unsigned long currentMillis);
  
  // ========== STATUS QUERIES ==========
  
  bool is_primary_battery_initialized() const { return battery_primary_ != nullptr; }
  bool is_secondary_battery_initialized() const { return battery_secondary_ != nullptr; }
  bool is_inverter_initialized() const { return inverter_ != nullptr; }
  bool is_charger_initialized() const { return charger_ != nullptr; }
  bool is_shunt_initialized() const { return shunt_ != nullptr; }
  
  BatteryType get_primary_battery_type() const { return primary_battery_type_; }
  BatteryType get_secondary_battery_type() const { return secondary_battery_type_; }
  InverterProtocolType get_inverter_type() const { return inverter_type_; }
  ChargerType get_charger_type() const { return charger_type_; }
  ShuntType get_shunt_type() const { return shunt_type_; }
  
  // ========== DIAGNOSTICS ==========
  
  /**
   * @brief Print comprehensive diagnostics
   */
  void print_diagnostics();
  
  /**
   * @brief Get human-readable status string
   */
  const char* get_status_string() const;
  
 private:
  BatteryManager() = default;
  ~BatteryManager();
  
  // ========== FACTORY METHODS ==========
  
  /**
   * @brief Create Battery instance of specified type
   */
  Battery* create_battery(BatteryType type);
  
  /**
   * @brief Create Inverter instance of specified type
   */
  InverterProtocol* create_inverter(InverterProtocolType type);
  
  /**
   * @brief Create Charger instance of specified type
   */
  CanCharger* create_charger(ChargerType type);
  
  /**
   * @brief Create Shunt instance of specified type
   */
  CanShunt* create_shunt(ShuntType type);
  
  // ========== STATE VARIABLES ==========
  
  // Primary and secondary batteries
  Battery* battery_primary_ = nullptr;
  Battery* battery_secondary_ = nullptr;
  BatteryType primary_battery_type_ = (BatteryType)0;
  BatteryType secondary_battery_type_ = (BatteryType)0;
  
  // Optional components
  InverterProtocol* inverter_ = nullptr;
  InverterProtocolType inverter_type_ = (InverterProtocolType)0;
  
  CanCharger* charger_ = nullptr;
  ChargerType charger_type_ = (ChargerType)0;
  
  CanShunt* shunt_ = nullptr;
  ShuntType shunt_type_ = (ShuntType)0;
  
  // Component counters for diagnostics
  uint32_t can_messages_processed_ = 0;
  unsigned long last_transmitter_update_ = 0;
};

#endif  // _BATTERY_MANAGER_H
