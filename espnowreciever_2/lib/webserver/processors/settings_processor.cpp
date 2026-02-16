#include "webserver.h"
#include "../utils/transmitter_manager.h"
#include <Arduino.h>

// Mock settings store instance (kept for local receiver settings)
static MockSettingsStore mockSettings;

String settings_processor(const String& var) {
    // Common placeholders from settings page (local receiver settings - still use mock)
    if (var == "SAVEDCLASS") return "hidden";
    if (var == "SSID") return mockSettings.getString("SSID", "");
    if (var == "PASSWORD") return mockSettings.getString("PASSWORD", "");
    if (var == "HOSTNAME") return mockSettings.getString("HOSTNAME", "ESP32-Receiver");
    
    // Battery settings (from transmitter config)
    if (var == "BATTTYPE") return "<option value='0'>No Battery Selected</option>";
    if (var == "BATTCOMM") return "<option value='0'>No Interface</option>";
    if (var == "BATTCHEM") return "<option value='0'>Unknown</option>";
    if (var == "BATT2COMM") return "<option value='0'>No Interface</option>";
    
    // Inverter settings (from transmitter config)
    if (var == "INVTYPE") return "<option value='0'>No Inverter Selected</option>";
    if (var == "INVCOMM") return "<option value='0'>No Interface</option>";
    
    // Charger settings  
    if (var == "CHGTYPE") return "<option value='0'>No Charger Selected</option>";
    if (var == "CHGCOMM") return "<option value='0'>No Interface</option>";
    
    // Network settings (local receiver - still use mock)
    if (var == "WIFICHANNEL") return mockSettings.getString("WIFICHANNEL", "0");
    if (var == "APNAME") return mockSettings.getString("APNAME", "ESP32-AP");
    if (var == "APPASSWORD") return mockSettings.getString("APPASSWORD", "");
    
    // Power settings (from transmitter cache)
    if (var == "CHGPOWER") return TransmitterManager::hasPowerSettings() ? String(TransmitterManager::getPowerSettings().charge_w) : "0";
    if (var == "DCHGPOWER") return TransmitterManager::hasPowerSettings() ? String(TransmitterManager::getPowerSettings().discharge_w) : "0";
    
    // Voltage settings (from transmitter cache)
    if (var == "BATTPVMAX") return TransmitterManager::hasBatteryEmulatorSettings() ? String(TransmitterManager::getBatteryEmulatorSettings().pack_max_voltage_dV / 10.0f, 1) : "0.0";
    if (var == "BATTPVMIN") return TransmitterManager::hasBatteryEmulatorSettings() ? String(TransmitterManager::getBatteryEmulatorSettings().pack_min_voltage_dV / 10.0f, 1) : "0.0";
    if (var == "BATTCVMAX") return TransmitterManager::hasBatteryEmulatorSettings() ? String(TransmitterManager::getBatteryEmulatorSettings().cell_max_voltage_mV) : "0";
    if (var == "BATTCVMIN") return TransmitterManager::hasBatteryEmulatorSettings() ? String(TransmitterManager::getBatteryEmulatorSettings().cell_min_voltage_mV) : "0";
    
    // IP settings (now handled via JavaScript - these are just placeholders)
    if (var == "LOCALIP1") return mockSettings.getString("LOCALIP1", "0");
    if (var == "LOCALIP2") return mockSettings.getString("LOCALIP2", "0");
    if (var == "LOCALIP3") return mockSettings.getString("LOCALIP3", "0");
    if (var == "LOCALIP4") return mockSettings.getString("LOCALIP4", "0");
    if (var == "GATEWAY1") return mockSettings.getString("GATEWAY1", "0");
    if (var == "GATEWAY2") return mockSettings.getString("GATEWAY2", "0");
    if (var == "GATEWAY3") return mockSettings.getString("GATEWAY3", "0");
    if (var == "GATEWAY4") return mockSettings.getString("GATEWAY4", "0");
    if (var == "SUBNET1") return mockSettings.getString("SUBNET1", "0");
    if (var == "SUBNET2") return mockSettings.getString("SUBNET2", "0");
    if (var == "SUBNET3") return mockSettings.getString("SUBNET3", "0");
    if (var == "SUBNET4") return mockSettings.getString("SUBNET4", "0");
    
    // MQTT settings (placeholder defaults)
    if (var == "MQTTSERVER") return "";
    if (var == "MQTTUSER") return "";
    if (var == "MQTTPASSWORD") return "";
    if (var == "MQTTPORT") return "1883";
    if (var == "MQTTTOPIC") return mockSettings.getString("MQTTTOPIC", "");
    if (var == "MQTTTIMEOUT") return mockSettings.getString("MQTTTIMEOUT", "2000");
    if (var == "MQTTOBJIDPREFIX") return mockSettings.getString("MQTTOBJIDPREFIX", "");
    if (var == "MQTTDEVICENAME") return mockSettings.getString("MQTTDEVICENAME", "");
    if (var == "HADEVICEID") return mockSettings.getString("HADEVICEID", "");
    
    // Boolean checkboxes - return "checked" or ""
    if (var == "DBLBTR") return (TransmitterManager::hasBatteryEmulatorSettings() && TransmitterManager::getBatteryEmulatorSettings().double_battery) ? "checked" : "";
    if (var == "SOCESTIMATED") return (TransmitterManager::hasBatteryEmulatorSettings() && TransmitterManager::getBatteryEmulatorSettings().soc_estimated) ? "checked" : "";
    if (var == "CNTCTRL") return TransmitterManager::hasContactorSettings() && TransmitterManager::getContactorSettings().control_enabled ? "checked" : "";
    if (var == "NCCONTACTOR") return TransmitterManager::hasContactorSettings() && TransmitterManager::getContactorSettings().nc_contactor ? "checked" : "";
    if (var == "WIFIAPENABLED") return mockSettings.getBool("WIFIAPENABLED") ? "checked" : "";
    if (var == "STATICIP") return mockSettings.getBool("STATICIP") ? "checked" : "";
    if (var == "WEBENABLED") return mockSettings.getBool("WEBENABLED", true) ? "checked" : "checked";
    if (var == "INTERLOCKREQ") return mockSettings.getBool("INTERLOCKREQ") ? "checked" : "";
    if (var == "DIGITALHVIL") return mockSettings.getBool("DIGITALHVIL") ? "checked" : "";
    if (var == "GTWRHD") return mockSettings.getBool("GTWRHD") ? "checked" : "";
    
    // Numeric values (from transmitter config where applicable)
    if (var == "MAXPRETIME") return TransmitterManager::hasPowerSettings() ? String(TransmitterManager::getPowerSettings().max_precharge_ms) : "0";
    if (var == "PRECHGMS") return TransmitterManager::hasPowerSettings() ? String(TransmitterManager::getPowerSettings().precharge_duration_ms) : "0";
    if (var == "CANFREQ") return TransmitterManager::hasCanSettings() ? String(TransmitterManager::getCanSettings().frequency_khz) : "0";
    if (var == "CANFDFREQ") return TransmitterManager::hasCanSettings() ? String(TransmitterManager::getCanSettings().fd_frequency_mhz) : "0";
    if (var == "LEDMODE") return "<option value='0'>Default</option>";
    
    // Tesla-specific
    if (var == "GTWCOUNTRY") return "<option value='0'>Not Set</option>";
    if (var == "GTWMAPREG") return "<option value='0'>Not Set</option>";
    if (var == "GTWCHASSIS") return "<option value='0'>Not Set</option>";
    if (var == "GTWPACK") return "<option value='0'>Not Set</option>";
    
    // Inverter specific (from transmitter config)
    if (var == "INVCELLS") return TransmitterManager::hasInverterSettings() ? String(TransmitterManager::getInverterSettings().cells) : "0";
    if (var == "INVMODULES") return TransmitterManager::hasInverterSettings() ? String(TransmitterManager::getInverterSettings().modules) : "0";
    if (var == "INVCELLSPER") return TransmitterManager::hasInverterSettings() ? String(TransmitterManager::getInverterSettings().cells_per_module) : "0";
    if (var == "INVVLEVEL") return TransmitterManager::hasInverterSettings() ? String(TransmitterManager::getInverterSettings().voltage_level) : "0";
    if (var == "INVCAPACITY") return TransmitterManager::hasInverterSettings() ? String(TransmitterManager::getInverterSettings().capacity_ah) : "0";
    if (var == "INVBTYPE") return TransmitterManager::hasInverterSettings() ? String(TransmitterManager::getInverterSettings().battery_type) : "0";
    if (var == "SOFAR_ID") return TransmitterManager::hasCanSettings() ? String(TransmitterManager::getCanSettings().sofar_id) : "0";
    if (var == "PYLONSEND") return TransmitterManager::hasCanSettings() ? String(TransmitterManager::getCanSettings().pylon_send_interval_ms) : "0";
    if (var == "PWMFREQ") return TransmitterManager::hasContactorSettings() ? String(TransmitterManager::getContactorSettings().pwm_frequency_hz) : "0";
    if (var == "PWMHOLD") return mockSettings.getString("PWMHOLD", "250");
    
    // GPIO options
    if (var == "GPIOOPT1") return "<option value='0'>Default</option>";
    
    // Default: return empty string for unknown placeholders
    return "";
}
