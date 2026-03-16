#ifndef BATTERIES_H
#define BATTERIES_H
#include "Shunt.h"

// FULL-build fallback defaults for battery support flags.
// These keep current runtime behavior unchanged while allowing the
// include/factory/name surfaces to be guarded consistently.
#if !defined(SUPPORT_BATT_BMW_I3)
	#define SUPPORT_BATT_BMW_I3 1
#endif
#if !defined(SUPPORT_BATT_BMW_IX)
	#define SUPPORT_BATT_BMW_IX 1
#endif
#if !defined(SUPPORT_BATT_BMW_PHEV)
	#define SUPPORT_BATT_BMW_PHEV 1
#endif
#if !defined(SUPPORT_BATT_BOLT_AMPERA)
	#define SUPPORT_BATT_BOLT_AMPERA 1
#endif
#if !defined(SUPPORT_BATT_BYD_ATTO3)
	#define SUPPORT_BATT_BYD_ATTO3 1
#endif
#if !defined(SUPPORT_BATT_CELLPOWER_BMS)
	#define SUPPORT_BATT_CELLPOWER_BMS 1
#endif
#if !defined(SUPPORT_BATT_CHADEMO)
	#define SUPPORT_BATT_CHADEMO 1
#endif
#if !defined(SUPPORT_BATT_CMFA_EV)
	#define SUPPORT_BATT_CMFA_EV 1
#endif
#if !defined(SUPPORT_BATT_CMP_SMART_CAR)
	#define SUPPORT_BATT_CMP_SMART_CAR 1
#endif
#if !defined(SUPPORT_BATT_DALY_BMS)
	#define SUPPORT_BATT_DALY_BMS 1
#endif
#if !defined(SUPPORT_BATT_ECMP)
	#define SUPPORT_BATT_ECMP 1
#endif
#if !defined(SUPPORT_BATT_FORD_MACH_E)
	#define SUPPORT_BATT_FORD_MACH_E 1
#endif
#if !defined(SUPPORT_BATT_FOXESS)
	#define SUPPORT_BATT_FOXESS 1
#endif
#if !defined(SUPPORT_BATT_GEELY_GEOMETRY_C)
	#define SUPPORT_BATT_GEELY_GEOMETRY_C 1
#endif
#if !defined(SUPPORT_BATT_HYUNDAI_IONIQ_28)
	#define SUPPORT_BATT_HYUNDAI_IONIQ_28 1
#endif
#if !defined(SUPPORT_BATT_IMIEV_CZERO_ION)
	#define SUPPORT_BATT_IMIEV_CZERO_ION 1
#endif
#if !defined(SUPPORT_BATT_JAGUAR_IPACE)
	#define SUPPORT_BATT_JAGUAR_IPACE 1
#endif
#if !defined(SUPPORT_BATT_KIA_64FD)
	#define SUPPORT_BATT_KIA_64FD 1
#endif
#if !defined(SUPPORT_BATT_KIA_E_GMP)
	#define SUPPORT_BATT_KIA_E_GMP 1
#endif
#if !defined(SUPPORT_BATT_KIA_HYUNDAI_64)
	#define SUPPORT_BATT_KIA_HYUNDAI_64 1
#endif
#if !defined(SUPPORT_BATT_KIA_HYUNDAI_HYBRID)
	#define SUPPORT_BATT_KIA_HYUNDAI_HYBRID 1
#endif
#if !defined(SUPPORT_BATT_MAXUS_EV80)
	#define SUPPORT_BATT_MAXUS_EV80 1
#endif
#if !defined(SUPPORT_BATT_MEB)
	#define SUPPORT_BATT_MEB 1
#endif
#if !defined(SUPPORT_BATT_MG5)
	#define SUPPORT_BATT_MG5 1
#endif
#if !defined(SUPPORT_BATT_MG_HS_PHEV)
	#define SUPPORT_BATT_MG_HS_PHEV 1
#endif
#if !defined(SUPPORT_BATT_NISSAN_LEAF)
	#define SUPPORT_BATT_NISSAN_LEAF 1
#endif
#if !defined(SUPPORT_BATT_ORION_BMS)
	#define SUPPORT_BATT_ORION_BMS 1
#endif
#if !defined(SUPPORT_BATT_PYLON)
	#define SUPPORT_BATT_PYLON 1
#endif
#if !defined(SUPPORT_BATT_RANGE_ROVER_PHEV)
	#define SUPPORT_BATT_RANGE_ROVER_PHEV 1
#endif
#if !defined(SUPPORT_BATT_RELION)
	#define SUPPORT_BATT_RELION 1
#endif
#if !defined(SUPPORT_BATT_RENAULT_KANGOO)
	#define SUPPORT_BATT_RENAULT_KANGOO 1
#endif
#if !defined(SUPPORT_BATT_RENAULT_TWIZY)
	#define SUPPORT_BATT_RENAULT_TWIZY 1
#endif
#if !defined(SUPPORT_BATT_RENAULT_ZOE1)
	#define SUPPORT_BATT_RENAULT_ZOE1 1
#endif
#if !defined(SUPPORT_BATT_RENAULT_ZOE2)
	#define SUPPORT_BATT_RENAULT_ZOE2 1
#endif
#if !defined(SUPPORT_BATT_RIVIAN)
	#define SUPPORT_BATT_RIVIAN 1
#endif
#if !defined(SUPPORT_BATT_RJXZS_BMS)
	#define SUPPORT_BATT_RJXZS_BMS 1
#endif
#if !defined(SUPPORT_BATT_SAMSUNG_SDI_LV)
	#define SUPPORT_BATT_SAMSUNG_SDI_LV 1
#endif
#if !defined(SUPPORT_BATT_SANTA_FE_PHEV)
	#define SUPPORT_BATT_SANTA_FE_PHEV 1
#endif
#if !defined(SUPPORT_BATT_SIMPBMS)
	#define SUPPORT_BATT_SIMPBMS 1
#endif
#if !defined(SUPPORT_BATT_SONO)
	#define SUPPORT_BATT_SONO 1
#endif
#if !defined(SUPPORT_BATT_TESLA_MODEL3Y)
	#define SUPPORT_BATT_TESLA_MODEL3Y 1
#endif
#if !defined(SUPPORT_BATT_TESLA_MODELSX)
	#define SUPPORT_BATT_TESLA_MODELSX 1
#endif
#if !defined(SUPPORT_BATT_TEST_FAKE)
	#define SUPPORT_BATT_TEST_FAKE 1
#endif
#if !defined(SUPPORT_BATT_VOLVO_SPA)
	#define SUPPORT_BATT_VOLVO_SPA 1
#endif
#if !defined(SUPPORT_BATT_VOLVO_SPA_HYBRID)
	#define SUPPORT_BATT_VOLVO_SPA_HYBRID 1
#endif

class Battery;

// Currently initialized objects for primary and secondary battery.
// Null value indicates that battery is not configured/initialized
extern Battery* battery;
extern Battery* battery2;

void setup_shunt();

#if SUPPORT_BATT_BMW_I3
	#include "BMW-I3-BATTERY.h"
#endif
#if SUPPORT_BATT_BMW_IX
	#include "BMW-IX-BATTERY.h"
#endif
#if SUPPORT_BATT_BMW_PHEV
	#include "BMW-PHEV-BATTERY.h"
#endif
#include "BMW-SBOX.h"
#if SUPPORT_BATT_BOLT_AMPERA
	#include "BOLT-AMPERA-BATTERY.h"
#endif
#if SUPPORT_BATT_BYD_ATTO3
	#include "BYD-ATTO-3-BATTERY.h"
#endif
#if SUPPORT_BATT_CELLPOWER_BMS
	#include "CELLPOWER-BMS.h"
#endif
#if SUPPORT_BATT_CHADEMO
	#include "CHADEMO-BATTERY.h"
#endif
#include "CHADEMO-SHUNTS.h"
#if SUPPORT_BATT_CMFA_EV
	#include "CMFA-EV-BATTERY.h"
#endif
#if SUPPORT_BATT_CMP_SMART_CAR
	#include "CMP-SMART-CAR-BATTERY.h"
#endif
#if SUPPORT_BATT_DALY_BMS
	#include "DALY-BMS.h"
#endif
#if SUPPORT_BATT_ECMP
	#include "ECMP-BATTERY.h"
#endif
#if SUPPORT_BATT_FORD_MACH_E
	#include "FORD-MACH-E-BATTERY.h"
#endif
#if SUPPORT_BATT_FOXESS
	#include "FOXESS-BATTERY.h"
#endif
#if SUPPORT_BATT_GEELY_GEOMETRY_C
	#include "GEELY-GEOMETRY-C-BATTERY.h"
#endif
#if SUPPORT_BATT_HYUNDAI_IONIQ_28
	#include "HYUNDAI-IONIQ-28-BATTERY.h"
#endif
#if SUPPORT_BATT_IMIEV_CZERO_ION
	#include "IMIEV-CZERO-ION-BATTERY.h"
#endif
#if SUPPORT_BATT_JAGUAR_IPACE
	#include "JAGUAR-IPACE-BATTERY.h"
#endif
#if SUPPORT_BATT_KIA_64FD
	#include "KIA-64FD-BATTERY.h"
#endif
#if SUPPORT_BATT_KIA_E_GMP
	#include "KIA-E-GMP-BATTERY.h"
#endif
#if SUPPORT_BATT_KIA_HYUNDAI_64
	#include "KIA-HYUNDAI-64-BATTERY.h"
#endif
#if SUPPORT_BATT_KIA_HYUNDAI_HYBRID
	#include "KIA-HYUNDAI-HYBRID-BATTERY.h"
#endif
#if SUPPORT_BATT_MAXUS_EV80
	#include "MAXUS-EV80-BATTERY.h"
#endif
#if SUPPORT_BATT_MEB
	#include "MEB-BATTERY.h"
#endif
#if SUPPORT_BATT_MG5
	#include "MG-5-BATTERY.h"
#endif
#if SUPPORT_BATT_MG_HS_PHEV
	#include "MG-HS-PHEV-BATTERY.h"
#endif
#if SUPPORT_BATT_NISSAN_LEAF
	#include "NISSAN-LEAF-BATTERY.h"
#endif
#if SUPPORT_BATT_ORION_BMS
	#include "ORION-BMS.h"
#endif
#if SUPPORT_BATT_PYLON
	#include "PYLON-BATTERY.h"
#endif
#if SUPPORT_BATT_RANGE_ROVER_PHEV
	#include "RANGE-ROVER-PHEV-BATTERY.h"
#endif
#if SUPPORT_BATT_RELION
	#include "RELION-LV-BATTERY.h"
#endif
#if SUPPORT_BATT_RENAULT_KANGOO
	#include "RENAULT-KANGOO-BATTERY.h"
#endif
#if SUPPORT_BATT_RENAULT_TWIZY
	#include "RENAULT-TWIZY.h"
#endif
#if SUPPORT_BATT_RENAULT_ZOE1
	#include "RENAULT-ZOE-GEN1-BATTERY.h"
#endif
#if SUPPORT_BATT_RENAULT_ZOE2
	#include "RENAULT-ZOE-GEN2-BATTERY.h"
#endif
#if SUPPORT_BATT_RIVIAN
	#include "RIVIAN-BATTERY.h"
#endif
#if SUPPORT_BATT_RJXZS_BMS
	#include "RJXZS-BMS.h"
#endif
#if SUPPORT_BATT_SAMSUNG_SDI_LV
	#include "SAMSUNG-SDI-LV-BATTERY.h"
#endif
#if SUPPORT_BATT_SANTA_FE_PHEV
	#include "SANTA-FE-PHEV-BATTERY.h"
#endif
#if SUPPORT_BATT_SIMPBMS
	#include "SIMPBMS-BATTERY.h"
#endif
#if SUPPORT_BATT_SONO
	#include "SONO-BATTERY.h"
#endif
#if SUPPORT_BATT_TESLA_MODEL3Y || SUPPORT_BATT_TESLA_MODELSX
	#include "TESLA-BATTERY.h"
#endif
#if SUPPORT_BATT_TEST_FAKE
	#include "TEST-FAKE-BATTERY.h"
#endif
#if SUPPORT_BATT_VOLVO_SPA
	#include "VOLVO-SPA-BATTERY.h"
#endif
#if SUPPORT_BATT_VOLVO_SPA_HYBRID
	#include "VOLVO-SPA-HYBRID-BATTERY.h"
#endif

void setup_battery(void);
Battery* create_battery(BatteryType type);

extern uint16_t user_selected_max_pack_voltage_dV;
extern uint16_t user_selected_min_pack_voltage_dV;
extern uint16_t user_selected_max_cell_voltage_mV;
extern uint16_t user_selected_min_cell_voltage_mV;
extern bool user_selected_use_estimated_SOC;
extern bool user_selected_LEAF_interlock_mandatory;
extern bool user_selected_tesla_digital_HVIL;
extern uint16_t user_selected_tesla_GTW_country;
extern bool user_selected_tesla_GTW_rightHandDrive;
extern uint16_t user_selected_tesla_GTW_mapRegion;
extern uint16_t user_selected_tesla_GTW_chassisType;
extern uint16_t user_selected_tesla_GTW_packEnergy;

#endif
