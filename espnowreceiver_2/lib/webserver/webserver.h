#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_server.h>

// ESP-IDF HTTP Server handle
extern httpd_handle_t server;

// Mock/Stub class for BatteryEmulatorSettingsStore
// This replaces the full NVM settings store since we'll get data via ESP-NOW
class MockSettingsStore {
public:
    String getString(const char* key, const char* defaultValue = "") {
        return String(defaultValue);
    }
    
    uint32_t getUInt(const char* key, uint32_t defaultValue = 0) {
        return defaultValue;
    }
    
    bool getBool(const char* key, bool defaultValue = false) {
        return defaultValue;
    }
};

/**
 * @brief Initialize webserver for ESP-NOW receiver
 * Sets up modular page handlers, API endpoints, and utilities
 * @return void
 */
void init_webserver();

/**
 * @brief Stop webserver and free resources
 * @return void
 */
void stop_webserver();

/**
 * @brief Notify SSE clients that battery data has been updated
 * Call this function whenever g_received_soc, g_received_power, or test mode changes
 * Uses SSENotifier utility class
 * @return void
 */
void notify_sse_data_updated();

/**
 * @brief Register the transmitter MAC address for control messages
 * Call this when first data is received from transmitter
 * Uses TransmitterManager utility class
 * @param mac Pointer to 6-byte MAC address
 * @return void
 */
void register_transmitter_mac(const uint8_t* mac);

/**
 * @brief Store transmitter IP address data received via ESP-NOW
 * Uses TransmitterManager utility class
 * @param ip Pointer to 4-byte IP address
 * @param gateway Pointer to 4-byte gateway address
 * @param subnet Pointer to 4-byte subnet mask
 * @return void
 */
void store_transmitter_ip_data(const uint8_t* ip, const uint8_t* gateway, const uint8_t* subnet);

#endif
