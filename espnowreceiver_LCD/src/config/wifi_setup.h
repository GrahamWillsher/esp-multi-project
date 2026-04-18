#pragma once
#include <cstddef>

namespace WiFiSetup {

// Connects STA WiFi using values previously loaded by ReceiverNetworkConfig::loadConfig().
// Starts mDNS on success.  Returns true when connected, false on timeout/failure.
bool setup_from_loaded_config();

// Returns true when the station interface is currently associated.
bool is_sta_connected();

// Starts AP mode so the user can connect and configure via the webserver.
// SSID = "ESP32-LCD-Setup", open (no password), IP = 192.168.4.1.
void start_ap_fallback();

// Returns true when currently operating as a soft-AP (AP or AP+STA).
bool is_ap_mode();

// Copies the current IP string into buf (STA IP or "192.168.4.1" in AP mode).
void get_ip_string(char* buf, size_t len);

}  // namespace WiFiSetup
