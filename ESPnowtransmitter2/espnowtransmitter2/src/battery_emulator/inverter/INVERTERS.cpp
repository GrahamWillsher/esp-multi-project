#include "INVERTERS.h"

InverterProtocol* inverter = nullptr;

InverterProtocolType user_selected_inverter_protocol = InverterProtocolType::BydModbus;

// Some user-configurable settings that can be used by inverters. These
// inverters should use sensible defaults if the corresponding user_selected
// value is zero.
uint16_t user_selected_pylon_send = 0;
uint16_t user_selected_inverter_cells = 0;
uint16_t user_selected_inverter_modules = 0;
uint16_t user_selected_inverter_cells_per_module = 0;
uint16_t user_selected_inverter_voltage_level = 0;
uint16_t user_selected_inverter_ah_capacity = 0;
uint16_t user_selected_inverter_battery_type = 0;
bool user_selected_inverter_ignore_contactors = false;
bool user_selected_pylon_30koffset = false;
bool user_selected_pylon_invert_byteorder = false;
bool user_selected_inverter_deye_workaround = false;

std::vector<InverterProtocolType> supported_inverter_protocols() {
  std::vector<InverterProtocolType> types;

  types.push_back(InverterProtocolType::None);

#if SUPPORT_AFORE_CAN
  types.push_back(InverterProtocolType::AforeCan);
#endif
#if SUPPORT_BYD_CAN
  types.push_back(InverterProtocolType::BydCan);
#endif
#if SUPPORT_BYD_MODBUS
  types.push_back(InverterProtocolType::BydModbus);
#endif
#if SUPPORT_FERROAMP_CAN
  types.push_back(InverterProtocolType::FerroampCan);
#endif
#if SUPPORT_FOXESS_CAN
  types.push_back(InverterProtocolType::Foxess);
#endif
#if SUPPORT_GROWATT_HV_CAN
  types.push_back(InverterProtocolType::GrowattHv);
#endif
#if SUPPORT_GROWATT_LV_CAN
  types.push_back(InverterProtocolType::GrowattLv);
#endif
#if SUPPORT_GROWATT_WIT_CAN
  types.push_back(InverterProtocolType::GrowattWit);
#endif
#if SUPPORT_KOSTAL_RS485
  types.push_back(InverterProtocolType::Kostal);
#endif
#if SUPPORT_PYLON_CAN
  types.push_back(InverterProtocolType::Pylon);
#endif
#if SUPPORT_PYLON_LV_CAN
  types.push_back(InverterProtocolType::PylonLv);
#endif
#if SUPPORT_SCHNEIDER_CAN
  types.push_back(InverterProtocolType::Schneider);
#endif
#if SUPPORT_SMA_BYD_H_CAN
  types.push_back(InverterProtocolType::SmaBydH);
#endif
#if SUPPORT_SMA_BYD_HVS_CAN
  types.push_back(InverterProtocolType::SmaBydHvs);
#endif
#if SUPPORT_SMA_LV_CAN
  types.push_back(InverterProtocolType::SmaLv);
#endif
#if SUPPORT_SMA_TRIPOWER_CAN
  types.push_back(InverterProtocolType::SmaTripower);
#endif
#if SUPPORT_SOFAR_CAN
  types.push_back(InverterProtocolType::Sofar);
#endif
#if SUPPORT_SOLAX_CAN
  types.push_back(InverterProtocolType::Solax);
#endif
#if SUPPORT_SOLXPOW_CAN
  types.push_back(InverterProtocolType::Solxpow);
#endif
#if SUPPORT_SOL_ARK_LV_CAN
  types.push_back(InverterProtocolType::SolArkLv);
#endif
#if SUPPORT_SUNGROW_CAN
  types.push_back(InverterProtocolType::Sungrow);
#endif

  return types;
}

extern const char* name_for_inverter_type(InverterProtocolType type) {
  switch (type) {
    case InverterProtocolType::None:
      return "None";

    case InverterProtocolType::AforeCan:
    #if SUPPORT_AFORE_CAN
      return AforeCanInverter::Name;
    #else
      return "AforeCan (disabled)";
    #endif

    case InverterProtocolType::BydCan:
    #if SUPPORT_BYD_CAN
      return BydCanInverter::Name;
    #else
      return "BydCan (disabled)";
    #endif

    case InverterProtocolType::BydModbus:
#if SUPPORT_BYD_MODBUS
      return BydModbusInverter::Name;
#else
      return "BydModbus (disabled)";
#endif
      break;

    case InverterProtocolType::FerroampCan:
    #if SUPPORT_FERROAMP_CAN
      return FerroampCanInverter::Name;
    #else
      return "FerroampCan (disabled)";
    #endif

    case InverterProtocolType::Foxess:
    #if SUPPORT_FOXESS_CAN
      return FoxessCanInverter::Name;
    #else
      return "Foxess (disabled)";
    #endif

    case InverterProtocolType::GrowattHv:
    #if SUPPORT_GROWATT_HV_CAN
      return GrowattHvInverter::Name;
    #else
      return "GrowattHv (disabled)";
    #endif

    case InverterProtocolType::GrowattLv:
    #if SUPPORT_GROWATT_LV_CAN
      return GrowattLvInverter::Name;
    #else
      return "GrowattLv (disabled)";
    #endif

    case InverterProtocolType::GrowattWit:
    #if SUPPORT_GROWATT_WIT_CAN
      return GrowattWitInverter::Name;
    #else
      return "GrowattWit (disabled)";
    #endif

    case InverterProtocolType::Kostal:
#if SUPPORT_KOSTAL_RS485
      return KostalInverterProtocol::Name;
#else
      return "Kostal (disabled)";
#endif
      break;

    case InverterProtocolType::Pylon:
    #if SUPPORT_PYLON_CAN
      return PylonInverter::Name;
    #else
      return "Pylon (disabled)";
    #endif

    case InverterProtocolType::PylonLv:
    #if SUPPORT_PYLON_LV_CAN
      return PylonLvInverter::Name;
    #else
      return "PylonLv (disabled)";
    #endif

    case InverterProtocolType::Schneider:
    #if SUPPORT_SCHNEIDER_CAN
      return SchneiderInverter::Name;
    #else
      return "Schneider (disabled)";
    #endif

    case InverterProtocolType::SmaBydH:
    #if SUPPORT_SMA_BYD_H_CAN
      return SmaBydHInverter::Name;
    #else
      return "SmaBydH (disabled)";
    #endif

    case InverterProtocolType::SmaBydHvs:
    #if SUPPORT_SMA_BYD_HVS_CAN
      return SmaBydHvsInverter::Name;
    #else
      return "SmaBydHvs (disabled)";
    #endif

    case InverterProtocolType::SmaLv:
    #if SUPPORT_SMA_LV_CAN
      return SmaLvInverter::Name;
    #else
      return "SmaLv (disabled)";
    #endif

    case InverterProtocolType::SmaTripower:
    #if SUPPORT_SMA_TRIPOWER_CAN
      return SmaTripowerInverter::Name;
    #else
      return "SmaTripower (disabled)";
    #endif

    case InverterProtocolType::Sofar:
    #if SUPPORT_SOFAR_CAN
      return SofarInverter::Name;
    #else
      return "Sofar (disabled)";
    #endif

    case InverterProtocolType::Solax:
    #if SUPPORT_SOLAX_CAN
      return SolaxInverter::Name;
    #else
      return "Solax (disabled)";
    #endif

    case InverterProtocolType::Solxpow:
    #if SUPPORT_SOLXPOW_CAN
      return SolxpowInverter::Name;
    #else
      return "Solxpow (disabled)";
    #endif

    case InverterProtocolType::SolArkLv:
    #if SUPPORT_SOL_ARK_LV_CAN
      return SolArkLvInverter::Name;
    #else
      return "SolArkLv (disabled)";
    #endif

    case InverterProtocolType::Sungrow:
    #if SUPPORT_SUNGROW_CAN
      return SungrowInverter::Name;
    #else
      return "Sungrow (disabled)";
    #endif

    case InverterProtocolType::Highest:
      return "None";
  }
  return nullptr;
}

bool setup_inverter() {
  if (inverter) {
    return true;
  }

  switch (user_selected_inverter_protocol) {
    case InverterProtocolType::AforeCan:
#if SUPPORT_AFORE_CAN
      inverter = new AforeCanInverter();
#endif
      break;

    case InverterProtocolType::BydCan:
#if SUPPORT_BYD_CAN
      inverter = new BydCanInverter();
#endif
      break;

    case InverterProtocolType::BydModbus:
#if SUPPORT_BYD_MODBUS
      inverter = new BydModbusInverter();
#endif
      break;

    case InverterProtocolType::FerroampCan:
#if SUPPORT_FERROAMP_CAN
      inverter = new FerroampCanInverter();
#endif
      break;

    case InverterProtocolType::Foxess:
#if SUPPORT_FOXESS_CAN
      inverter = new FoxessCanInverter();
#endif
      break;

    case InverterProtocolType::GrowattHv:
#if SUPPORT_GROWATT_HV_CAN
      inverter = new GrowattHvInverter();
#endif
      break;

    case InverterProtocolType::GrowattLv:
#if SUPPORT_GROWATT_LV_CAN
      inverter = new GrowattLvInverter();
#endif
      break;

    case InverterProtocolType::GrowattWit:
#if SUPPORT_GROWATT_WIT_CAN
      inverter = new GrowattWitInverter();
#endif
      break;

    case InverterProtocolType::Kostal:
#if SUPPORT_KOSTAL_RS485
      inverter = new KostalInverterProtocol();
#endif
      break;

    case InverterProtocolType::Pylon:
#if SUPPORT_PYLON_CAN
      inverter = new PylonInverter();
#endif
      break;

    case InverterProtocolType::PylonLv:
#if SUPPORT_PYLON_LV_CAN
      inverter = new PylonLvInverter();
#endif
      break;

    case InverterProtocolType::Schneider:
#if SUPPORT_SCHNEIDER_CAN
      inverter = new SchneiderInverter();
#endif
      break;

    case InverterProtocolType::SmaBydH:
#if SUPPORT_SMA_BYD_H_CAN
      inverter = new SmaBydHInverter();
#endif
      break;

    case InverterProtocolType::SmaBydHvs:
#if SUPPORT_SMA_BYD_HVS_CAN
      inverter = new SmaBydHvsInverter();
#endif
      break;

    case InverterProtocolType::SmaLv:
#if SUPPORT_SMA_LV_CAN
      inverter = new SmaLvInverter();
#endif
      break;

    case InverterProtocolType::SmaTripower:
#if SUPPORT_SMA_TRIPOWER_CAN
      inverter = new SmaTripowerInverter();
#endif
      break;

    case InverterProtocolType::Sofar:
#if SUPPORT_SOFAR_CAN
      inverter = new SofarInverter();
#endif
      break;

    case InverterProtocolType::Solax:
#if SUPPORT_SOLAX_CAN
      inverter = new SolaxInverter();
#endif
      break;

    case InverterProtocolType::Solxpow:
#if SUPPORT_SOLXPOW_CAN
      inverter = new SolxpowInverter();
#endif
      break;

    case InverterProtocolType::SolArkLv:
#if SUPPORT_SOL_ARK_LV_CAN
      inverter = new SolArkLvInverter();
#endif
      break;

    case InverterProtocolType::Sungrow:
#if SUPPORT_SUNGROW_CAN
      inverter = new SungrowInverter();
#endif
      break;

    case InverterProtocolType::None:
      return true;
    case InverterProtocolType::Highest:
    default:
      inverter = nullptr;  // Or handle as error
      break;
  }

  if (inverter) {
    return inverter->setup();
  }

  return false;
}
