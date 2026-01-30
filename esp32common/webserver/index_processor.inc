// Main page processor - generates the dashboard HTML content
#include <Arduino.h>
#include <WiFi.h>
#include "../../datalayer/datalayer.h"
#include "../../datalayer/datalayer_extended.h"
#include "../hal/esp_eth_w5500.h"
#include "../../battery/BATTERIES.h"
#include "../../battery/Shunt.h"
#include "../../inverter/INVERTERS.h"
#include "../../charger/CHARGERS.h"
#include "../../communication/contactorcontrol/comm_contactorcontrol.h"
#include "../utils/timer.h"
#include "../utils/events.h"
#include "../safety/safety.h"
#include "../wifi/wifi.h"
#include "../hal/hal.h"
#include "webserver.h"
#include "html_escape.h"

// Helper function to convert WiFi status to string
static String getConnectResultString(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "Idle";
    case WL_NO_SSID_AVAIL:
      return "SSID not available";
    case WL_SCAN_COMPLETED:
      return "Scan completed";
    case WL_CONNECTED:
      return "Connected";
    case WL_CONNECT_FAILED:
      return "Connection failed";
    case WL_CONNECTION_LOST:
      return "Connection lost";
    case WL_DISCONNECTED:
      return "Disconnected";
    default:
      return "Unknown";
  }
}

// Helper function to get formatted uptime string
static String get_uptime() {
  unsigned long uptime_ms = millis();
  unsigned long uptime_seconds = uptime_ms / 1000;
  unsigned long days = uptime_seconds / 86400;
  unsigned long hours = (uptime_seconds % 86400) / 3600;
  unsigned long minutes = (uptime_seconds % 3600) / 60;
  unsigned long seconds = uptime_seconds % 60;
  
  String uptime = "";
  if (days > 0) {
    uptime += String(days) + "d ";
  }
  uptime += String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s";
  return uptime;
}

// Forward declaration for http_username from webserver.cpp
extern std::string http_username;

String processor(const String& var) {
  if (var == "X") {
    String content = "";
    
    // Page-specific styles (only h2 size override, all other styles come from COMMON_STYLES)
    content += "<style>h2 { font-size: 1.2em; margin: 0.3em 0 0.5em 0; }</style>";

    // Compact header
    content += "<h2>Battery Emulator</h2>";

    // Start content block
    content += "<div style='background-color: #303E47; padding: 10px; margin-bottom: 10px; border-radius: 50px'>";
    content += "<h4>Software: " + String(version_number);

// Show hardware used:
// #ifdef HW_LILYGO
//     content += " Hardware: LilyGo T-CAN485";
// #endif  // HW_LILYGO
// #ifdef HW_LILYGO2CAN
//     content += " Hardware: LilyGo T_2CAN";
// #endif  // HW_LILYGO2CAN
// #ifdef HW_STARK
//     content += " Hardware: Stark CMR Module";
// #endif  // HW_STARK
// #ifdef HW_LILYGO_T_CONNECT_PRO
//     content += " Hardware: LilyGo T-Connect Pro";
// #endif  // HW_LILYGO_T_CONNECT_PRO

const char* hardware_name = esp32hal->name();
content += " Hardware: " + String(hardware_name ? hardware_name : "undefined");

    content += " @ " + String(datalayer.system.info.CPU_temperature, 1) + " &deg;C</h4>";
    content += "<h4>Uptime: " + get_uptime() + "</h4>";
    if (datalayer.system.info.performance_measurement_active) {
      // Load information
      content += "<h4>Core task max load: " + String(datalayer.system.status.core_task_max_us) + " us</h4>";
      content +=
          "<h4>Core task max load last 10 s: " + String(datalayer.system.status.core_task_10s_max_us) + " us</h4>";
      content +=
          "<h4>MQTT function (MQTT task) max load last 10 s: " + String(datalayer.system.status.mqtt_task_10s_max_us) +
          " us</h4>";
      content +=
          "<h4>WIFI function (MQTT task) max load last 10 s: " + String(datalayer.system.status.wifi_task_10s_max_us) +
          " us</h4>";
      content += "<h4>Max load @ worst case execution of core task:</h4>";
      content += "<h4>10ms function timing: " + String(datalayer.system.status.time_snap_10ms_us) + " us</h4>";
      content += "<h4>Values function timing: " + String(datalayer.system.status.time_snap_values_us) + " us</h4>";
      content += "<h4>CAN/serial RX function timing: " + String(datalayer.system.status.time_snap_comm_us) + " us</h4>";
      content += "<h4>CAN TX function timing: " + String(datalayer.system.status.time_snap_cantx_us) + " us</h4>";
      content += "<h4>OTA function timing: " + String(datalayer.system.status.time_snap_ota_us) + " us</h4>";
    }

    // Display appropriate network information based on active connection
    bool wifi_connected = wifi_enabled && (WiFi.status() == WL_CONNECTED || WiFi.getMode() == WIFI_AP);
    bool ethernet_ready = ethernetPresent && ethernet_connected && esp_eth_w5500_is_link_up();
    
    if (wifi_connected && ethernet_ready) {
      // Both WiFi and Ethernet available - show both
      content += "<h4>Network Mode: WiFi + Ethernet</h4>";
      
      // WiFi info
      content += "<h4>WiFi SSID: " + html_escape(ssid.c_str());
      content += " RSSI:" + String(WiFi.RSSI()) + " dBm Ch: " + String(WiFi.channel());
      content += "</h4>";
      content += "<h4>WiFi IP: " + WiFi.localIP().toString() + "</h4>";
      
      // Ethernet info
      char eth_ip[16];
      esp_eth_w5500_get_ip(eth_ip, sizeof(eth_ip));
      content += "<h4>Ethernet IP: " + String(eth_ip) + "</h4>";
      
    } else if (wifi_connected) {
      // WiFi only mode
      wl_status_t status = WiFi.status();
      content += "<h4>Network Mode: WiFi</h4>";
      content += "<h4>SSID: " + html_escape(ssid.c_str());
      if (status == WL_CONNECTED) {
        content += " RSSI:" + String(WiFi.RSSI()) + " dBm Ch: " + String(WiFi.channel());
      }
      content += "</h4>";
      
      if (status == WL_CONNECTED) {
        content += "<h4>WiFi IP: " + WiFi.localIP().toString() + "</h4>";
        content += "<h4>Hostname: " + html_escape(WiFi.getHostname()) + "</h4>";
      } else {
        content += "<h4>WiFi state: " + getConnectResultString(status) + "</h4>";
      }
      
    } else if (ethernet_ready) {
      // Ethernet only mode
      content += "<h4>Network Mode: Ethernet (W5500)</h4>";
      char eth_ip[16];
      esp_eth_w5500_get_ip(eth_ip, sizeof(eth_ip));
      content += "<h4>Ethernet IP: " + String(eth_ip) + "</h4>";
      // Gateway, subnet, and MAC info not readily available from ESP-IDF driver
      // Could be added if needed by extending the driver API
      
    } else {
      // No network connection
      content += "<h4>Network Mode: Not connected</h4>";
      if (wifi_enabled) {
        wl_status_t status = WiFi.status();
        content += "<h4>WiFi state: " + getConnectResultString(status) + "</h4>";
      }
      if (ethernetPresent) {
        content += "<h4>Ethernet: Checking connection...</h4>";
      }
    }
    // Close the block
    content += "</div>";

    if (inverter || battery || charger || user_selected_shunt_type != ShuntType::None) {
      // Start a new block with a specific background color
      content += "<div style='background-color: #333; padding: 10px; margin-bottom: 10px; border-radius: 50px'>";

      // Display which components are used
      if (inverter) {
        content += "<h4 style='color: white;'>Inverter protocol: ";
        content += inverter->name();
        content += " ";
        content += datalayer.system.info.inverter_brand;
        content += "</h4>";
      }

      if (battery) {
        content += "<h4 style='color: white;'>Battery protocol: ";
        content += datalayer.system.info.battery_protocol;
        if (battery3) {
          content += " (Triple battery)";
        } else if (battery2) {
          content += " (Double battery)";
        }
        if (datalayer.battery.info.chemistry == battery_chemistry_enum::LFP) {
          content += " (LFP)";
        }
        content += "</h4>";
      }

      if (user_selected_shunt_type != ShuntType::None) {
        content += "<h4 style='color: white;'>Shunt protocol: ";
        content += datalayer.system.info.shunt_protocol;
        content += "</h4>";
      }

      if (charger) {
        content += "<h4 style='color: white;'>Charger protocol: ";
        content += charger->name();
        content += "</h4>";
      }

      // Close the block
      content += "</div>";
    }

    if (battery) {
      if (battery2) {
        // Start a new block with a specific background color. Color changes depending on BMS status
        content += "<div style='display: flex; width: 100%;'>";
        content += "<div style='flex: 1; background-color: ";
      } else {
        // Start a new block with a specific background color. Color changes depending on system status
        content += "<div style='background-color: ";
      }

      switch (get_emulator_status()) {
        case EMULATOR_STATUS::STATUS_OK:
          content += "#2D3F2F;";
          break;
        case EMULATOR_STATUS::STATUS_WARNING:
          content += "#F5CC00;";
          break;
        case EMULATOR_STATUS::STATUS_ERROR:
          content += "#A70107;";
          break;
        case EMULATOR_STATUS::STATUS_UPDATING:
          content += "#2B35AF;";  // Blue in test mode
          break;
      }

      // Add the common style properties
      content += "padding: 10px; margin-bottom: 10px; border-radius: 50px;'>";

      // Display battery statistics within this block
      float socRealFloat =
          static_cast<float>(datalayer.battery.status.real_soc) / 100.0f;  // Convert to float and divide by 100
      float socScaledFloat =
          static_cast<float>(datalayer.battery.status.reported_soc) / 100.0f;  // Convert to float and divide by 100
      float sohFloat =
          static_cast<float>(datalayer.battery.status.soh_pptt) / 100.0f;  // Convert to float and divide by 100
      float voltageFloat =
          static_cast<float>(datalayer.battery.status.voltage_dV) / 10.0f;  // Convert to float and divide by 10
      float currentFloat =
          static_cast<float>(datalayer.battery.status.current_dA) / 10.0f;  // Convert to float and divide by 10
      float powerFloat = static_cast<float>(datalayer.battery.status.active_power_W);                // Convert to float
      float tempMaxFloat = static_cast<float>(datalayer.battery.status.temperature_max_dC) / 10.0f;  // Convert to float
      float tempMinFloat = static_cast<float>(datalayer.battery.status.temperature_min_dC) / 10.0f;  // Convert to float
      float maxCurrentChargeFloat =
          static_cast<float>(datalayer.battery.status.max_charge_current_dA) / 10.0f;  // Convert to float
      float maxCurrentDischargeFloat =
          static_cast<float>(datalayer.battery.status.max_discharge_current_dA) / 10.0f;  // Convert to float
      uint16_t cell_delta_mv =
          datalayer.battery.status.cell_max_voltage_mV - datalayer.battery.status.cell_min_voltage_mV;

      if (datalayer.battery.settings.soc_scaling_active)
        content += "<h4 style='color: white;'>Scaled SOC: " + String(socScaledFloat, 2) +
                   "&percnt; (real: " + String(socRealFloat, 2) + "&percnt;)</h4>";
      else
        content += "<h4 style='color: white;'>SOC: " + String(socRealFloat, 2) + "&percnt;</h4>";

      content += "<h4 style='color: white;'>SOH: " + String(sohFloat, 2) + "&percnt;</h4>";
      content += "<h4 style='color: white;'>Voltage: " + String(voltageFloat, 1) +
                 " V &nbsp; Current: " + String(currentFloat, 1) + " A</h4>";
      content += formatPowerValue("Power", powerFloat, "", 1);

      if (datalayer.battery.settings.soc_scaling_active)
        content += "<h4 style='color: white;'>Scaled total capacity: " +
                   formatPowerValue(datalayer.battery.info.reported_total_capacity_Wh, "h", 1) +
                   " (real: " + formatPowerValue(datalayer.battery.info.total_capacity_Wh, "h", 1) + ")</h4>";
      else
        content += formatPowerValue("Total capacity", datalayer.battery.info.total_capacity_Wh, "h", 1);

      if (datalayer.battery.settings.soc_scaling_active)
        content += "<h4 style='color: white;'>Scaled remaining capacity: " +
                   formatPowerValue(datalayer.battery.status.reported_remaining_capacity_Wh, "h", 1) +
                   " (real: " + formatPowerValue(datalayer.battery.status.remaining_capacity_Wh, "h", 1) + ")</h4>";
      else
        content += formatPowerValue("Remaining capacity", datalayer.battery.status.remaining_capacity_Wh, "h", 1);

      if (datalayer.system.info.equipment_stop_active) {
        content +=
            formatPowerValue("Max discharge power", datalayer.battery.status.max_discharge_power_W, "", 1, "red");
        content += formatPowerValue("Max charge power", datalayer.battery.status.max_charge_power_W, "", 1, "red");
        content += "<h4 style='color: red;'>Max discharge current: " + String(maxCurrentDischargeFloat, 1) + " A</h4>";
        content += "<h4 style='color: red;'>Max charge current: " + String(maxCurrentChargeFloat, 1) + " A</h4>";
      } else {
        content += formatPowerValue("Max discharge power", datalayer.battery.status.max_discharge_power_W, "", 1);
        content += formatPowerValue("Max charge power", datalayer.battery.status.max_charge_power_W, "", 1);
        content += "<h4 style='color: white;'>Max discharge current: " + String(maxCurrentDischargeFloat, 1) + " A";
        if (datalayer.battery.settings.remote_settings_limit_discharge) {
          content += " (Remote)</h4>";
        } else if (datalayer.battery.settings.user_settings_limit_discharge) {
          content += " (Manual)</h4>";
        } else {
          content += " (BMS)</h4>";
        }
        content += "<h4 style='color: white;'>Max charge current: " + String(maxCurrentChargeFloat, 1) + " A";
        if (datalayer.battery.settings.remote_settings_limit_charge) {
          content += " (Remote)</h4>";
        } else if (datalayer.battery.settings.user_settings_limit_charge) {
          content += " (Manual)</h4>";
        } else {
          content += " (BMS)</h4>";
        }
      }

      content += "<h4>Cell min/max: " + String(datalayer.battery.status.cell_min_voltage_mV) + " mV / " +
                 String(datalayer.battery.status.cell_max_voltage_mV) + " mV</h4>";
      if (cell_delta_mv > datalayer.battery.info.max_cell_voltage_deviation_mV) {
        content += "<h4 style='color: red;'>Cell delta: " + String(cell_delta_mv) + " mV</h4>";
      } else {
        content += "<h4>Cell delta: " + String(cell_delta_mv) + " mV</h4>";
      }
      content += "<h4>Temperature min/max: " + String(tempMinFloat, 1) + " &deg;C / " + String(tempMaxFloat, 1) +
                 " &deg;C</h4>";

      content += "<h4>System status: ";
      switch (datalayer.battery.status.bms_status) {
        case ACTIVE:
          content += String("OK");
          break;
        case UPDATING:
          content += String("UPDATING");
          break;
        case FAULT:
          content += String("FAULT");
          break;
        case INACTIVE:
          content += String("INACTIVE");
          break;
        case STANDBY:
          content += String("STANDBY");
          break;
        default:
          content += String("??");
          break;
      }
      content += "</h4>";

      if (battery && battery->supports_real_BMS_status()) {
        content += "<h4>Battery BMS status: ";
        switch (datalayer.battery.status.real_bms_status) {
          case BMS_ACTIVE:
            content += String("OK");
            break;
          case BMS_FAULT:
            content += String("FAULT");
            break;
          case BMS_DISCONNECTED:
            content += String("DISCONNECTED");
            break;
          case BMS_STANDBY:
            content += String("STANDBY");
            break;
          default:
            content += String("??");
            break;
        }
        content += "</h4>";
      }

      if (datalayer.battery.status.current_dA == 0) {
        content += "<h4>Battery idle</h4>";
      } else if (datalayer.battery.status.current_dA < 0) {
        content += "<h4>Battery discharging!";
        if (datalayer.battery.settings.inverter_limits_discharge) {
          content += " (Inverter limiting)</h4>";
        } else {
          if (datalayer.battery.settings.user_settings_limit_discharge) {
            content += " (Settings limiting)</h4>";
          } else {
            content += " (Battery limiting)</h4>";
          }
        }
        content += "</h4>";
      } else {  // > 0 , positive current
        content += "<h4>Battery charging!";
        if (datalayer.battery.settings.inverter_limits_charge) {
          content += " (Inverter limiting)</h4>";
        } else {
          if (datalayer.battery.settings.user_settings_limit_charge) {
            content += " (Settings limiting)</h4>";
          } else {
            content += " (Battery limiting)</h4>";
          }
        }
      }

      // Close the block
      content += "</div>";

      if (battery2) {
        content += "<div style='flex: 1; background-color: ";
        switch (datalayer.battery.status.bms_status) {
          case ACTIVE:
            content += "#2D3F2F;";
            break;
          case FAULT:
            content += "#A70107;";
            break;
          default:
            content += "#2D3F2F;";
            break;
        }
        // Add the common style properties
        content += "padding: 10px; margin-bottom: 10px; border-radius: 50px;'>";

        // Display battery statistics within this block
        socRealFloat =
            static_cast<float>(datalayer.battery2.status.real_soc) / 100.0f;  // Convert to float and divide by 100
        //socScaledFloat; // Same value used for bat2
        sohFloat =
            static_cast<float>(datalayer.battery2.status.soh_pptt) / 100.0f;  // Convert to float and divide by 100
        voltageFloat =
            static_cast<float>(datalayer.battery2.status.voltage_dV) / 10.0f;  // Convert to float and divide by 10
        currentFloat =
            static_cast<float>(datalayer.battery2.status.current_dA) / 10.0f;       // Convert to float and divide by 10
        powerFloat = static_cast<float>(datalayer.battery2.status.active_power_W);  // Convert to float
        tempMaxFloat = static_cast<float>(datalayer.battery2.status.temperature_max_dC) / 10.0f;  // Convert to float
        tempMinFloat = static_cast<float>(datalayer.battery2.status.temperature_min_dC) / 10.0f;  // Convert to float
        cell_delta_mv = datalayer.battery2.status.cell_max_voltage_mV - datalayer.battery2.status.cell_min_voltage_mV;

        if (datalayer.battery.settings.soc_scaling_active)
          content += "<h4 style='color: white;'>Scaled SOC: " + String(socScaledFloat, 2) +
                     "&percnt; (real: " + String(socRealFloat, 2) + "&percnt;)</h4>";
        else
          content += "<h4 style='color: white;'>SOC: " + String(socRealFloat, 2) + "&percnt;</h4>";

        content += "<h4 style='color: white;'>SOH: " + String(sohFloat, 2) + "&percnt;</h4>";
        content += "<h4 style='color: white;'>Voltage: " + String(voltageFloat, 1) +
                   " V &nbsp; Current: " + String(currentFloat, 1) + " A</h4>";
        content += formatPowerValue("Power", powerFloat, "", 1);

        if (datalayer.battery.settings.soc_scaling_active)
          content += "<h4 style='color: white;'>Scaled total capacity: " +
                     formatPowerValue(datalayer.battery2.info.reported_total_capacity_Wh, "h", 1) +
                     " (real: " + formatPowerValue(datalayer.battery2.info.total_capacity_Wh, "h", 1) + ")</h4>";
        else
          content += formatPowerValue("Total capacity", datalayer.battery2.info.total_capacity_Wh, "h", 1);

        if (datalayer.battery.settings.soc_scaling_active)
          content += "<h4 style='color: white;'>Scaled remaining capacity: " +
                     formatPowerValue(datalayer.battery2.status.reported_remaining_capacity_Wh, "h", 1) +
                     " (real: " + formatPowerValue(datalayer.battery2.status.remaining_capacity_Wh, "h", 1) + ")</h4>";
        else
          content += formatPowerValue("Remaining capacity", datalayer.battery2.status.remaining_capacity_Wh, "h", 1);

        if (datalayer.system.info.equipment_stop_active) {
          content +=
              formatPowerValue("Max discharge power", datalayer.battery2.status.max_discharge_power_W, "", 1, "red");
          content += formatPowerValue("Max charge power", datalayer.battery2.status.max_charge_power_W, "", 1, "red");
          content +=
              "<h4 style='color: red;'>Max discharge current: " + String(maxCurrentDischargeFloat, 1) + " A</h4>";
          content += "<h4 style='color: red;'>Max charge current: " + String(maxCurrentChargeFloat, 1) + " A</h4>";
        } else {
          content += formatPowerValue("Max discharge power", datalayer.battery2.status.max_discharge_power_W, "", 1);
          content += formatPowerValue("Max charge power", datalayer.battery2.status.max_charge_power_W, "", 1);
          content +=
              "<h4 style='color: white;'>Max discharge current: " + String(maxCurrentDischargeFloat, 1) + " A</h4>";
          content += "<h4 style='color: white;'>Max charge current: " + String(maxCurrentChargeFloat, 1) + " A</h4>";
        }

        content += "<h4>Cell min/max: " + String(datalayer.battery2.status.cell_min_voltage_mV) + " mV / " +
                   String(datalayer.battery2.status.cell_max_voltage_mV) + " mV</h4>";
        if (cell_delta_mv > datalayer.battery2.info.max_cell_voltage_deviation_mV) {
          content += "<h4 style='color: red;'>Cell delta: " + String(cell_delta_mv) + " mV</h4>";
        } else {
          content += "<h4>Cell delta: " + String(cell_delta_mv) + " mV</h4>";
        }
        content += "<h4>Temperature min/max: " + String(tempMinFloat, 1) + " &deg;C / " + String(tempMaxFloat, 1) +
                   " &deg;C</h4>";
        if (datalayer.battery.status.bms_status == ACTIVE) {
          content += "<h4>System status: OK </h4>";
        } else if (datalayer.battery.status.bms_status == UPDATING) {
          content += "<h4>System status: UPDATING </h4>";
        } else {
          content += "<h4>System status: FAULT </h4>";
        }
        if (datalayer.battery2.status.current_dA == 0) {
          content += "<h4>Battery idle</h4>";
        } else if (datalayer.battery2.status.current_dA < 0) {
          content += "<h4>Battery discharging!</h4>";
        } else {  // > 0
          content += "<h4>Battery charging!</h4>";
        }
        content += "</div>";
        if (battery3) {
          content += "<div style='flex: 1; background-color: ";
          switch (datalayer.battery.status.bms_status) {
            case ACTIVE:
              content += "#2D3F2F;";
              break;
            case FAULT:
              content += "#A70107;";
              break;
            default:
              content += "#2D3F2F;";
              break;
          }
          // Add the common style properties
          content += "padding: 10px; margin-bottom: 10px; border-radius: 50px;'>";

          // Display battery statistics within this block
          socRealFloat =
              static_cast<float>(datalayer.battery3.status.real_soc) / 100.0f;  // Convert to float and divide by 100
          //socScaledFloat; // Same value used for bat2
          sohFloat =
              static_cast<float>(datalayer.battery3.status.soh_pptt) / 100.0f;  // Convert to float and divide by 100
          voltageFloat =
              static_cast<float>(datalayer.battery3.status.voltage_dV) / 10.0f;  // Convert to float and divide by 10
          currentFloat =
              static_cast<float>(datalayer.battery3.status.current_dA) / 10.0f;  // Convert to float and divide by 10
          powerFloat = static_cast<float>(datalayer.battery3.status.active_power_W);                // Convert to float
          tempMaxFloat = static_cast<float>(datalayer.battery3.status.temperature_max_dC) / 10.0f;  // Convert to float
          tempMinFloat = static_cast<float>(datalayer.battery3.status.temperature_min_dC) / 10.0f;  // Convert to float
          cell_delta_mv = datalayer.battery3.status.cell_max_voltage_mV - datalayer.battery3.status.cell_min_voltage_mV;

          if (datalayer.battery.settings.soc_scaling_active)
            content += "<h4 style='color: white;'>Scaled SOC: " + String(socScaledFloat, 2) +
                       "&percnt; (real: " + String(socRealFloat, 2) + "&percnt;)</h4>";
          else
            content += "<h4 style='color: white;'>SOC: " + String(socRealFloat, 2) + "&percnt;</h4>";

          content += "<h4 style='color: white;'>SOH: " + String(sohFloat, 2) + "&percnt;</h4>";
          content += "<h4 style='color: white;'>Voltage: " + String(voltageFloat, 1) +
                     " V &nbsp; Current: " + String(currentFloat, 1) + " A</h4>";
          content += formatPowerValue("Power", powerFloat, "", 1);

          if (datalayer.battery.settings.soc_scaling_active)
            content += "<h4 style='color: white;'>Scaled total capacity: " +
                       formatPowerValue(datalayer.battery3.info.reported_total_capacity_Wh, "h", 1) +
                       " (real: " + formatPowerValue(datalayer.battery3.info.total_capacity_Wh, "h", 1) + ")</h4>";
          else
            content += formatPowerValue("Total capacity", datalayer.battery3.info.total_capacity_Wh, "h", 1);

          if (datalayer.battery.settings.soc_scaling_active)
            content += "<h4 style='color: white;'>Scaled remaining capacity: " +
                       formatPowerValue(datalayer.battery3.status.reported_remaining_capacity_Wh, "h", 1) +
                       " (real: " + formatPowerValue(datalayer.battery3.status.remaining_capacity_Wh, "h", 1) +
                       ")</h4>";
          else
            content += formatPowerValue("Remaining capacity", datalayer.battery3.status.remaining_capacity_Wh, "h", 1);

          if (datalayer.system.info.equipment_stop_active) {
            content +=
                formatPowerValue("Max discharge power", datalayer.battery3.status.max_discharge_power_W, "", 1, "red");
            content += formatPowerValue("Max charge power", datalayer.battery3.status.max_charge_power_W, "", 1, "red");
            content +=
                "<h4 style='color: red;'>Max discharge current: " + String(maxCurrentDischargeFloat, 1) + " A</h4>";
            content += "<h4 style='color: red;'>Max charge current: " + String(maxCurrentChargeFloat, 1) + " A</h4>";
          } else {
            content += formatPowerValue("Max discharge power", datalayer.battery3.status.max_discharge_power_W, "", 1);
            content += formatPowerValue("Max charge power", datalayer.battery3.status.max_charge_power_W, "", 1);
            content +=
                "<h4 style='color: white;'>Max discharge current: " + String(maxCurrentDischargeFloat, 1) + " A</h4>";
            content += "<h4 style='color: white;'>Max charge current: " + String(maxCurrentChargeFloat, 1) + " A</h4>";
          }

          content += "<h4>Cell min/max: " + String(datalayer.battery3.status.cell_min_voltage_mV) + " mV / " +
                     String(datalayer.battery3.status.cell_max_voltage_mV) + " mV</h4>";
          if (cell_delta_mv > datalayer.battery3.info.max_cell_voltage_deviation_mV) {
            content += "<h4 style='color: red;'>Cell delta: " + String(cell_delta_mv) + " mV</h4>";
          } else {
            content += "<h4>Cell delta: " + String(cell_delta_mv) + " mV</h4>";
          }
          content += "<h4>Temperature min/max: " + String(tempMinFloat, 1) + " &deg;C / " + String(tempMaxFloat, 1) +
                     " &deg;C</h4>";
          if (datalayer.battery.status.bms_status == ACTIVE) {
            content += "<h4>System status: OK </h4>";
          } else if (datalayer.battery.status.bms_status == UPDATING) {
            content += "<h4>System status: UPDATING </h4>";
          } else {
            content += "<h4>System status: FAULT </h4>";
          }
          if (datalayer.battery3.status.current_dA == 0) {
            content += "<h4>Battery idle</h4>";
          } else if (datalayer.battery3.status.current_dA < 0) {
            content += "<h4>Battery discharging!</h4>";
          } else {  // > 0
            content += "<h4>Battery charging!</h4>";
          }
          content += "</div>";
          content += "</div>";
        }
        content += "</div>";
      }
    }
    // Block for Contactor status and component request status
    // Start a new block with gray background color
    content += "<div style='background-color: #333; padding: 10px; margin-bottom: 10px;border-radius: 50px'>";

    if (emulator_pause_status == NORMAL) {
      content += "<h4>Power status: " + String(get_emulator_pause_status().c_str()) + " </h4>";
    } else {
      content += "<h4 style='color: red;'>Power status: " + String(get_emulator_pause_status().c_str()) + " </h4>";
    }

    content += "<h4>Emulator allows contactor closing: ";
    if (datalayer.battery.status.bms_status == FAULT) {
      content += "<span style='color: red;'>&#10005;</span>";
    } else {
      content += "<span>&#10003;</span>";
    }
    content += " Inverter allows contactor closing: ";
    if (datalayer.system.status.inverter_allows_contactor_closing == true) {
      content += "<span>&#10003;</span></h4>";
    } else {
      content += "<span style='color: red;'>&#10005;</span></h4>";
    }
    if (battery2) {
      content += "<h4>Secondary battery allowed to join ";
      if (datalayer.system.status.battery2_allowed_contactor_closing == true) {
        content += "<span>&#10003;</span>";
      } else {
        content += "<span style='color: red;'>&#10005; (voltage mismatch)</span>";
      }
    }

    if (!contactor_control_enabled) {
      content += "<div class=\"tooltip\">";
      content += "<h4>Contactors not fully controlled via emulator <span style=\"color:orange\">[?]</span></h4>";
      content +=
          "<span class=\"tooltiptext\">This means you are either running CAN controlled contactors OR manually "
          "powering the contactors. Battery-Emulator will have limited amount of control over the contactors!</span>";
      content += "</div>";
    } else {  //contactor_control_enabled TRUE
      content += "<div class=\"tooltip\"><h4>Contactors controlled by emulator, state: ";
      if (datalayer.system.status.contactors_engaged == 0) {
        content += "<span style='color: red;'>OFF (DISCONNECTED)</span>";
      } else if (datalayer.system.status.contactors_engaged == 1) {
        content += "<span style='color: green;'>ON</span>";
      } else if (datalayer.system.status.contactors_engaged == 2) {
        content += "<span style='color: red;'>OFF (FAULT)</span>";
        content += "<span class=\"tooltip-icon\"> [!]</span>";
        content +=
            "<span class=\"tooltiptext\">Emulator spent too much time in critical FAULT event. Investigate event "
            "causing this via Events page. Reboot required to resume operation!</span>";
      } else if (datalayer.system.status.contactors_engaged == 3) {
        content += "<span style='color: orange;'>PRECHARGE</span>";
      }
      content += "</h4></div>";
      if (contactor_control_enabled_double_battery && battery2) {
        content += "<h4>Secondary battery contactor, state: ";
        if (pwm_contactor_control) {
          if (datalayer.system.status.contactors_battery2_engaged) {
            content += "<span style='color: green;'>Economized</span>";
          } else {
            content += "<span style='color: red;'>OFF</span>";
          }
        } else if (
            esp32hal->SECOND_BATTERY_CONTACTORS_PIN() !=
            GPIO_NUM_NC) {  // No PWM_CONTACTOR_CONTROL , we can read the pin and see feedback. Helpful if channel overloaded
          if (digitalRead(esp32hal->SECOND_BATTERY_CONTACTORS_PIN()) == HIGH) {
            content += "<span style='color: green;'>ON</span>";
          } else {
            content += "<span style='color: red;'>OFF</span>";
          }
        }  //no PWM_CONTACTOR_CONTROL
        content += "</h4>";
      }
    }

    // Close the block
    content += "</div>";

    if (charger) {
      // Start a new block with orange background color
      content += "<div style='background-color: #FF6E00; padding: 10px; margin-bottom: 10px;border-radius: 50px'>";

      content += "<h4>Charger HV Enabled: ";
      if (datalayer.charger.charger_HV_enabled) {
        content += "<span>&#10003;</span>";
      } else {
        content += "<span style='color: red;'>&#10005;</span>";
      }
      content += "</h4>";

      content += "<h4>Charger Aux12v Enabled: ";
      if (datalayer.charger.charger_aux12V_enabled) {
        content += "<span>&#10003;</span>";
      } else {
        content += "<span style='color: red;'>&#10005;</span>";
      }
      content += "</h4>";

      auto chgPwrDC = charger->outputPowerDC();
      auto chgEff = charger->efficiency();

      content += formatPowerValue("Charger Output Power", chgPwrDC, "", 1);
      if (charger->efficiencySupported()) {
        content += "<h4 style='color: white;'>Charger Efficiency: " + String(chgEff) + "%</h4>";
      }

      float HVvol = charger->HVDC_output_voltage();
      float HVcur = charger->HVDC_output_current();
      float LVvol = charger->LVDC_output_voltage();
      float LVcur = charger->LVDC_output_current();

      content += "<h4 style='color: white;'>Charger HVDC Output V: " + String(HVvol, 2) + " V</h4>";
      content += "<h4 style='color: white;'>Charger HVDC Output I: " + String(HVcur, 2) + " A</h4>";
      content += "<h4 style='color: white;'>Charger LVDC Output I: " + String(LVcur, 2) + "</h4>";
      content += "<h4 style='color: white;'>Charger LVDC Output V: " + String(LVvol, 2) + "</h4>";

      float ACcur = charger->AC_input_current();
      float ACvol = charger->AC_input_voltage();

      content += "<h4 style='color: white;'>Charger AC Input V: " + String(ACvol, 2) + " VAC</h4>";
      content += "<h4 style='color: white;'>Charger AC Input I: " + String(ACcur, 2) + " A</h4>";

      content += "</div>";
    }

    if (emulator_pause_request_ON)
      content += "<button onclick='PauseBattery(false)'>Resume charge/discharge</button> ";
    else
      content +=
          "<button onclick=\"if(confirm('Are you sure you want to pause charging and discharging? This will set the "
          "maximum charge and discharge values to zero, preventing any further power flow.')) { PauseBattery(true); "
          "}\">Pause charge/discharge</button> ";

    content += "<button onclick='OTA()'>Perform OTA update</button> ";
    content += "<button onclick='Settings()'>Change Settings</button> ";
    content += "<button onclick='Advanced()'>More Battery Info</button> ";
    content += "<button onclick='CANlog()'>CAN logger</button> ";
    content += "<button onclick='CANreplay()'>CAN replay</button> ";
    if (datalayer.system.info.web_logging_active || datalayer.system.info.SD_logging_active) {
      content += "<button onclick='Log()'>Log</button> ";
    }
    content += "<button onclick='Cellmon()'>Cellmonitor</button> ";
    content += "<button onclick='Events()'>Events</button> ";
    content += "<button onclick='askReboot()'>Reboot Emulator</button>";
    if (!http_username.empty())
      content += "<button onclick='logout()'>Logout</button>";
    if (!datalayer.system.info.equipment_stop_active)
      content +=
          "<br/><button style=\"background:red;color:white;cursor:pointer;\""
          " onclick=\""
          "if(confirm('This action will attempt to open contactors on the battery. Are you "
          "sure?')) { estop(true); }\""
          ">Open Contactors</button><br/>";
    else
      content +=
          "<br/><button style=\"background:green;color:white;cursor:pointer;\""
          "20px;font-size:16px;font-weight:bold;cursor:pointer;border-radius:5px; margin:10px;"
          " onclick=\""
          "if(confirm('This action will attempt to close contactors and enable power transfer. Are you sure?')) { "
          "estop(false); }\""
          ">Close Contactors</button><br/>";
    content += "<script>";
    content += "function OTA() { window.location.href = '/update'; }";
    content += "function Cellmon() { window.location.href = '/cellmonitor'; }";
    content += "function Settings() { window.location.href = '/settings'; }";
    content += "function Advanced() { window.location.href = '/advanced'; }";
    content += "function CANlog() { window.location.href = '/canlog'; }";
    content += "function CANreplay() { window.location.href = '/canreplay'; }";
    content += "function Log() { window.location.href = '/log'; }";
    content += "function Events() { window.location.href = '/events'; }";
    if (!http_username.empty()) {
      content += "function logout() {";
      content += "  var xhr = new XMLHttpRequest();";
      content += "  xhr.open('GET', '/logout', true);";
      content += "  xhr.send();";
      content += "  setTimeout(function(){ window.open(\"/\",\"_self\"); }, 1000);";
      content += "}";
    }
    content += "function PauseBattery(pause){";
    content +=
        "var xhr=new "
        "XMLHttpRequest();xhr.onload=function() { "
        "window.location.reload();};xhr.open('GET','/pause?value='+pause,true);xhr.send();";
    content += "}";
    content += "function estop(stop){";
    content +=
        "var xhr=new "
        "XMLHttpRequest();xhr.onload=function() { "
        "window.location.reload();};xhr.open('GET','/equipmentStop?value='+stop,true);xhr.send();";
    content += "}";
    content += "</script>";

    //Script for refreshing page
    content += "<script>";
    content += "setTimeout(function(){ location.reload(true); }, 15000);";
    content += "</script>";
    
    return content;
  }
  return String();
}