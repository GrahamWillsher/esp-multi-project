#include "webserver.h"
#include <Arduino.h>

// Mock settings store instance
static MockSettingsStore mockSettings;

// Simplified settings processor that uses the mock store
// Returns placeholder values for all settings since we'll use ESP-NOW to get real data
String settings_processor(const String& var) {
    // For now, return empty/default values for all placeholders
    // These will be populated via ESP-NOW in the future
    
    // Common placeholders from settings page
    if (var == "SAVEDCLASS") return "hidden";
    if (var == "SSID") return mockSettings.getString("SSID", "");
    if (var == "PASSWORD") return mockSettings.getString("PASSWORD", "");
    if (var == "HOSTNAME") return mockSettings.getString("HOSTNAME", "ESP32-Receiver");
    
    // Battery settings
    if (var == "BATTTYPE") return "<option value='0'>No Battery Selected</option>";
    if (var == "BATTCOMM") return "<option value='0'>No Interface</option>";
    if (var == "BATTCHEM") return "<option value='0'>Unknown</option>";
    if (var == "BATT2COMM") return "<option value='0'>No Interface</option>";
    
    // Inverter settings
    if (var == "INVTYPE") return "<option value='0'>No Inverter Selected</option>";
    if (var == "INVCOMM") return "<option value='0'>No Interface</option>";
    
    // Charger settings  
    if (var == "CHGTYPE") return "<option value='0'>No Charger Selected</option>";
    if (var == "CHGCOMM") return "<option value='0'>No Interface</option>";
    
    // Network settings
    if (var == "WIFICHANNEL") return mockSettings.getString("WIFICHANNEL", "0");
    if (var == "APNAME") return mockSettings.getString("APNAME", "ESP32-AP");
    if (var == "APPASSWORD") return mockSettings.getString("APPASSWORD", "");
    
    // Power settings
    if (var == "CHGPOWER") return mockSettings.getString("CHGPOWER", "0");
    if (var == "DCHGPOWER") return mockSettings.getString("DCHGPOWER", "0");
    
    // Voltage settings
    if (var == "BATTPVMAX") return mockSettings.getString("BATTPVMAX", "0.0");
    if (var == "BATTPVMIN") return mockSettings.getString("BATTPVMIN", "0.0");
    if (var == "BATTCVMAX") return mockSettings.getString("BATTCVMAX", "0");
    if (var == "BATTCVMIN") return mockSettings.getString("BATTCVMIN", "0");
    
    // IP settings
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
    
    // MQTT settings
    if (var == "MQTTSERVER") return mockSettings.getString("MQTTSERVER", "");
    if (var == "MQTTUSER") return mockSettings.getString("MQTTUSER", "");
    if (var == "MQTTPASSWORD") return mockSettings.getString("MQTTPASSWORD", "");
    if (var == "MQTTPORT") return mockSettings.getString("MQTTPORT", "1883");
    if (var == "MQTTTOPIC") return mockSettings.getString("MQTTTOPIC", "");
    if (var == "MQTTTIMEOUT") return mockSettings.getString("MQTTTIMEOUT", "2000");
    if (var == "MQTTOBJIDPREFIX") return mockSettings.getString("MQTTOBJIDPREFIX", "");
    if (var == "MQTTDEVICENAME") return mockSettings.getString("MQTTDEVICENAME", "");
    if (var == "HADEVICEID") return mockSettings.getString("HADEVICEID", "");
    
    // Boolean checkboxes - return "checked" or ""
    if (var == "DBLBTR") return mockSettings.getBool("DBLBTR") ? "checked" : "";
    if (var == "SOCESTIMATED") return mockSettings.getBool("SOCESTIMATED") ? "checked" : "";
    if (var == "CNTCTRL") return mockSettings.getBool("CNTCTRL") ? "checked" : "";
    if (var == "NCCONTACTOR") return mockSettings.getBool("NCCONTACTOR") ? "checked" : "";
    if (var == "WIFIAPENABLED") return mockSettings.getBool("WIFIAPENABLED") ? "checked" : "";
    if (var == "STATICIP") return mockSettings.getBool("STATICIP") ? "checked" : "";
    if (var == "WEBENABLED") return mockSettings.getBool("WEBENABLED", true) ? "checked" : "checked";
    if (var == "INTERLOCKREQ") return mockSettings.getBool("INTERLOCKREQ") ? "checked" : "";
    if (var == "DIGITALHVIL") return mockSettings.getBool("DIGITALHVIL") ? "checked" : "";
    if (var == "GTWRHD") return mockSettings.getBool("GTWRHD") ? "checked" : "";
    
    // Numeric values
    if (var == "MAXPRETIME") return mockSettings.getString("MAXPRETIME", "15000");
    if (var == "PRECHGMS") return mockSettings.getString("PRECHGMS", "100");
    if (var == "CANFREQ") return mockSettings.getString("CANFREQ", "8");
    if (var == "CANFDFREQ") return mockSettings.getString("CANFDFREQ", "40");
    if (var == "LEDMODE") return "<option value='0'>Default</option>";
    
    // Tesla-specific
    if (var == "GTWCOUNTRY") return "<option value='0'>Not Set</option>";
    if (var == "GTWMAPREG") return "<option value='0'>Not Set</option>";
    if (var == "GTWCHASSIS") return "<option value='0'>Not Set</option>";
    if (var == "GTWPACK") return "<option value='0'>Not Set</option>";
    
    // Inverter specific
    if (var == "INVCELLS") return mockSettings.getString("INVCELLS", "0");
    if (var == "INVMODULES") return mockSettings.getString("INVMODULES", "0");
    if (var == "INVCELLSPER") return mockSettings.getString("INVCELLSPER", "0");
    if (var == "INVVLEVEL") return mockSettings.getString("INVVLEVEL", "0");
    if (var == "INVCAPACITY") return mockSettings.getString("INVCAPACITY", "0");
    if (var == "INVBTYPE") return mockSettings.getString("INVBTYPE", "0");
    if (var == "SOFAR_ID") return mockSettings.getString("SOFAR_ID", "0");
    if (var == "PYLONSEND") return mockSettings.getString("PYLONSEND", "0");
    if (var == "PWMFREQ") return mockSettings.getString("PWMFREQ", "20000");
    if (var == "PWMHOLD") return mockSettings.getString("PWMHOLD", "250");
    
    // GPIO options
    if (var == "GPIOOPT1") return "<option value='0'>Default</option>";
    
    // Default: return empty string for unknown placeholders
    return "";
}
