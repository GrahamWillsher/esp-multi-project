/**
 * @file datalayer_extended.h
 * @brief Compatibility shim to canonical Battery Emulator extended datalayer
 *
 * Canonical owner:
 *   src/battery_emulator/datalayer/datalayer_extended.h
 *
 * This local wrapper exists only to preserve include stability for legacy
 * `src/datalayer/*` include paths while avoiding duplicated struct definitions.
 */

#ifndef DATALAYER_EXTENDED_SHIM_H_
#define DATALAYER_EXTENDED_SHIM_H_

#include "../battery_emulator/datalayer/datalayer_extended.h"

#if 0  // Legacy duplicated body retained only as archived reference; excluded from build.

  bool CrashMemorized = false;  //mysteryvan parameters
  bool InterlockOpen = false;
  bool UserRequestContactorReset = false;
  bool UserRequestCollisionReset = false;
  bool UserRequestIsolationReset = false;
  bool UserRequestDisableIsoMonitoring = false;
  bool ALERT_CELL_POOR_CONSIST = false;  //mysteryvan parameters
  bool ALERT_OVERCHARGE = false;         //mysteryvan parameters
  bool ALERT_BATT = false;               //mysteryvan parameters
  bool ALERT_LOW_SOC = false;            //mysteryvan parameters
  bool ALERT_HIGH_SOC = false;           //mysteryvan parameters
  bool ALERT_SOC_JUMP = false;           //mysteryvan parameters
  bool ALERT_TEMP_DIFF = false;          //mysteryvan parameters
  bool ALERT_HIGH_TEMP = false;          //mysteryvan parameters
  bool ALERT_OVERVOLTAGE = false;        //mysteryvan parameters
  bool ALERT_CELL_OVERVOLTAGE = false;   //mysteryvan parameters
  bool ALERT_CELL_UNDERVOLTAGE = false;  //mysteryvan parameters

  uint8_t pid_battery_serial[13] = {0};
};

struct DATALAYER_INFO_GEELY_GEOMETRY_C {
  /** int16_t */
  /** Module temperatures 1-6 */
  int16_t ModuleTemperatures[6] = {0};

  /** uint8_t */
  /** Battery software/hardware/serial versions, stores raw HEX values for ASCII chars */
  uint8_t BatterySoftwareVersion[16] = {0};
  uint8_t BatteryHardwareVersion[16] = {0};
  uint8_t BatterySerialNumber[28] = {0};

  /** uint16_t */
  /** Various values polled via OBD2 PIDs */
  uint16_t soc = 0;
  uint16_t CC2voltage = 0;
  uint16_t cellMaxVoltageNumber = 0;
  uint16_t cellMinVoltageNumber = 0;
  uint16_t cellTotalAmount = 0;
  uint16_t specificialVoltage = 0;
  uint16_t unknown1 = 0;
  uint16_t rawSOCmax = 0;
  uint16_t rawSOCmin = 0;
  uint16_t unknown4 = 0;
  uint16_t capModMax = 0;
  uint16_t capModMin = 0;
  uint16_t unknown7 = 0;
  uint16_t unknown8 = 0;
};

struct DATALAYER_INFO_KIAHYUNDAI64 {
  uint32_t cumulative_charge_current_ah = 0;
  uint32_t cumulative_discharge_current_ah = 0;
  uint32_t cumulative_energy_charged_kWh = 0;
  uint32_t cumulative_energy_discharged_kWh = 0;
  uint32_t powered_on_total_time = 0;

  uint16_t inverterVoltage = 0;
  uint16_t isolation_resistance_kOhm = 0;
  uint16_t number_of_standard_charging_sessions = 0;
  uint16_t number_of_fastcharging_sessions = 0;
  uint16_t accumulated_normal_charging_energy_kWh = 0;
  uint16_t accumulated_fastcharging_energy_kWh = 0;
  uint16_t battery_12V = 0;

  int8_t temperature_water_inlet = 0;
  int8_t powerRelayTemperature = 0;

  uint8_t total_cell_count = 0;
  uint8_t waterleakageSensor = 0;
  uint8_t batteryManagementMode = 0;
  uint8_t BMS_ign = 0;
  uint8_t batteryRelay = 0;

  uint8_t ecu_serial_number[16] = {0};
  uint8_t ecu_version_number[16] = {0};
};

struct DATALAYER_INFO_TESLA {
  uint64_t BMS_info_bootGitHash = 0;
  uint64_t PCS_info_bootGitHash = 0;
  uint64_t HVP_info_bootGitHash = 0;

  uint32_t HVP_info_bootCrc = 0;
  uint32_t HVP_info_appCrc = 0;
  uint32_t PCS_info_appCrc = 0;
  uint32_t PCS_info_cpu2AppCrc = 0;
  uint32_t PCS_info_bootCrc = 0;
  uint32_t PCS_dcdc12vSupportLifetimekWh = 0;
  uint32_t BMS_info_appCrc = 0;
  uint32_t BMS_info_bootCrc = 0;
  uint32_t battery_packMass = 0;
  uint32_t battery_platformMaxBusVoltage = 0;
  uint32_t BMS_min_voltage = 0;
  uint32_t BMS_max_voltage = 0;
  uint32_t battery_max_charge_current = 0;
  uint32_t battery_max_discharge_current = 0;
  uint32_t battery_soc_min = 0;
  uint32_t battery_soc_max = 0;
  uint32_t battery_soc_ave = 0;
  uint32_t battery_soc_ui = 0;

  uint16_t BMS_info_buildConfigId = 0;
  uint16_t BMS_info_hardwareId = 0;
  uint16_t BMS_info_componentId = 0;
  uint16_t BMS_info_usageId = 0;
  uint16_t BMS_info_subUsageId = 0;
  uint16_t battery_dcdcLvBusVolt = 0;
  uint16_t battery_dcdcHvBusVolt = 0;
  uint16_t battery_dcdcLvOutputCurrent = 0;
  uint16_t battery_nominal_full_pack_energy = 0;
  uint16_t battery_nominal_full_pack_energy_m0 = 0;
  uint16_t battery_nominal_energy_remaining = 0;
  uint16_t battery_nominal_energy_remaining_m0 = 0;
  uint16_t battery_ideal_energy_remaining = 0;
  uint16_t battery_ideal_energy_remaining_m0 = 0;
  uint16_t battery_energy_to_charge_complete = 0;
  uint16_t battery_energy_to_charge_complete_m1 = 0;
  uint16_t battery_energy_buffer = 0;
  uint16_t battery_energy_buffer_m1 = 0;
  uint16_t battery_expected_energy_remaining = 0;
  uint16_t battery_expected_energy_remaining_m1 = 0;
  uint16_t battery_BrickVoltageMax = 0;
  uint16_t battery_BrickVoltageMin = 0;
  uint16_t HVP_hvp1v5Ref = 0;
  uint16_t HVP_shuntCurrentDebug = 0;
  uint16_t PCS_dcdcTemp = 0;
  uint16_t PCS_ambientTemp = 0;
  uint16_t PCS_chgPhATemp = 0;
  uint16_t PCS_chgPhBTemp = 0;
  uint16_t PCS_chgPhCTemp = 0;
  uint16_t PCS_dcdcMaxLvOutputCurrent = 0;
  uint16_t PCS_dcdcCurrentLimit = 0;
  uint16_t PCS_dcdcLvOutputCurrentTempLimit = 0;
  uint16_t PCS_dcdcUnifiedCommand = 0;
  uint16_t PCS_dcdcCLAControllerOutput = 0;
  uint16_t PCS_dcdcTankVoltage = 0;
  uint16_t PCS_dcdcTankVoltageTarget = 0;
  uint16_t PCS_dcdcClaCurrentFreq = 0;
  uint16_t PCS_dcdcTCommMeasured = 0;
  uint16_t PCS_dcdcShortTimeUs = 0;
  uint16_t PCS_dcdcHalfPeriodUs = 0;
  uint16_t PCS_dcdcIntervalMaxFrequency = 0;
  uint16_t PCS_dcdcIntervalMaxHvBusVolt = 0;
  uint16_t PCS_dcdcIntervalMaxLvBusVolt = 0;
  uint16_t PCS_dcdcIntervalMaxLvOutputCurr = 0;
  uint16_t PCS_dcdcIntervalMinFrequency = 0;
  uint16_t PCS_dcdcIntervalMinHvBusVolt = 0;
  uint16_t PCS_dcdcIntervalMinLvBusVolt = 0;
  uint16_t PCS_dcdcIntervalMinLvOutputCurr = 0;
  uint16_t battery_packConfigMultiplexer = 0;
  uint16_t battery_moduleType = 0;
  uint16_t battery_reservedConfig = 0;
  uint16_t BMS_isolationResistance = 0;
  uint16_t BMS_chgPowerAvailable = 0;
  uint16_t BMS_maxRegenPower = 0;
  uint16_t BMS_maxDischargePower = 0;
  uint16_t BMS_maxStationaryHeatPower = 0;
  uint16_t BMS_hvacPowerBudget = 0;
  uint16_t BMS_powerDissipation = 0;
  uint16_t BMS_inletActiveCoolTargetT = 0;
  uint16_t BMS_inletPassiveTargetT = 0;
  uint16_t BMS_inletActiveHeatTargetT = 0;
  uint16_t BMS_packTMin = 0;
  uint16_t BMS_packTMax = 0;
  uint16_t PCS_info_buildConfigId = 0;
  uint16_t PCS_info_hardwareId = 0;
  uint16_t PCS_info_componentId = 0;
  uint16_t PCS_dcdcMaxOutputCurrentAllowed = 0;
  uint16_t PCS_info_usageId = 0;
  uint16_t PCS_info_subUsageId = 0;
  uint16_t HVP_dcLinkVoltage = 0;
  uint16_t HVP_packVoltage = 0;
  uint16_t HVP_fcLinkVoltage = 0;
  uint16_t HVP_packContVoltage = 0;
  uint16_t HVP_packNegativeV = 0;
  uint16_t HVP_packPositiveV = 0;
  uint16_t HVP_pyroAnalog = 0;
  uint16_t HVP_dcLinkNegativeV = 0;
  uint16_t HVP_dcLinkPositiveV = 0;
  uint16_t HVP_fcLinkNegativeV = 0;
  uint16_t HVP_fcContCoilCurrent = 0;
  uint16_t HVP_fcContVoltage = 0;
  uint16_t HVP_hvilInVoltage = 0;
  uint16_t HVP_hvilOutVoltage = 0;
  uint16_t HVP_fcLinkPositiveV = 0;
  uint16_t HVP_packContCoilCurrent = 0;
  uint16_t HVP_battery12V = 0;
  uint16_t HVP_shuntRefVoltageDbg = 0;
  uint16_t HVP_shuntAuxCurrentDbg = 0;
  uint16_t HVP_shuntBarTempDbg = 0;
  uint16_t HVP_shuntAsicTempDbg = 0;
  uint16_t HVP_info_buildConfigId = 0;
  uint16_t HVP_info_hardwareId = 0;
  uint16_t HVP_info_componentId = 0;
  uint16_t HVP_info_usageId = 0;
  uint16_t HVP_info_subUsageId = 0;

  uint8_t hvil_status = 0;
  uint8_t packContNegativeState = 0;
  uint8_t packContPositiveState = 0;
  uint8_t packContactorSetState = 0;
  uint8_t battery_packCtrsRequestStatus = 0;
  uint8_t BMS_info_pcbaId = 0;
  uint8_t BMS_info_assemblyId = 0;
  uint8_t BMS_info_platformType = 0;
  uint8_t BMS_info_bootUdsProtoVersion = 0;
  uint8_t battery_beginning_of_life = 0;
  uint8_t battery_battTempPct = 0;
  uint8_t battery_BrickVoltageMaxNum = 0;
  uint8_t battery_BrickVoltageMinNum = 0;
  uint8_t battery_BrickTempMaxNum = 0;
  uint8_t battery_BrickTempMinNum = 0;
  uint8_t battery_BrickModelTMax = 0;
  uint8_t battery_BrickModelTMin = 0;
  uint8_t BMS_flowRequest = 0;
  uint8_t BMS_uiChargeStatus = 0;
  uint8_t BMS_contactorState = 0;
  uint8_t BMS_state = 0;
  uint8_t BMS_hvState = 0;
  uint8_t BMS_notEnoughPowerForHeatPump = 0;
  uint8_t BMS_powerLimitState = 0;
  uint8_t BMS_inverterTQF = 0;
  uint8_t PCS_dcdcPrechargeStatus = 0;
  uint8_t PCS_dcdc12VSupportStatus = 0;
  uint8_t PCS_dcdcHvBusDischargeStatus = 0;
  uint8_t PCS_dcdcMainState = 0;
  uint8_t PCS_dcdcSubState = 0;
  uint8_t PCS_dcdcPrechargeRtyCnt = 0;
  uint8_t PCS_dcdc12VSupportRtyCnt = 0;
  uint8_t PCS_dcdcDischargeRtyCnt = 0;
  uint8_t PCS_dcdcPwmEnableLine = 0;
  uint8_t PCS_dcdcSupportingFixedLvTarget = 0;
  uint8_t PCS_dcdcPrechargeRestartCnt = 0;
  uint8_t PCS_dcdcInitialPrechargeSubState = 0;
  uint8_t PCS_info_pcbaId = 0;
  uint8_t PCS_info_assemblyId = 0;
  uint8_t PCS_info_platformType = 0;
  uint8_t PCS_info_bootUdsProtoVersion = 0;
  uint8_t HVP_info_platformType = 0;
  uint8_t HVP_info_pcbaId = 0;
  uint8_t HVP_info_assemblyId = 0;
  uint8_t HVP_info_bootUdsProtoVersion = 0;
  uint8_t HVP_shuntHwMia = 0;
  uint8_t HVP_shuntAuxCurrentStatus = 0;
  uint8_t HVP_shuntBarTempStatus = 0;
  uint8_t HVP_shuntAsicTempStatus = 0;

  bool packCtrsClosingBlocked = false;
  bool pyroTestInProgress = false;
  bool battery_packCtrsOpenNowRequested = false;
  bool battery_packCtrsOpenRequested = false;
  bool battery_packCtrsResetRequestRequired = false;
  bool battery_dcLinkAllowedToEnergize = false;
  bool BMS352_mux = false;  // variable to store when 0x352 mux is present
  bool battery_full_charge_complete = false;
  bool battery_fully_charged = false;
  bool BMS_hvilFault = false;
  bool BMS_diLimpRequest = false;
  bool BMS_pcsPwmEnabled = false;
  bool BMS_pcsNoFlowRequest = false;
  bool BMS_noFlowRequest = false;
  bool PCS_dcdcFaulted = false;
  bool PCS_dcdcOutputIsLimited = false;
  bool HVP_gpioPassivePyroDepl = false;
  bool HVP_gpioPyroIsoEn = false;
  bool HVP_gpioCpFaultIn = false;
  bool HVP_gpioPackContPowerEn = false;
  bool HVP_gpioHvCablesOk = false;
  bool HVP_gpioHvpSelfEnable = false;
  bool HVP_gpioLed = false;
  bool HVP_gpioCrashSignal = false;
  bool HVP_gpioShuntDataReady = false;
  bool HVP_gpioFcContPosAux = false;
  bool HVP_gpioFcContNegAux = false;
  bool HVP_gpioBmsEout = false;
  bool HVP_gpioCpFaultOut = false;
  bool HVP_gpioPyroPor = false;
  bool HVP_gpioShuntEn = false;
  bool HVP_gpioHvpVerEn = false;
  bool HVP_gpioPackCoontPosFlywheel = false;
  bool HVP_gpioCpLatchEnable = false;
  bool HVP_gpioPcsEnable = false;
  bool HVP_gpioPcsDcdcPwmEnable = false;
  bool HVP_gpioPcsChargePwmEnable = false;
  bool HVP_gpioFcContPowerEnable = false;
  bool HVP_gpioHvilEnable = false;
  bool HVP_gpioSecDrdy = false;
  bool HVP_packCurrentMia = false;
  bool HVP_auxCurrentMia = false;
  bool HVP_currentSenseMia = false;
  bool HVP_shuntRefVoltageMismatch = false;
  bool HVP_shuntThermistorMia = false;

  uint8_t BMS_partNumber[12] = {0};        //stores raw HEX values for ASCII chars
  uint8_t battery_serialNumber[15] = {0};  //stores raw HEX values for ASCII chars
  uint8_t battery_partNumber[12] = {0};    //stores raw HEX values for ASCII chars
  uint8_t PCS_partNumber[13] = {0};        //stores raw HEX values for ASCII chars
  uint8_t HVP_partNumber[13] = {0};        //stores raw HEX values for ASCII chars
  char* battery_manufactureDate;
};

struct DATALAYER_INFO_NISSAN_LEAF {
  /** Cryptographic challenge to be solved */
  uint32_t CryptoChallenge = 0;
  /** Solution for crypto challenge, MSBs */
  uint32_t SolvedChallengeMSB = 0;
  /** Solution for crypto challenge, LSBs */
  uint32_t SolvedChallengeLSB = 0;

  /** 77Wh per gid. LEAF specific unit */
  uint16_t GIDS = 0;
  /** Max regen power in kW */
  uint16_t ChargePowerLimit = 0;
  /** Internal resistance in percentage */
  uint16_t battery_HX = 0;
  /** Insulation resistance, most likely kOhm */
  uint16_t Insulation = 0;

  /** Max charge power in kW */
  int16_t MaxPowerForCharger = 0;
  /** Temperature sensoros 1-4 */
  int16_t temperature1 = 0;
  int16_t temperature2 = 0;
  int16_t temperature3 = 0;  // This sensor not available on 2013+ packs
  int16_t temperature4 = 0;

  /** Enum, ZE0 = 0, AZE0 = 1, ZE1 = 2 */
  uint8_t LEAF_gen = 0;
  /** battery_FAIL status */
  uint8_t RelayCutRequest = 0;
  /** battery_STATUS status */
  uint8_t FailsafeStatus = 0;

  /** Interlock status */
  bool Interlock = false;
  /** True if fully charged */
  bool Full = false;
  /** True if battery empty */
  bool Empty = false;
  /** Battery pack allows closing of contacors */
  bool MainRelayOn = false;
  /** True if heater exists */
  bool HeatExist = false;
  /** Heater stopped */
  bool HeatingStop = false;
  /** Heater starting */
  bool HeatingStart = false;
  /** Heat request sent*/
  bool HeaterSendRequest = false;
  /** User requesting SOH reset via WebUI*/
  bool UserRequestSOHreset = false;
  /** True if the crypto challenge response from BMS is signalling a failed attempt*/
  bool challengeFailed = false;

  /** Battery info, stores raw HEX values for ASCII chars */
  uint8_t BatterySerialNumber[15] = {0};
  uint8_t BatteryPartNumber[7] = {0};
  uint8_t BMSIDcode[8] = {0};
};

struct DATALAYER_INFO_MEB {
  /** Isolation resistance in kOhm */
  uint32_t isolation_resistance = 0;
  int32_t BMS_voltage_intermediate_dV = 0;
  int32_t BMS_voltage_dV = 0;

  uint16_t battery_temperature_dC = 0;

  /** All realtime_ warnings have same enumeration, 0 = no fault, 1 = error level 1, 2 error level 2, 3 error level 3 */
  uint8_t rt_overcurrent = 0;
  uint8_t rt_CAN_fault = 0;
  uint8_t rt_overcharge = 0;
  uint8_t rt_SOC_high = 0;
  uint8_t rt_SOC_low = 0;
  uint8_t rt_SOC_jumping = 0;
  uint8_t rt_temp_difference = 0;
  uint8_t rt_cell_overtemp = 0;
  uint8_t rt_cell_undertemp = 0;
  uint8_t rt_battery_overvolt = 0;
  uint8_t rt_battery_undervol = 0;
  uint8_t rt_cell_overvolt = 0;
  uint8_t rt_cell_undervol = 0;
  uint8_t rt_cell_imbalance = 0;
  uint8_t rt_battery_unathorized = 0;
  /** HVIL status, 0 = Init, 1 = Closed, 2 = Open!, 3 = Fault */
  uint8_t HVIL = 0;
  /** 0 = HV inactive, 1 = HV active, 2 = Balancing, 3 = Extern charging, 4 = AC charging, 5 = Battery error, 6 = DC charging, 7 = init */
  uint8_t BMS_mode = 0;
  /** 1 = Battery display, 4 = Battery display OK, 4 = Display battery charging, 6 = Display battery check, 7 = Fault */
  uint8_t battery_diagnostic = 0;
  /** 0 = init, 1 = no open HV line detected, 2 = open HV line , 3 = fault */
  uint8_t status_HV_line = 0;
  /** 0 = OK, 1 = Not OK, 0x06 = init, 0x07 = fault */
  uint8_t warning_support = 0;
  /** 0=Init, 1=BMS intermediate circuit voltage-free (U_Zwkr < 20V), 2=BMS intermediate circuit not voltage-free (U_Zwkr >/= 25V, hysteresis), 3=Error */
  uint8_t BMS_status_voltage_free = 0;
  /** 0 Component_IO, 1 Restricted_CompFkt_Isoerror_I, 2 Restricted_CompFkt_Isoerror_II, 3 Restricted_CompFkt_Interlock, 4 Restricted_CompFkt_SD, 5 Restricted_CompFkt_Performance red, 6 = No component function, 7 = Init */
  uint8_t BMS_error_status = 0;
  /** 0 init, 1 closed, 2 open, 3 fault */
  uint8_t BMS_Kl30c_Status = 0;
  uint8_t balancing_active = 0;
  uint8_t BMS_welded_contactors_status = 0;

  bool balancing_request = 0;
  bool charging_active = 0;
  bool BMS_OBD_MIL = 0; /** true if BMS requests error/warning light */
  bool BMS_error_lamp_req = 0;
  bool BMS_warning_lamp_req = 0;
  bool BMS_fault_performance = false;  //Error: Battery performance is limited (e.g. due to sensor or fan failure)
  bool BMS_fault_emergency_shutdown_crash =
      false;  //Error: Safety-critical error (crash detection) Battery contactors are already opened / will be opened immediately Signal is read directly by the EMS and initiates an AKS of the PWR and an active discharge of the DC link
  bool BMS_error_shutdown_request =
      false;  // Fault: Fault condition, requires battery contactors to be opened internal battery error; Advance notification of an impending opening of the battery contactors by the BMS
  bool BMS_error_shutdown =
      false;  // Fault: Fault condition, requires battery contactors to be opened Internal battery error, battery contactors opened without notice by the BMS
  bool SDSW = 0;                /** Service disconnect switch status */
  bool pilotline = 0;           /** Pilotline status */
  bool transportmode = 0;       /** Transportation mode status */
  bool componentprotection = 0; /** Componentprotection mode status */
  bool shutdown_active = 0;     /** Shutdown status */
  bool battery_heating = 0;     /** Battery heating status */

  float temp_points[18] = {0};
  uint16_t celltemperature_dC[56] = {0};
};

struct DATALAYER_INFO_VOLVO_POLESTAR {
  uint16_t soc_bms = 0;
  uint16_t soc_calc = 0;
  uint16_t soc_rescaled = 0;
  uint16_t soh_bms = 0;
  uint16_t BECMsupplyVoltage = 12000;
  uint16_t BECMBatteryVoltage = 0;
  uint16_t BECMUDynMaxLim = 0;
  uint16_t BECMUDynMinLim = 0;
  uint16_t HvBattPwrLimDcha1 = 0;
  uint16_t HvBattPwrLimDchaSoft = 0;
  uint16_t HvBattPwrLimDchaSlowAgi = 0;
  uint16_t HvBattPwrLimChrgSlowAgi = 0;

  int16_t BECMBatteryCurrent = 0;

  uint8_t HVSysRlySts = 0;
  uint8_t HVSysDCRlySts1 = 0;
  uint8_t HVSysDCRlySts2 = 0;
  uint8_t HVSysIsoRMonrSts = 0;
  uint8_t DTCcount = 0;
  uint8_t HVILstatusBits = 0;
  /** User requesting DTC reset via WebUI*/
  bool UserRequestDTCreset = false;
  /** User requesting DTC readout via WebUI*/
  bool UserRequestDTCreadout = false;
  /** User requesting BECM reset via WebUI*/
  bool UserRequestBECMecuReset = false;
};

struct DATALAYER_INFO_VOLVO_HYBRID {
  uint16_t soc_bms = 0;
  uint16_t soc_calc = 0;
  uint16_t soc_rescaled = 0;
  uint16_t soh_bms = 0;
  uint16_t BECMsupplyVoltage = 0;

  uint16_t BECMBatteryVoltage = 0;
  uint16_t BECMBatteryCurrent = 0;
  uint16_t BECMUDynMaxLim = 0;
  uint16_t BECMUDynMinLim = 0;

  uint16_t HvBattPwrLimDcha1 = 0;
  uint16_t HvBattPwrLimDchaSoft = 0;
  //uint16_t HvBattPwrLimDchaSlowAgi = 0;
  //uint16_t HvBattPwrLimChrgSlowAgi = 0;

  uint8_t HVSysRlySts = 0;
  uint8_t HVSysDCRlySts1 = 0;
  uint8_t HVSysDCRlySts2 = 0;
  uint8_t HVSysIsoRMonrSts = 0;
  /** User requesting DTC reset via WebUI*/
  bool UserRequestDTCreset = false;
  /** User requesting DTC readout via WebUI*/
  bool UserRequestDTCreadout = false;
  /** User requesting BECM reset via WebUI*/
  bool UserRequestBECMecuReset = false;
};

struct DATALAYER_INFO_ZOE {
  uint16_t mileage_km = 0;
  uint16_t alltime_kWh = 0;

  uint8_t CUV = 0;
  uint8_t HVBIR = 0;
  uint8_t HVBUV = 0;
  uint8_t EOCR = 0;
  uint8_t HVBOC = 0;
  uint8_t HVBOT = 0;
  uint8_t HVBOV = 0;
  uint8_t COV = 0;
};

struct DATALAYER_INFO_ZOE_PH2 {
  /** uint16_t */
  uint16_t battery_soc = 0;
  uint16_t battery_usable_soc = 0;
  uint16_t battery_soh = 0;
  uint16_t battery_pack_voltage = 0;
  uint16_t battery_max_cell_voltage = 0;
  uint16_t battery_min_cell_voltage = 0;
  uint16_t battery_12v = 0;
  uint16_t battery_avg_temp = 0;
  uint16_t battery_min_temp = 0;
  uint16_t battery_max_temp = 0;
  uint16_t battery_max_power = 0;
  uint16_t battery_interlock = 0;
  uint16_t battery_kwh = 0;
  uint16_t battery_current = 0;
  uint16_t battery_current_offset = 0;
  uint16_t battery_max_generated = 0;
  uint16_t battery_max_available = 0;
  uint16_t battery_current_voltage = 0;
  uint16_t battery_charging_status = 0;
  uint16_t battery_remaining_charge = 0;
  uint16_t battery_balance_capacity_total = 0;
  uint16_t battery_balance_time_total = 0;
  uint16_t battery_balance_capacity_sleep = 0;
  uint16_t battery_balance_time_sleep = 0;
  uint16_t battery_balance_capacity_wake = 0;
  uint16_t battery_balance_time_wake = 0;
  uint16_t battery_bms_state = 0;
  uint16_t battery_energy_complete = 0;
  uint16_t battery_energy_partial = 0;
  uint16_t battery_slave_failures = 0;
  uint16_t battery_mileage = 0;
  uint16_t battery_fan_speed = 0;
  uint16_t battery_fan_period = 0;
  uint16_t battery_fan_control = 0;
  uint16_t battery_fan_duty = 0;
  uint16_t battery_temporisation = 0;
  uint16_t battery_time = 0;
  uint16_t battery_pack_time = 0;
  uint16_t battery_soc_min = 0;
  uint16_t battery_soc_max = 0;
  /** User requesting NVROL reset via WebUI*/
  bool UserRequestNVROLReset = false;
};

class DataLayerExtended {
 public:
  DATALAYER_INFO_BOLTAMPERA boltampera;
  DATALAYER_INFO_BMWPHEV bmwphev;
  DATALAYER_INFO_BMWIX bmwix;
  DATALAYER_INFO_BYDATTO3 bydAtto3;
  DATALAYER_INFO_CELLPOWER cellpower;
  DATALAYER_INFO_CHADEMO chademo;
  DATALAYER_INFO_CMFAEV CMFAEV;
  DATALAYER_INFO_CMPSMART stellantisCMPsmart;
  DATALAYER_INFO_ECMP stellantisECMP;
  DATALAYER_INFO_GEELY_GEOMETRY_C geometryC;
  DATALAYER_INFO_KIAHYUNDAI64 KiaHyundai64;
  DATALAYER_INFO_KIAHYUNDAI64 KiaHyundai64_2;
  DATALAYER_INFO_TESLA tesla;
  DATALAYER_INFO_NISSAN_LEAF nissanleaf;
  DATALAYER_INFO_MEB meb;
  DATALAYER_INFO_VOLVO_POLESTAR VolvoPolestar;
  DATALAYER_INFO_VOLVO_HYBRID VolvoHybrid;
  DATALAYER_INFO_ZOE zoe;
  DATALAYER_INFO_ZOE_PH2 zoePH2;
};

extern DataLayerExtended datalayer_extended;

#endif

#endif
