#include "BATTERIES.h"
#include "../datalayer/datalayer_extended.h"
#include "CanBattery.h"
#include "RS485Battery.h"

Battery* battery = nullptr;
Battery* battery2 = nullptr;

std::vector<BatteryType> supported_battery_types() {
  std::vector<BatteryType> types;

  types.push_back(BatteryType::None);

#if SUPPORT_BATT_BMW_I3
  types.push_back(BatteryType::BmwI3);
#endif
#if SUPPORT_BATT_BMW_IX
  types.push_back(BatteryType::BmwIX);
#endif
#if SUPPORT_BATT_BOLT_AMPERA
  types.push_back(BatteryType::BoltAmpera);
#endif
#if SUPPORT_BATT_BYD_ATTO3
  types.push_back(BatteryType::BydAtto3);
#endif
#if SUPPORT_BATT_CELLPOWER_BMS
  types.push_back(BatteryType::CellPowerBms);
#endif
#if SUPPORT_BATT_CHADEMO
  types.push_back(BatteryType::Chademo);
#endif
#if SUPPORT_BATT_CMFA_EV
  types.push_back(BatteryType::CmfaEv);
#endif
#if SUPPORT_BATT_FOXESS
  types.push_back(BatteryType::Foxess);
#endif
#if SUPPORT_BATT_GEELY_GEOMETRY_C
  types.push_back(BatteryType::GeelyGeometryC);
#endif
#if SUPPORT_BATT_ORION_BMS
  types.push_back(BatteryType::OrionBms);
#endif
#if SUPPORT_BATT_SONO
  types.push_back(BatteryType::Sono);
#endif
#if SUPPORT_BATT_ECMP
  types.push_back(BatteryType::StellantisEcmp);
#endif
#if SUPPORT_BATT_IMIEV_CZERO_ION
  types.push_back(BatteryType::ImievCZeroIon);
#endif
#if SUPPORT_BATT_JAGUAR_IPACE
  types.push_back(BatteryType::JaguarIpace);
#endif
#if SUPPORT_BATT_KIA_E_GMP
  types.push_back(BatteryType::KiaEGmp);
#endif
#if SUPPORT_BATT_KIA_HYUNDAI_64
  types.push_back(BatteryType::KiaHyundai64);
#endif
#if SUPPORT_BATT_KIA_HYUNDAI_HYBRID
  types.push_back(BatteryType::KiaHyundaiHybrid);
#endif
#if SUPPORT_BATT_MEB
  types.push_back(BatteryType::Meb);
#endif
#if SUPPORT_BATT_MG5
  types.push_back(BatteryType::Mg5);
#endif
#if SUPPORT_BATT_NISSAN_LEAF
  types.push_back(BatteryType::NissanLeaf);
#endif
#if SUPPORT_BATT_PYLON
  types.push_back(BatteryType::Pylon);
#endif
#if SUPPORT_BATT_DALY_BMS
  types.push_back(BatteryType::DalyBms);
#endif
#if SUPPORT_BATT_RJXZS_BMS
  types.push_back(BatteryType::RjxzsBms);
#endif
#if SUPPORT_BATT_RANGE_ROVER_PHEV
  types.push_back(BatteryType::RangeRoverPhev);
#endif
#if SUPPORT_BATT_RENAULT_KANGOO
  types.push_back(BatteryType::RenaultKangoo);
#endif
#if SUPPORT_BATT_RENAULT_TWIZY
  types.push_back(BatteryType::RenaultTwizy);
#endif
#if SUPPORT_BATT_RENAULT_ZOE1
  types.push_back(BatteryType::RenaultZoe1);
#endif
#if SUPPORT_BATT_RENAULT_ZOE2
  types.push_back(BatteryType::RenaultZoe2);
#endif
#if SUPPORT_BATT_SANTA_FE_PHEV
  types.push_back(BatteryType::SantaFePhev);
#endif
#if SUPPORT_BATT_SIMPBMS
  types.push_back(BatteryType::SimpBms);
#endif
#if SUPPORT_BATT_TESLA_MODEL3Y
  types.push_back(BatteryType::TeslaModel3Y);
#endif
#if SUPPORT_BATT_TESLA_MODELSX
  types.push_back(BatteryType::TeslaModelSX);
#endif
#if SUPPORT_BATT_TEST_FAKE
  types.push_back(BatteryType::TestFake);
#endif
#if SUPPORT_BATT_VOLVO_SPA
  types.push_back(BatteryType::VolvoSpa);
#endif
#if SUPPORT_BATT_VOLVO_SPA_HYBRID
  types.push_back(BatteryType::VolvoSpaHybrid);
#endif
#if SUPPORT_BATT_MG_HS_PHEV
  types.push_back(BatteryType::MgHsPhev);
#endif
#if SUPPORT_BATT_SAMSUNG_SDI_LV
  types.push_back(BatteryType::SamsungSdiLv);
#endif
#if SUPPORT_BATT_HYUNDAI_IONIQ_28
  types.push_back(BatteryType::HyundaiIoniq28);
#endif
#if SUPPORT_BATT_KIA_64FD
  types.push_back(BatteryType::Kia64FD);
#endif
#if SUPPORT_BATT_RELION
  types.push_back(BatteryType::RelionBattery);
#endif
#if SUPPORT_BATT_RIVIAN
  types.push_back(BatteryType::RivianBattery);
#endif
#if SUPPORT_BATT_BMW_PHEV
  types.push_back(BatteryType::BmwPhev);
#endif
#if SUPPORT_BATT_FORD_MACH_E
  types.push_back(BatteryType::FordMachE);
#endif
#if SUPPORT_BATT_CMP_SMART_CAR
  types.push_back(BatteryType::CmpSmartCar);
#endif
#if SUPPORT_BATT_MAXUS_EV80
  types.push_back(BatteryType::MaxusEV80);
#endif

  return types;
}

const char* name_for_chemistry(battery_chemistry_enum chem) {
  switch (chem) {
    case battery_chemistry_enum::Autodetect:
      return "Autodetect";
    case battery_chemistry_enum::LFP:
      return "LFP";
    case battery_chemistry_enum::NCA:
      return "NCA";
    case battery_chemistry_enum::NMC:
      return "NMC";
    default:
      return nullptr;
  }
}

const char* name_for_comm_interface(comm_interface comm) {
  switch (comm) {
    case comm_interface::Modbus:
      return "Modbus";
    case comm_interface::RS485:
      return "RS485";
    case comm_interface::CanNative:
      return "CAN (Native)";
    case comm_interface::CanFdNative:
      return "";
    case comm_interface::CanAddonMcp2515:
      return "CAN (MCP2515 add-on)";
    case comm_interface::CanFdAddonMcp2518:
      return "CAN FD (MCP2518 add-on)";
    default:
      return nullptr;
  }
}

const char* name_for_battery_type(BatteryType type) {
  switch (type) {
    case BatteryType::None:
      return "None";
    case BatteryType::BmwI3:
    #if SUPPORT_BATT_BMW_I3
      return BmwI3Battery::Name;
    #else
      return "BmwI3 (disabled)";
    #endif
    case BatteryType::BmwIX:
    #if SUPPORT_BATT_BMW_IX
      return BmwIXBattery::Name;
    #else
      return "BmwIX (disabled)";
    #endif
    case BatteryType::BmwPhev:
    #if SUPPORT_BATT_BMW_PHEV
      return BmwPhevBattery::Name;
    #else
      return "BmwPhev (disabled)";
    #endif
    case BatteryType::BoltAmpera:
    #if SUPPORT_BATT_BOLT_AMPERA
      return BoltAmperaBattery::Name;
    #else
      return "BoltAmpera (disabled)";
    #endif
    case BatteryType::BydAtto3:
    #if SUPPORT_BATT_BYD_ATTO3
      return BydAttoBattery::Name;
    #else
      return "BydAtto3 (disabled)";
    #endif
    case BatteryType::CellPowerBms:
    #if SUPPORT_BATT_CELLPOWER_BMS
      return CellPowerBms::Name;
    #else
      return "CellPowerBms (disabled)";
    #endif
    case BatteryType::Chademo:
    #if SUPPORT_BATT_CHADEMO
      return ChademoBattery::Name;
    #else
      return "Chademo (disabled)";
    #endif
    case BatteryType::CmfaEv:
    #if SUPPORT_BATT_CMFA_EV
      return CmfaEvBattery::Name;
    #else
      return "CmfaEv (disabled)";
    #endif
    case BatteryType::CmpSmartCar:
    #if SUPPORT_BATT_CMP_SMART_CAR
      return CmpSmartCarBattery::Name;
    #else
      return "CmpSmartCar (disabled)";
    #endif
    case BatteryType::FordMachE:
    #if SUPPORT_BATT_FORD_MACH_E
      return FordMachEBattery::Name;
    #else
      return "FordMachE (disabled)";
    #endif
    case BatteryType::Foxess:
    #if SUPPORT_BATT_FOXESS
      return FoxessBattery::Name;
    #else
      return "Foxess (disabled)";
    #endif
    case BatteryType::GeelyGeometryC:
    #if SUPPORT_BATT_GEELY_GEOMETRY_C
      return GeelyGeometryCBattery::Name;
    #else
      return "GeelyGeometryC (disabled)";
    #endif
    case BatteryType::HyundaiIoniq28:
    #if SUPPORT_BATT_HYUNDAI_IONIQ_28
      return HyundaiIoniq28Battery::Name;
    #else
      return "HyundaiIoniq28 (disabled)";
    #endif
    case BatteryType::OrionBms:
    #if SUPPORT_BATT_ORION_BMS
      return OrionBms::Name;
    #else
      return "OrionBms (disabled)";
    #endif
    case BatteryType::Sono:
    #if SUPPORT_BATT_SONO
      return SonoBattery::Name;
    #else
      return "Sono (disabled)";
    #endif
    case BatteryType::StellantisEcmp:
    #if SUPPORT_BATT_ECMP
      return EcmpBattery::Name;
    #else
      return "StellantisEcmp (disabled)";
    #endif
    case BatteryType::ImievCZeroIon:
    #if SUPPORT_BATT_IMIEV_CZERO_ION
      return ImievCZeroIonBattery::Name;
    #else
      return "ImievCZeroIon (disabled)";
    #endif
    case BatteryType::JaguarIpace:
    #if SUPPORT_BATT_JAGUAR_IPACE
      return JaguarIpaceBattery::Name;
    #else
      return "JaguarIpace (disabled)";
    #endif
    case BatteryType::KiaEGmp:
    #if SUPPORT_BATT_KIA_E_GMP
      return KiaEGmpBattery::Name;
    #else
      return "KiaEGmp (disabled)";
    #endif
    case BatteryType::KiaHyundai64:
    #if SUPPORT_BATT_KIA_HYUNDAI_64
      return KiaHyundai64Battery::Name;
    #else
      return "KiaHyundai64 (disabled)";
    #endif
    case BatteryType::Kia64FD:
    #if SUPPORT_BATT_KIA_64FD
      return Kia64FDBattery::Name;
    #else
      return "Kia64FD (disabled)";
    #endif
    case BatteryType::KiaHyundaiHybrid:
    #if SUPPORT_BATT_KIA_HYUNDAI_HYBRID
      return KiaHyundaiHybridBattery::Name;
    #else
      return "KiaHyundaiHybrid (disabled)";
    #endif
    case BatteryType::MaxusEV80:
    #if SUPPORT_BATT_MAXUS_EV80
      return MaxusEV80Battery::Name;
    #else
      return "MaxusEV80 (disabled)";
    #endif
    case BatteryType::Meb:
    #if SUPPORT_BATT_MEB
      return MebBattery::Name;
    #else
      return "Meb (disabled)";
    #endif
    case BatteryType::Mg5:
    #if SUPPORT_BATT_MG5
      return Mg5Battery::Name;
    #else
      return "Mg5 (disabled)";
    #endif
    case BatteryType::MgHsPhev:
    #if SUPPORT_BATT_MG_HS_PHEV
      return MgHsPHEVBattery::Name;
    #else
      return "MgHsPhev (disabled)";
    #endif
    case BatteryType::NissanLeaf:
    #if SUPPORT_BATT_NISSAN_LEAF
      return NissanLeafBattery::Name;
    #else
      return "NissanLeaf (disabled)";
    #endif
    case BatteryType::Pylon:
    #if SUPPORT_BATT_PYLON
      return PylonBattery::Name;
    #else
      return "Pylon (disabled)";
    #endif
    case BatteryType::DalyBms:
    #if SUPPORT_BATT_DALY_BMS
      return DalyBms::Name;
    #else
      return "DalyBms (disabled)";
    #endif
    case BatteryType::RjxzsBms:
    #if SUPPORT_BATT_RJXZS_BMS
      return RjxzsBms::Name;
    #else
      return "RjxzsBms (disabled)";
    #endif
    case BatteryType::RangeRoverPhev:
    #if SUPPORT_BATT_RANGE_ROVER_PHEV
      return RangeRoverPhevBattery::Name;
    #else
      return "RangeRoverPhev (disabled)";
    #endif
    case BatteryType::RelionBattery:
    #if SUPPORT_BATT_RELION
      return RelionBattery::Name;
    #else
      return "RelionBattery (disabled)";
    #endif
    case BatteryType::RenaultKangoo:
    #if SUPPORT_BATT_RENAULT_KANGOO
      return RenaultKangooBattery::Name;
    #else
      return "RenaultKangoo (disabled)";
    #endif
    case BatteryType::RenaultTwizy:
    #if SUPPORT_BATT_RENAULT_TWIZY
      return RenaultTwizyBattery::Name;
    #else
      return "RenaultTwizy (disabled)";
    #endif
    case BatteryType::RenaultZoe1:
    #if SUPPORT_BATT_RENAULT_ZOE1
      return RenaultZoeGen1Battery::Name;
    #else
      return "RenaultZoe1 (disabled)";
    #endif
    case BatteryType::RenaultZoe2:
    #if SUPPORT_BATT_RENAULT_ZOE2
      return RenaultZoeGen2Battery::Name;
    #else
      return "RenaultZoe2 (disabled)";
    #endif
    case BatteryType::RivianBattery:
    #if SUPPORT_BATT_RIVIAN
      return RivianBattery::Name;
    #else
      return "RivianBattery (disabled)";
    #endif
    case BatteryType::SamsungSdiLv:
    #if SUPPORT_BATT_SAMSUNG_SDI_LV
      return SamsungSdiLVBattery::Name;
    #else
      return "SamsungSdiLv (disabled)";
    #endif
    case BatteryType::SantaFePhev:
    #if SUPPORT_BATT_SANTA_FE_PHEV
      return SantaFePhevBattery::Name;
    #else
      return "SantaFePhev (disabled)";
    #endif
    case BatteryType::SimpBms:
    #if SUPPORT_BATT_SIMPBMS
      return SimpBmsBattery::Name;
    #else
      return "SimpBms (disabled)";
    #endif
    case BatteryType::TeslaModel3Y:
    #if SUPPORT_BATT_TESLA_MODEL3Y
      return TeslaModel3YBattery::Name;
    #else
      return "TeslaModel3Y (disabled)";
    #endif
    case BatteryType::TeslaModelSX:
    #if SUPPORT_BATT_TESLA_MODELSX
      return TeslaModelSXBattery::Name;
    #else
      return "TeslaModelSX (disabled)";
    #endif
    case BatteryType::TestFake:
    #if SUPPORT_BATT_TEST_FAKE
      return TestFakeBattery::Name;
    #else
      return "TestFake (disabled)";
    #endif
    case BatteryType::VolvoSpa:
    #if SUPPORT_BATT_VOLVO_SPA
      return VolvoSpaBattery::Name;
    #else
      return "VolvoSpa (disabled)";
    #endif
    case BatteryType::VolvoSpaHybrid:
    #if SUPPORT_BATT_VOLVO_SPA_HYBRID
      return VolvoSpaHybridBattery::Name;
    #else
      return "VolvoSpaHybrid (disabled)";
    #endif
    default:
      return nullptr;
  }
}

const battery_chemistry_enum battery_chemistry_default = battery_chemistry_enum::NMC;

battery_chemistry_enum user_selected_battery_chemistry = battery_chemistry_default;

BatteryType user_selected_battery_type = BatteryType::NissanLeaf;
bool user_selected_second_battery = false;

Battery* create_battery(BatteryType type) {
  switch (type) {
    case BatteryType::None:
      return nullptr;
    case BatteryType::BmwI3:
#if SUPPORT_BATT_BMW_I3
      return new BmwI3Battery();
#endif
      return nullptr;
    case BatteryType::BmwIX:
#if SUPPORT_BATT_BMW_IX
      return new BmwIXBattery();
#endif
      return nullptr;
    case BatteryType::BmwPhev:
#if SUPPORT_BATT_BMW_PHEV
      return new BmwPhevBattery();
#endif
      return nullptr;
    case BatteryType::BoltAmpera:
#if SUPPORT_BATT_BOLT_AMPERA
      return new BoltAmperaBattery();
#endif
      return nullptr;
    case BatteryType::BydAtto3:
#if SUPPORT_BATT_BYD_ATTO3
      return new BydAttoBattery();
#endif
      return nullptr;
    case BatteryType::CellPowerBms:
#if SUPPORT_BATT_CELLPOWER_BMS
      return new CellPowerBms();
#endif
      return nullptr;
    case BatteryType::Chademo:
#if SUPPORT_BATT_CHADEMO
      return new ChademoBattery();
#endif
      return nullptr;
    case BatteryType::CmfaEv:
#if SUPPORT_BATT_CMFA_EV
      return new CmfaEvBattery();
#endif
      return nullptr;
    case BatteryType::CmpSmartCar:
#if SUPPORT_BATT_CMP_SMART_CAR
      return new CmpSmartCarBattery();
#endif
      return nullptr;
    case BatteryType::FordMachE:
#if SUPPORT_BATT_FORD_MACH_E
      return new FordMachEBattery();
#endif
      return nullptr;
    case BatteryType::Foxess:
#if SUPPORT_BATT_FOXESS
      return new FoxessBattery();
#endif
      return nullptr;
    case BatteryType::GeelyGeometryC:
#if SUPPORT_BATT_GEELY_GEOMETRY_C
      return new GeelyGeometryCBattery();
#endif
      return nullptr;
    case BatteryType::HyundaiIoniq28:
#if SUPPORT_BATT_HYUNDAI_IONIQ_28
      return new HyundaiIoniq28Battery();
#endif
      return nullptr;
    case BatteryType::OrionBms:
#if SUPPORT_BATT_ORION_BMS
      return new OrionBms();
#endif
      return nullptr;
    case BatteryType::Sono:
#if SUPPORT_BATT_SONO
      return new SonoBattery();
#endif
      return nullptr;
    case BatteryType::StellantisEcmp:
#if SUPPORT_BATT_ECMP
      return new EcmpBattery();
#endif
      return nullptr;
    case BatteryType::ImievCZeroIon:
#if SUPPORT_BATT_IMIEV_CZERO_ION
      return new ImievCZeroIonBattery();
#endif
      return nullptr;
    case BatteryType::JaguarIpace:
#if SUPPORT_BATT_JAGUAR_IPACE
      return new JaguarIpaceBattery();
#endif
      return nullptr;
    case BatteryType::Kia64FD:
#if SUPPORT_BATT_KIA_64FD
      return new Kia64FDBattery();
#endif
      return nullptr;
    case BatteryType::KiaEGmp:
#if SUPPORT_BATT_KIA_E_GMP
      return new KiaEGmpBattery();
#endif
      return nullptr;
    case BatteryType::KiaHyundai64:
#if SUPPORT_BATT_KIA_HYUNDAI_64
      return new KiaHyundai64Battery();
#endif
      return nullptr;
    case BatteryType::KiaHyundaiHybrid:
#if SUPPORT_BATT_KIA_HYUNDAI_HYBRID
      return new KiaHyundaiHybridBattery();
#endif
      return nullptr;
    case BatteryType::MaxusEV80:
#if SUPPORT_BATT_MAXUS_EV80
      return new MaxusEV80Battery();
#endif
      return nullptr;
    case BatteryType::Meb:
#if SUPPORT_BATT_MEB
      return new MebBattery();
#endif
      return nullptr;
    case BatteryType::Mg5:
#if SUPPORT_BATT_MG5
      return new Mg5Battery();
#endif
      return nullptr;
    case BatteryType::MgHsPhev:
#if SUPPORT_BATT_MG_HS_PHEV
      return new MgHsPHEVBattery();
#endif
      return nullptr;
    case BatteryType::NissanLeaf:
#if SUPPORT_BATT_NISSAN_LEAF
      return new NissanLeafBattery();
#endif
      return nullptr;
    case BatteryType::Pylon:
#if SUPPORT_BATT_PYLON
      return new PylonBattery();
#endif
      return nullptr;
    case BatteryType::DalyBms:
#if SUPPORT_BATT_DALY_BMS
      return new DalyBms();
#endif
      return nullptr;
    case BatteryType::RjxzsBms:
#if SUPPORT_BATT_RJXZS_BMS
      return new RjxzsBms();
#endif
      return nullptr;
    case BatteryType::RangeRoverPhev:
#if SUPPORT_BATT_RANGE_ROVER_PHEV
      return new RangeRoverPhevBattery();
#endif
      return nullptr;
    case BatteryType::RelionBattery:
#if SUPPORT_BATT_RELION
      return new RelionBattery();
#endif
      return nullptr;
    case BatteryType::RenaultKangoo:
#if SUPPORT_BATT_RENAULT_KANGOO
      return new RenaultKangooBattery();
#endif
      return nullptr;
    case BatteryType::RenaultTwizy:
#if SUPPORT_BATT_RENAULT_TWIZY
      return new RenaultTwizyBattery();
#endif
      return nullptr;
    case BatteryType::RenaultZoe1:
#if SUPPORT_BATT_RENAULT_ZOE1
      return new RenaultZoeGen1Battery();
#endif
      return nullptr;
    case BatteryType::RenaultZoe2:
#if SUPPORT_BATT_RENAULT_ZOE2
      return new RenaultZoeGen2Battery();
#endif
      return nullptr;
    case BatteryType::RivianBattery:
#if SUPPORT_BATT_RIVIAN
      return new RivianBattery();
#endif
      return nullptr;
    case BatteryType::SamsungSdiLv:
#if SUPPORT_BATT_SAMSUNG_SDI_LV
      return new SamsungSdiLVBattery();
#endif
      return nullptr;
    case BatteryType::SantaFePhev:
#if SUPPORT_BATT_SANTA_FE_PHEV
      return new SantaFePhevBattery();
#endif
      return nullptr;
    case BatteryType::SimpBms:
#if SUPPORT_BATT_SIMPBMS
      return new SimpBmsBattery();
#endif
      return nullptr;
    case BatteryType::TeslaModel3Y:
#if SUPPORT_BATT_TESLA_MODEL3Y
      return new TeslaModel3YBattery(user_selected_battery_chemistry);
#endif
      return nullptr;
    case BatteryType::TeslaModelSX:
#if SUPPORT_BATT_TESLA_MODELSX
      return new TeslaModelSXBattery();
#endif
      return nullptr;
    case BatteryType::TestFake:
#if SUPPORT_BATT_TEST_FAKE
      return new TestFakeBattery();
#endif
      return nullptr;
    case BatteryType::VolvoSpa:
#if SUPPORT_BATT_VOLVO_SPA
      return new VolvoSpaBattery();
#endif
      return nullptr;
    case BatteryType::VolvoSpaHybrid:
#if SUPPORT_BATT_VOLVO_SPA_HYBRID
      return new VolvoSpaHybridBattery();
#endif
      return nullptr;
    default:
      return nullptr;
  }
}

void setup_battery() {
  if (battery) {
    // Let's not create the battery again.
    return;
  }

  battery = create_battery(user_selected_battery_type);

  if (battery) {
    battery->setup();
  }

  if (user_selected_second_battery && !battery2) {
    switch (user_selected_battery_type) {
      case BatteryType::NissanLeaf:
#if SUPPORT_BATT_NISSAN_LEAF
        battery2 = new NissanLeafBattery(&datalayer.battery2, nullptr, can_config.battery_double);
#endif
        break;
      case BatteryType::BmwI3:
#if SUPPORT_BATT_BMW_I3
        battery2 = new BmwI3Battery(&datalayer.battery2, &datalayer.system.status.battery2_allowed_contactor_closing,
                  can_config.battery_double, GPIO_NUM_NC);
#endif
        break;
      case BatteryType::CmfaEv:
#if SUPPORT_BATT_CMFA_EV
        battery2 = new CmfaEvBattery(&datalayer.battery2, nullptr, can_config.battery_double);
#endif
        break;
      case BatteryType::KiaHyundai64:
#if SUPPORT_BATT_KIA_HYUNDAI_64
        battery2 = new KiaHyundai64Battery(&datalayer.battery2, &datalayer_extended.KiaHyundai64_2,
                                           &datalayer.system.status.battery2_allowed_contactor_closing,
                                           can_config.battery_double);
#endif
        break;
      case BatteryType::SantaFePhev:
#if SUPPORT_BATT_SANTA_FE_PHEV
        battery2 = new SantaFePhevBattery(&datalayer.battery2, can_config.battery_double);
#endif
        break;
      case BatteryType::RenaultZoe1:
#if SUPPORT_BATT_RENAULT_ZOE1
        battery2 = new RenaultZoeGen1Battery(&datalayer.battery2, nullptr, can_config.battery_double);
#endif
        break;
      case BatteryType::RenaultZoe2:
#if SUPPORT_BATT_RENAULT_ZOE2
        battery2 = new RenaultZoeGen2Battery(&datalayer.battery2, nullptr, can_config.battery_double);
#endif
        break;
      case BatteryType::TestFake:
#if SUPPORT_BATT_TEST_FAKE
        battery2 = new TestFakeBattery(&datalayer.battery2, can_config.battery_double);
#endif
        break;
      default:
        DEBUG_PRINTF("User tried enabling double battery on non-supported integration!\n");
        break;
    }

    if (battery2) {
      battery2->setup();
    }
  }
}

/* User-selected Nissan LEAF settings */
bool user_selected_LEAF_interlock_mandatory = false;
/* User-selected Tesla settings */
bool user_selected_tesla_digital_HVIL = false;
uint16_t user_selected_tesla_GTW_country = 17477;
bool user_selected_tesla_GTW_rightHandDrive = true;
uint16_t user_selected_tesla_GTW_mapRegion = 2;
uint16_t user_selected_tesla_GTW_chassisType = 2;
uint16_t user_selected_tesla_GTW_packEnergy = 1;
/* User-selected EGMP+others settings */
bool user_selected_use_estimated_SOC = false;

// Use 0V for user selected cell/pack voltage defaults (On boot will be replaced with saved values from NVM)
uint16_t user_selected_max_pack_voltage_dV = 0;
uint16_t user_selected_min_pack_voltage_dV = 0;
uint16_t user_selected_max_cell_voltage_mV = 0;
uint16_t user_selected_min_cell_voltage_mV = 0;
