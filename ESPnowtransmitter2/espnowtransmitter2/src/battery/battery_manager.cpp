/**
 * @file battery_manager.cpp
 * @brief Battery Emulator integration - MINIMAL WRAPPER
 * 
 * This is a minimal wrapper around Battery Emulator's built-in functionality.
 * Battery Emulator ALREADY handles all initialization via:
 * - user_selected_battery_type, user_selected_inverter_protocol, etc.
 * - setup_battery(), setup_inverter(), setup_charger(), setup_shunt()
 * - Global battery, inverter, charger, shunt pointers
 * 
 * We just set the global variables and call setup functions.
 */

#include "battery_manager.h"
#include "battery_emulator/battery/Battery.h"
#include "battery_emulator/battery/BATTERIES.h"
#include "battery_emulator/inverter/INVERTERS.h"
#include "battery_emulator/charger/CHARGERS.h"
#include "battery_emulator/battery/Shunt.h"
#include "battery_emulator/datalayer/datalayer.h"
#include "../config/logging_config.h"
#include <mqtt_logger.h>

// External from Battery Emulator - global component pointers
extern Battery* battery;
extern Battery* battery2;
extern InverterProtocol* inverter;
extern CanCharger* charger;
extern CanShunt* shunt;

// External from Battery Emulator - configuration variables
extern BatteryType user_selected_battery_type;
extern BatteryType user_selected_battery_type_2;
extern InverterProtocolType user_selected_inverter_protocol;
extern ChargerType user_selected_charger_type;
extern ShuntType user_selected_shunt_type;

// External from Battery Emulator - setup functions
extern void setup_battery(void);
extern bool setup_inverter(void);
extern void setup_charger(void);
extern void setup_shunt(void);

BatteryManager& BatteryManager::instance() {
  static BatteryManager instance;
  return instance;
}

BatteryManager::~BatteryManager() {
  // Battery Emulator manages lifecycle - we don't delete
}

bool BatteryManager::init_primary_battery(BatteryType battery_type) {
  if (battery) {
    LOG_WARN("BATTERY_MGR", "Battery already initialized!");
    return false;
  }
  
  LOG_INFO("BATTERY_MGR", "Initializing PRIMARY battery (type %d)...", static_cast<int>(battery_type));
  user_selected_battery_type = battery_type;
  primary_battery_type_ = battery_type;
  
  setup_battery();
  
  if (!battery) {
    LOG_ERROR("BATTERY_MGR", "Battery setup failed!");
    return false;
  }
  
  LOG_INFO("BATTERY_MGR", "✓ Primary battery initialized");
  return true;
}

bool BatteryManager::init_secondary_battery(BatteryType battery_type) {
  if (battery2) {
    LOG_WARN("BATTERY_MGR", "Secondary battery already initialized!");
    return false;
  }
  
  LOG_INFO("BATTERY_MGR", "Initializing SECONDARY battery (type %d)...", static_cast<int>(battery_type));
  user_selected_battery_type_2 = battery_type;
  secondary_battery_type_ = battery_type;
  
  setup_battery();  // Battery Emulator will initialize battery2
  
  if (!battery2) {
    LOG_ERROR("BATTERY_MGR", "Secondary battery setup failed!");
    return false;
  }
  
  LOG_INFO("BATTERY_MGR", "✓ Secondary battery initialized");
  return true;
}

bool BatteryManager::init_inverter(InverterProtocolType inverter_type) {
  if (inverter) {
    LOG_WARN("BATTERY_MGR", "Inverter already initialized!");
    return false;
  }
  
  if (inverter_type == InverterProtocolType::None) {
    LOG_INFO("BATTERY_MGR", "Inverter disabled");
    inverter_type_ = InverterProtocolType::None;
    return true;
  }
  
  LOG_INFO("BATTERY_MGR", "Initializing inverter (type %d)...", static_cast<int>(inverter_type));
  user_selected_inverter_protocol = inverter_type;
  inverter_type_ = inverter_type;
  
  if (!setup_inverter()) {
    LOG_ERROR("BATTERY_MGR", "Inverter setup failed!");
    return false;
  }
  
  if (!inverter) {
    LOG_ERROR("BATTERY_MGR", "Inverter not created!");
    return false;
  }
  
  LOG_INFO("BATTERY_MGR", "✓ Inverter initialized");
  return true;
}

bool BatteryManager::init_charger(ChargerType charger_type) {
  if (charger) {
    LOG_WARN("BATTERY_MGR", "Charger already initialized!");
    return false;
  }
  
  if (charger_type == ChargerType::None) {
    LOG_INFO("BATTERY_MGR", "Charger disabled");
    charger_type_ = ChargerType::None;
    return true;
  }
  
  LOG_INFO("BATTERY_MGR", "Initializing charger (type %d)...", static_cast<int>(charger_type));
  user_selected_charger_type = charger_type;
  charger_type_ = charger_type;
  
  setup_charger();
  
  if (!charger) {
    LOG_ERROR("BATTERY_MGR", "Charger setup failed!");
    return false;
  }
  
  LOG_INFO("BATTERY_MGR", "✓ Charger initialized");
  return true;
}

bool BatteryManager::init_shunt(ShuntType shunt_type) {
  if (shunt) {
    LOG_WARN("BATTERY_MGR", "Shunt already initialized!");
    return false;
  }
  
  if (shunt_type == ShuntType::None) {
    LOG_INFO("BATTERY_MGR", "Shunt disabled");
    shunt_type_ = ShuntType::None;
    return true;
  }
  
  LOG_INFO("BATTERY_MGR", "Initializing shunt (type %d)...", static_cast<int>(shunt_type));
  user_selected_shunt_type = shunt_type;
  shunt_type_ = shunt_type;
  
  setup_shunt();
  
  if (!shunt) {
    LOG_ERROR("BATTERY_MGR", "Shunt setup failed!");
    return false;
  }
  
  LOG_INFO("BATTERY_MGR", "✓ Shunt initialized");
  return true;
}

void BatteryManager::process_can_message(uint32_t can_id, const uint8_t* data, uint8_t dlc) {
  // Battery Emulator handles CAN message routing via registered receivers
  // (auto-registered during setup)
}

void BatteryManager::update_transmitters(unsigned long currentMillis) {
  // Battery Emulator handles transmitter updates via Transmitter base class registry
}

void BatteryManager::print_diagnostics() {
  LOG_INFO("BATTERY_MGR", "=== Battery Manager Status ===");
  LOG_INFO("BATTERY_MGR", "Primary BMS: %s", battery ? "ACTIVE" : "INACTIVE");
  LOG_INFO("BATTERY_MGR", "Secondary BMS: %s", battery2 ? "ACTIVE" : "INACTIVE");
  LOG_INFO("BATTERY_MGR", "Inverter: %s", inverter ? "ACTIVE" : "INACTIVE");
  LOG_INFO("BATTERY_MGR", "Charger: %s", charger ? "ACTIVE" : "INACTIVE");
  LOG_INFO("BATTERY_MGR", "Shunt: %s", shunt ? "ACTIVE" : "INACTIVE");
}

const char* BatteryManager::get_status_string() const {
  static char status[128];
  snprintf(status, sizeof(status), "BMS:%s INV:%s CHG:%s",
           battery ? "OK" : "NO",
           inverter ? "OK" : "NO",
           charger ? "OK" : "NO");
  return status;
}
