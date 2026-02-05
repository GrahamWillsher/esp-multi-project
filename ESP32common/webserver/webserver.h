#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>

// ESP-IDF HTTP Server handle - defined in webserver.cpp
extern httpd_handle_t server;

extern const char* version_number;  // The current software version, shown on webserver

// Network connection state variables
extern bool wifi_enabled;
extern bool ethernetPresent;
extern volatile bool ethernet_connected;

// Common charger parameters
extern float charger_stat_HVcur;
extern float charger_stat_HVvol;
extern float charger_stat_ACcur;
extern float charger_stat_ACvol;
extern float charger_stat_LVcur;
extern float charger_stat_LVvol;

//LEAF charger
extern uint16_t OBC_Charge_Power;

/**
 * @brief Initialization function for the webserver using ESP-IDF http_server.
 *
 * @param[in] void
 *
 * @return void
 */
void init_webserver();

// /**
//  * @brief Function to handle WiFi reconnection.
//  *
//  * @param[in] void
//  *
//  * @return void
//  */
// void handle_WiFi_reconnection(unsigned long currentMillis);

/**
 * @brief Replaces placeholder with content section in web page
 *
 * @param[in] var
 *
 * @return String
 */
String processor(const String& var);
String optimised_processor(const String& var);
String optimised_advanced_battery_processor(const String& var);
String optimised_cellmonitor_processor(const String& var);
String optimised_events_processor(const String& var);
String get_firmware_info_processor(const String& var);

/**
 * @brief Formats power values 
 *
 * @param[in] float or uint16_t 
 * 
 * @return string: values
 */
template <typename T>
String formatPowerValue(String label, T value, String unit, int precision, String color = "white");

template <typename T>  // This function makes power values appear as W when under 1000, and kW when over
String formatPowerValue(T value, String unit, int precision);

extern void store_settings();

#endif
