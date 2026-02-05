#include "webserver.h"
#include "../../../src/config/config_receiver.h"
#include <Arduino.h>

// Mock settings store instance (kept for local receiver settings)
static MockSettingsStore mockSettings;

// Simplified settings processor that uses ReceiverConfigManager for transmitter config
// Returns values from synchronized config or empty/default values if not yet received
String settings_processor(const String& var) {
    auto& configMgr = ReceiverConfigManager::instance();
    bool configAvailable = configMgr.isConfigAvailable();
    
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
    
    // Power settings (from transmitter config)
    if (var == "CHGPOWER") {
        if (configAvailable) {
            const PowerConfig& power = configMgr.getPowerConfig();
            return String(power.charge_power_w);
        }
        return "0";
    }
    if (var == "DCHGPOWER") {
        if (configAvailable) {
            const PowerConfig& power = configMgr.getPowerConfig();
            return String(power.discharge_power_w);
        }
        return "0";
    }
    
    // Voltage settings (from transmitter config)
    if (var == "BATTPVMAX") {
        if (configAvailable) {
            const BatteryConfig& batt = configMgr.getBatteryConfig();
            return String(batt.pack_voltage_max / 1000.0, 1);
        }
        return "0.0";
    }
    if (var == "BATTPVMIN") {
        if (configAvailable) {
            const BatteryConfig& batt = configMgr.getBatteryConfig();
            return String(batt.pack_voltage_min / 1000.0, 1);
        }
        return "0.0";
    }
    if (var == "BATTCVMAX") {
        if (configAvailable) {
            const BatteryConfig& batt = configMgr.getBatteryConfig();
            return String(batt.cell_voltage_max);
        }
        return "0";
    }
    if (var == "BATTCVMIN") {
        if (configAvailable) {
            const BatteryConfig& batt = configMgr.getBatteryConfig();
            return String(batt.cell_voltage_min);
        }
        return "0";
    }
    
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
    
    // MQTT settings (from transmitter config)
    if (var == "MQTTSERVER") {
        if (configAvailable) {
            const MqttConfig& mqtt = configMgr.getMqttConfig();
            return String(mqtt.server);
        }
        return "";
    }
    if (var == "MQTTUSER") {
        if (configAvailable) {
            const MqttConfig& mqtt = configMgr.getMqttConfig();
            return String(mqtt.username);
        }
        return "";
    }
    if (var == "MQTTPASSWORD") {
        if (configAvailable) {
            const MqttConfig& mqtt = configMgr.getMqttConfig();
            return String(mqtt.password);
        }
        return "";
    }
    if (var == "MQTTPORT") {
        if (configAvailable) {
            const MqttConfig& mqtt = configMgr.getMqttConfig();
            return String(mqtt.port);
        }
        return "1883";
    }
    if (var == "MQTTTOPIC") return mockSettings.getString("MQTTTOPIC", "");
    if (var == "MQTTTIMEOUT") return mockSettings.getString("MQTTTIMEOUT", "2000");
    if (var == "MQTTOBJIDPREFIX") return mockSettings.getString("MQTTOBJIDPREFIX", "");
    if (var == "MQTTDEVICENAME") return mockSettings.getString("MQTTDEVICENAME", "");
    if (var == "HADEVICEID") return mockSettings.getString("HADEVICEID", "");
    
    // Boolean checkboxes - return "checked" or ""
    if (var == "DBLBTR") {
        if (configAvailable) {
            const BatteryConfig& batt = configMgr.getBatteryConfig();
            return batt.double_battery ? "checked" : "";
        }
        return "";
    }
    if (var == "SOCESTIMATED") {
        if (configAvailable) {
            const BatteryConfig& batt = configMgr.getBatteryConfig();
            return batt.use_estimated_soc ? "checked" : "";
        }
        return "";
    }
    if (var == "CNTCTRL") {
        if (configAvailable) {
            const ContactorConfig& contactor = configMgr.getContactorConfig();
            return contactor.control_enabled ? "checked" : "";
        }
        return "";
    }
    if (var == "NCCONTACTOR") {
        if (configAvailable) {
            const ContactorConfig& contactor = configMgr.getContactorConfig();
            return contactor.nc_contactor ? "checked" : "";
        }
        return "";
    }
    if (var == "WIFIAPENABLED") return mockSettings.getBool("WIFIAPENABLED") ? "checked" : "";
    if (var == "STATICIP") return mockSettings.getBool("STATICIP") ? "checked" : "";
    if (var == "WEBENABLED") return mockSettings.getBool("WEBENABLED", true) ? "checked" : "checked";
    if (var == "INTERLOCKREQ") return mockSettings.getBool("INTERLOCKREQ") ? "checked" : "";
    if (var == "DIGITALHVIL") return mockSettings.getBool("DIGITALHVIL") ? "checked" : "";
    if (var == "GTWRHD") return mockSettings.getBool("GTWRHD") ? "checked" : "";
    
    // Numeric values (from transmitter config where applicable)
    if (var == "MAXPRETIME") {
        if (configAvailable) {
            const PowerConfig& power = configMgr.getPowerConfig();
            return String(power.max_precharge_ms);
        }
        return "15000";
    }
    if (var == "PRECHGMS") {
        if (configAvailable) {
            const PowerConfig& power = configMgr.getPowerConfig();
            return String(power.precharge_duration_ms);
        }
        return "100";
    }
    if (var == "CANFREQ") {
        if (configAvailable) {
            const CanConfig& can = configMgr.getCanConfig();
            return String(can.frequency_khz);
        }
        return "8";
    }
    if (var == "CANFDFREQ") {
        if (configAvailable) {
            const CanConfig& can = configMgr.getCanConfig();
            return String(can.fd_frequency_mhz);
        }
        return "40";
    }
    if (var == "LEDMODE") return "<option value='0'>Default</option>";
    
    // Tesla-specific
    if (var == "GTWCOUNTRY") return "<option value='0'>Not Set</option>";
    if (var == "GTWMAPREG") return "<option value='0'>Not Set</option>";
    if (var == "GTWCHASSIS") return "<option value='0'>Not Set</option>";
    if (var == "GTWPACK") return "<option value='0'>Not Set</option>";
    
    // Inverter specific (from transmitter config)
    if (var == "INVCELLS") {
        if (configAvailable) {
            const InverterConfig& inv = configMgr.getInverterConfig();
            return String(inv.total_cells);
        }
        return "0";
    }
    if (var == "INVMODULES") {
        if (configAvailable) {
            const InverterConfig& inv = configMgr.getInverterConfig();
            return String(inv.modules);
        }
        return "0";
    }
    if (var == "INVCELLSPER") {
        if (configAvailable) {
            const InverterConfig& inv = configMgr.getInverterConfig();
            return String(inv.cells_per_module);
        }
        return "0";
    }
    if (var == "INVVLEVEL") {
        if (configAvailable) {
            const InverterConfig& inv = configMgr.getInverterConfig();
            return String(inv.voltage_level);
        }
        return "0";
    }
    if (var == "INVCAPACITY") {
        if (configAvailable) {
            const InverterConfig& inv = configMgr.getInverterConfig();
            return String(inv.capacity_ah);
        }
        return "0";
    }
    if (var == "INVBTYPE") {
        if (configAvailable) {
            const InverterConfig& inv = configMgr.getInverterConfig();
            return String(inv.battery_type);
        }
        return "0";
    }
    if (var == "SOFAR_ID") {
        if (configAvailable) {
            const CanConfig& can = configMgr.getCanConfig();
            return String(can.sofar_id);
        }
        return "0";
    }
    if (var == "PYLONSEND") {
        if (configAvailable) {
            const CanConfig& can = configMgr.getCanConfig();
            return String(can.pylon_send_interval);
        }
        return "0";
    }
    if (var == "PWMFREQ") {
        if (configAvailable) {
            const ContactorConfig& contactor = configMgr.getContactorConfig();
            return String(contactor.pwm_frequency);
        }
        return "20000";
    }
    if (var == "PWMHOLD") return mockSettings.getString("PWMHOLD", "250");
    
    // GPIO options
    if (var == "GPIOOPT1") return "<option value='0'>Default</option>";
    
    // Default: return empty string for unknown placeholders
    return "";
}
