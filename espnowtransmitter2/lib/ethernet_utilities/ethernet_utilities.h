#ifndef ETHERNET_UTILITIES_H
#define ETHERNET_UTILITIES_H

/**
 * Network Time Utilities for ESP32
 * 
 * Provides NTP time synchronization and internet connectivity monitoring
 * for ESP32 boards. Works with both WiFi and Ethernet connections.
 * 
 * On ESP32, WiFiUDP and WiFiClient work transparently with both WiFi 
 * and Ethernet (ETH) - the network stack handles routing automatically.
 * 
 * Compatible with:
 * - ESP32 with WiFi
 * - ESP32 with internal Ethernet PHY (Olimex ESP32-POE, WT32-ETH01, etc.)
 * - ESP32 with external Ethernet (W5500, etc.)
 */

#include <Arduino.h>
#include <ETH.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>

// NTP Configuration
#ifndef NTP_SERVER1
#define NTP_SERVER1 "pool.ntp.org"
#endif

#ifndef NTP_SERVER2
#define NTP_SERVER2 "time.nist.gov"
#endif

// Internet connectivity test configuration
#define INTERNET_TEST_HOST "8.8.8.8"  // Google DNS
#define INTERNET_TEST_PORT 53         // DNS port

// Time sync intervals
#define NTP_SYNC_INTERVAL_MS (30 * 60 * 1000)  // 30 minutes
#define INTERNET_CHECK_INTERVAL_MS (60 * 1000)  // 60 seconds (for periodic ping)

// Task configuration
#define ETHERNET_UTILS_TASK_STACK_SIZE 4096
#define ETHERNET_UTILS_TASK_PRIORITY 1
#define ETHERNET_UTILS_TASK_CORE 0

/**
 * @brief Initialize Ethernet utilities library
 * @return true if initialization successful, false otherwise
 */
bool init_ethernet_utilities();

/**
 * @brief Start the Ethernet utilities task (NTP sync and connectivity monitoring)
 * @return true if task started successfully, false otherwise
 */
bool start_ethernet_utilities_task();

/**
 * @brief Stop the Ethernet utilities task
 */
void stop_ethernet_utilities_task();

/**
 * @brief Get time from NTP servers via Ethernet
 * @return true if time sync was successful, false otherwise
 */
bool get_ntp_time();

/**
 * @brief Test internet connectivity by pinging Google DNS
 * @return true if internet is reachable, false otherwise
 */
bool test_internet_connectivity();

/**
 * @brief Check if network is connected (WiFi or Ethernet)
 * @return true if network has valid IP address, false otherwise
 */
bool is_network_connected();

/**
 * @brief Check if internet is reachable (cached result from periodic checks)
 * @return true if internet connectivity test passed, false otherwise
 */
bool is_internet_reachable();

/**
 * @brief Configure timezone from IP geolocation
 * @return true if timezone was configured successfully, false otherwise
 */
bool configure_timezone_from_location();

/**
 * @brief Get current time as formatted string
 * @param buffer Buffer to store formatted time string
 * @param bufferSize Size of the buffer
 * @return true if time is available and formatted, false otherwise
 */
bool get_formatted_time(char* buffer, size_t bufferSize);

/**
 * @brief Force immediate NTP sync (resets cooldown timer)
 * @return true if sync successful, false otherwise
 */
bool force_sync_ntp();

/**
 * @brief Get timestamp of last successful NTP sync
 * @return millis() timestamp of last sync, or 0 if never synced
 */
unsigned long get_last_ntp_sync_time();

/**
 * @brief Check if system time has been initialized from NTP
 * @return true if time has been set from NTP at least once
 */
bool is_time_synchronized();

#endif // ETHERNET_UTILITIES_H
