/**
 * Network Time Utilities for ESP32
 * 
 * On ESP32, the WiFi classes (WiFiUDP, WiFiClient) work transparently with
 * both WiFi and Ethernet connections. The network stack automatically routes
 * packets through the active interface, so no special "Ethernet-specific" code
 * is needed.
 * 
 * Features:
 * - NTP time synchronization with automatic timezone detection via ipapi.co
 * - Periodic internet connectivity monitoring  
 * - Background FreeRTOS task for automatic updates
 * - Works with WiFi or Ethernet (or both)
 */

#include "ethernet_utilities.h"
#include "../src/config/logging_config.h"
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <mqtt_logger.h>

// ═══════════════════════════════════════════════════════════════════════
// GLOBAL VARIABLES
// ═══════════════════════════════════════════════════════════════════════

// NTP client - WiFiUDP works with both WiFi and Ethernet on ESP32
static WiFiUDP ntp_udp;
static const int NTP_PACKET_SIZE = 48;
static byte ntp_packet_buffer[NTP_PACKET_SIZE];
static const int NTP_LOCAL_PORT = 2390;

// Task handle
static TaskHandle_t ethernet_utils_task_handle = NULL;

// Time sync tracking
static unsigned long last_ntp_sync = 0;
static bool time_initialized = false;
static bool timezone_configured = false;
static unsigned long last_timezone_attempt = 0;

// Internet connectivity status
static volatile bool internet_connected = false;
static unsigned long last_internet_check = 0;

// Public IP address
static String public_ip_address = "";
static unsigned long last_public_ip_check = 0;

// Timezone information
static String detected_timezone_name = "";
static String detected_timezone_abbreviation = "";

// ═══════════════════════════════════════════════════════════════════════
// PRIVATE HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief Send NTP request packet to server
 * @param server NTP server hostname
 * @return true if packet was sent successfully
 */
static bool send_ntp_packet(const char* server) {
    // Zero out the buffer
    memset(ntp_packet_buffer, 0, NTP_PACKET_SIZE);
    
    // Initialize NTP request packet
    ntp_packet_buffer[0] = 0b11100011;  // LI, Version, Mode
    ntp_packet_buffer[1] = 0;           // Stratum
    ntp_packet_buffer[2] = 6;           // Polling Interval
    ntp_packet_buffer[3] = 0xEC;        // Peer Clock Precision
    ntp_packet_buffer[12] = 49;         // Reference ID
    ntp_packet_buffer[13] = 0x4E;
    ntp_packet_buffer[14] = 49;
    ntp_packet_buffer[15] = 52;
    
    // Send packet
    ntp_udp.beginPacket(server, 123);
    ntp_udp.write(ntp_packet_buffer, NTP_PACKET_SIZE);
    return (ntp_udp.endPacket() != 0);
}

/**
 * @brief Get public IP address and timezone information from ipapi.co service
 * @param timezone_out Output parameter for timezone name (e.g., "Europe/London")
 * @param country_out Output parameter for country name
 * @param city_out Output parameter for city name
 * @return Public IP address as string, or empty string if failed
 */
static String get_public_ip_and_timezone(String& timezone_out, String& country_out, String& city_out) {
    if (!is_network_connected()) {
        LOG_ERROR("IP_DETECT", "No network connection!");
        MQTT_LOG_ERROR("IP", "No network connection");
        return "";
    }
    
    const char* host = "ip-api.com";
    const char* path = "/json/?fields=status,timezone";
    
    LOG_INFO("IP_DETECT", "===== PUBLIC IP & TIMEZONE DETECTION START =====");
    LOG_INFO("IP_DETECT", "Connecting to %s...", host);
    MQTT_LOG_INFO("IP", "Connecting to ip-api.com...");
    
    // Check Ethernet is up
    if (!ETH.linkUp()) {
        LOG_ERROR("IP_DETECT", "✗ Ethernet not connected!");
        MQTT_LOG_ERROR("IP", "Ethernet not connected");
        return "";
    }
    
    // Verify we have a valid local IP
    IPAddress local_ip = ETH.localIP();
    if (local_ip == IPAddress(0, 0, 0, 0)) {
        LOG_ERROR("IP_DETECT", "✗ Ethernet has no valid IP address!");
        MQTT_LOG_ERROR("IP", "No valid Ethernet IP");
        return "";
    }
    
    LOG_INFO("IP_DETECT", "Using Ethernet connection (IP: %s)", local_ip.toString().c_str());
    
    // WiFiClient works for Ethernet on ESP32
    WiFiClient client;
    
    LOG_INFO("IP_DETECT", "Attempting to connect to %s:80...", host);
    if (!client.connect(host, 80)) {
        LOG_ERROR("IP_DETECT", "✗ Connection to ip-api.com FAILED!");
        MQTT_LOG_ERROR("IP", "Connection to ip-api.com failed");
        return "";
    }
    
    LOG_INFO("IP_DETECT", "✓ Connected! Sending HTTP request...");
    
    // Small delay to ensure connection is stable
    delay(10);
    
    // Send HTTP GET request
    client.print("GET ");
    client.print(path);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(host);
    client.println("Connection: close");
    client.println();
    
    LOG_INFO("IP_DETECT", "Request sent, waiting for response...");
    
    // Read response
    String response = "";
    bool headersPassed = false;
    unsigned long timeout = millis() + 10000;  // 10 seconds
    int headerCount = 0;
    unsigned long startWait = millis();
    
    // Wait for data to arrive
    while (!client.available() && millis() - startWait < 5000) {
        delay(10);
    }
    
    if (!client.available()) {
        LOG_ERROR("IP_DETECT", "No data received from server after 5 seconds!");
        client.stop();
        return "";
    }
    
    LOG_INFO("IP_DETECT", "Data available, reading response...");
    
    // Read until timeout or no more data (connection can close but data still in buffer)
    while (millis() < timeout) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (!headersPassed) {
                headerCount++;
                if (headerCount == 1) {
                    LOG_INFO("IP_DETECT", "HTTP Status: %s", line.c_str());
                }
                MQTT_LOG_DEBUG("IP", "Header: %s", line.c_str());
                // Check for blank line (headers/body separator) - can be "\r" or empty after trim
                line.trim();
                if (line.length() == 0) {
                    headersPassed = true;
                    LOG_INFO("IP_DETECT", "Headers complete, reading body...");
                    continue;
                }
            } else {
                // We're in the body section
                LOG_INFO("IP_DETECT", "Body line read: %d chars", line.length());
                response += line;
            }
        } else {
            // No data available right now
            if (!client.connected()) {
                // Connection closed and no data left
                LOG_INFO("IP_DETECT", "Connection closed, no more data");
                break;
            }
            delay(10);  // Small delay when no data available
        }
    }
    
    LOG_INFO("IP_DETECT", "Loop exited. Response bytes: %d", response.length());
    client.stop();
    
    LOG_INFO("IP_DETECT", "Response received: %d bytes", response.length());
    LOG_INFO("IP_DETECT", "Raw response (first 200 chars): '%s'", response.substring(0, 200).c_str());
    
    if (response.length() == 0) {
        LOG_ERROR("IP_DETECT", "✗ Empty response from ipapi.co (timeout?)");
        MQTT_LOG_ERROR("IP", "Empty response from ipapi.co");
        return "";
    }
    
    // Clean up response (remove whitespace/carriage returns)
    response.trim();
    
    // Parse JSON response from ipapi.co
    // Example response: {"ip":"1.2.3.4","city":"London","region":"England",
    //                    "country":"GB","country_name":"United Kingdom",
    //                    "timezone":"Europe/London","latitude":51.5074,"longitude":-0.1278}
    LOG_INFO("IP_DETECT", "Parsing JSON...");
    DynamicJsonDocument doc(1024);  // ipapi.co has smaller responses
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
        LOG_ERROR("IP_DETECT", "✗ JSON parse error: %s", error.c_str());
        LOG_ERROR("IP_DETECT", "Response was: %s", response.substring(0, 200).c_str());
        MQTT_LOG_ERROR("IP", "JSON parse error: %s", error.c_str());
        return "";
    }
    
    String ip = doc["ip"].as<String>();
    String timezone = doc["timezone"].as<String>();
    String country = doc["country_name"].as<String>();
    String city = doc["city"].as<String>();
    
    if (ip.length() == 0) {
        LOG_ERROR("IP_DETECT", "✗ No 'ipAddress' field in JSON response");
        MQTT_LOG_ERROR("IP", "No IP field in response");
        return "";
    }
    
    if (timezone.length() == 0) {
        LOG_WARN("IP_DETECT", "No timezone in response, will use UTC");
        timezone = "UTC";
    }
    
    // Return parsed values
    timezone_out = timezone;
    country_out = country;
    city_out = city;
    
    LOG_INFO("IP_DETECT", "✓✓✓ SUCCESS! Public IP: %s ✓✓✓", ip.c_str());
    LOG_INFO("IP_DETECT", "✓ Location: %s, %s", city.c_str(), country.c_str());
    LOG_INFO("IP_DETECT", "✓ Timezone: %s", timezone.c_str());
    LOG_INFO("IP_DETECT", "===== PUBLIC IP & TIMEZONE DETECTION END =====");
    MQTT_LOG_NOTICE("IP", "Detected: %s in %s, %s (TZ: %s)", ip.c_str(), city.c_str(), country.c_str(), timezone.c_str());
    
    return ip;
}



/**
 * @brief Get timezone information using only ipapi.co
 * @return Timezone name (e.g., "Europe/London"), or "UTC" if failed
 */
static String get_timezone_from_location() {
    if (!is_network_connected()) {
        LOG_WARN("TZ_DETECT", "No network connection for timezone detection");
        MQTT_LOG_WARNING("TZ", "No network connection available");
        return "UTC";
    }
    
    LOG_INFO("TZ_DETECT", "===== Starting timezone detection =====");
    MQTT_LOG_INFO("TZ", "Detecting timezone from public IP");
    
    // Get public IP and timezone info from ip-api.com (all in one call)
    LOG_INFO("TZ_DETECT", "Getting IP and timezone from ip-api.com...");
    String timezone_name = "";
    String country = "";
    String city = "";
    
    String ip = get_public_ip_and_timezone(timezone_name, country, city);
    
    if (ip.length() == 0) {
        LOG_ERROR("TZ_DETECT", "Failed to get public IP from ip-api.com");
        MQTT_LOG_ERROR("TZ", "Failed to get public IP");
        return "UTC";
    }
    
    // Update global public IP
    public_ip_address = ip;
    last_public_ip_check = millis();
    
    if (timezone_name.length() == 0) {
        LOG_ERROR("TZ_DETECT", "Failed to get timezone name from ip-api.com");
        MQTT_LOG_ERROR("TZ", "Timezone detection failed");
        return "UTC";
    }
    
    detected_timezone_name = timezone_name;
    detected_timezone_abbreviation = "";  // Will be set after configureTime()
    
    LOG_INFO("TZ_DETECT", "✓✓✓ SUCCESS! ✓✓✓");
    LOG_INFO("TZ_DETECT", "✓ Public IP: %s", public_ip_address.c_str());
    LOG_INFO("TZ_DETECT", "✓ Location: %s, %s", city.c_str(), country.c_str());
    LOG_INFO("TZ_DETECT", "✓ Timezone: %s", timezone_name.c_str());
    LOG_INFO("TZ_DETECT", "===== TIMEZONE DETECTION END =====");
    MQTT_LOG_NOTICE("TZ", "Detected: %s in %s, %s", timezone_name.c_str(), city.c_str(), country.c_str());
    
    return timezone_name;
}

/**
 * @brief Network utilities task - handles periodic NTP sync and connectivity checks
 */
static void ethernet_utilities_task(void* parameter) {
    TickType_t last_ntp_check = 0;
    TickType_t last_ping_check = 0;
    
    LOG_INFO("NTP_UTILS", "Network utilities task started");
    
    // Wait for network connection
    while (!is_network_connected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // Do initial NTP sync
    get_ntp_time();
    
    // State tracking
    bool timezone_detected = false;
    int timezone_retry_count = 0;
    const int TIMEZONE_RETRY_DELAY_MS = 30000;      // 30 seconds between timezone retries
    
    while (true) {
        TickType_t current_time = xTaskGetTickCount();
        
        // Timezone auto-detection (combines public IP detection and timezone lookup)
        if (!timezone_detected && is_network_connected()) {
            unsigned long time_since_last_attempt = millis() - last_timezone_attempt;
            if (time_since_last_attempt >= TIMEZONE_RETRY_DELAY_MS || last_timezone_attempt == 0) {
                last_timezone_attempt = millis();
                timezone_retry_count++;
                
                LOG_INFO("NET_UTILS", "===== Timezone & IP detection attempt #%d =====", timezone_retry_count);
                MQTT_LOG_INFO("TZ", "Detection attempt #%d", timezone_retry_count);
                
                // Show local IP addresses
                IPAddress eth_ip = ETH.localIP();
                IPAddress wifi_ip = WiFi.localIP();
                if (eth_ip != IPAddress(0, 0, 0, 0)) {
                    LOG_INFO("NET_UTILS", "Local Ethernet IP: %s", eth_ip.toString().c_str());
                }
                if (wifi_ip != IPAddress(0, 0, 0, 0)) {
                    LOG_INFO("NET_UTILS", "Local WiFi IP: %s", wifi_ip.toString().c_str());
                }
                
                if (configure_timezone_from_location()) {
                    timezone_detected = true;
                    LOG_INFO("NET_UTILS", "✓✓✓ SUCCESS! Timezone configured: %s (%s) ✓✓✓",
                             detected_timezone_name.c_str(),
                             detected_timezone_abbreviation.c_str());
                    MQTT_LOG_NOTICE("TZ", "Configured: %s (%s)",
                                    detected_timezone_name.c_str(),
                                    detected_timezone_abbreviation.c_str());
                    // Re-sync time to apply new timezone
                    last_ntp_sync = 0;
                    get_ntp_time();
                } else {
                    LOG_WARN("NET_UTILS", "Timezone detection attempt #%d FAILED - will retry in %d seconds",
                             timezone_retry_count, TIMEZONE_RETRY_DELAY_MS/1000);
                    MQTT_LOG_WARNING("TZ", "Detection failed, retry #%d", timezone_retry_count);
                }
            }
        }
        
        // NTP sync every 30 minutes
        if (current_time - last_ntp_check >= pdMS_TO_TICKS(NTP_SYNC_INTERVAL_MS)) {
            last_ntp_check = current_time;
            if (is_network_connected()) {
                get_ntp_time();
            }
        }
        
        // Internet connectivity check every 60 seconds
        if (current_time - last_ping_check >= pdMS_TO_TICKS(INTERNET_CHECK_INTERVAL_MS)) {
            last_ping_check = current_time;
            
            if (is_network_connected()) {
                bool was_connected = internet_connected;
                internet_connected = test_internet_connectivity();
                
                if (internet_connected != was_connected) {
                    LOG_INFO("NTP_UTILS", "Internet: %s", 
                                internet_connected ? "ONLINE" : "OFFLINE");
                }
            } else {
                internet_connected = false;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ═══════════════════════════════════════════════════════════════════════
// PUBLIC API IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════

bool init_ethernet_utilities() {
    LOG_INFO("NTP_UTILS", "Initializing network time utilities...");
    ntp_udp.begin(NTP_LOCAL_PORT);
    LOG_INFO("NTP_UTILS", "NTP client ready on port %d", NTP_LOCAL_PORT);
    return true;
}

bool start_ethernet_utilities_task() {
    if (ethernet_utils_task_handle != NULL) {
        LOG_INFO("NTP_UTILS", "Task already running");
        return true;
    }
    
    BaseType_t result = xTaskCreatePinnedToCore(
        ethernet_utilities_task,
        "NetTimeUtils",
        ETHERNET_UTILS_TASK_STACK_SIZE,
        NULL,
        ETHERNET_UTILS_TASK_PRIORITY,
        &ethernet_utils_task_handle,
        ETHERNET_UTILS_TASK_CORE
    );
    
    if (result == pdPASS) {
        LOG_INFO("NTP_UTILS", "Background task started");
        return true;
    } else {
        LOG_INFO("NTP_UTILS", "Failed to start task");
        return false;
    }
}

void stop_ethernet_utilities_task() {
    if (ethernet_utils_task_handle != NULL) {
        vTaskDelete(ethernet_utils_task_handle);
        ethernet_utils_task_handle = NULL;
        LOG_INFO("NTP_UTILS", "Background task stopped");
    }
}

bool get_ntp_time() {
    // Configure default UTC timezone on first call (will be auto-detected later)
    if (!timezone_configured) {
        setenv("TZ", "UTC0", 1);
        tzset();
        timezone_configured = true;
        LOG_INFO("NTP_UTILS", "Initial timezone: UTC (will auto-detect)");
    }
    
    // Skip if recently synced
    if (time_initialized && (millis() - last_ntp_sync < NTP_SYNC_INTERVAL_MS)) {
        return true;
    }
    
    if (!is_network_connected()) {
        LOG_INFO("NTP_UTILS", "No network connection");
        return false;
    }
    
    LOG_INFO("NTP_UTILS", "Syncing time from NTP...");
    
    const char* servers[] = {NTP_SERVER1, NTP_SERVER2};
    
    for (int i = 0; i < 2; i++) {
        const char* server = servers[i];
        LOG_INFO("NTP_UTILS", "Trying %s...", server);
        
        if (!send_ntp_packet(server)) {
            continue;
        }
        
        // Wait for response (5 second timeout)
        unsigned long start = millis();
        int packet_size = 0;
        while ((millis() - start < 5000) && (packet_size = ntp_udp.parsePacket()) == 0) {
            delay(10);
        }
        
        if (packet_size >= NTP_PACKET_SIZE) {
            ntp_udp.read(ntp_packet_buffer, NTP_PACKET_SIZE);
            
            // Extract NTP timestamp and convert to Unix epoch
            unsigned long high = word(ntp_packet_buffer[40], ntp_packet_buffer[41]);
            unsigned long low = word(ntp_packet_buffer[42], ntp_packet_buffer[43]);
            unsigned long ntp_time = (high << 16) | low;
            unsigned long epoch = ntp_time - 2208988800UL;
            
            // Set system time
            struct timeval tv = {.tv_sec = (time_t)epoch, .tv_usec = 0};
            settimeofday(&tv, NULL);
            
            time_t now = epoch;
            struct tm* local_time = localtime(&now);
            
            const char* tz_display = detected_timezone_abbreviation.length() > 0 ? 
                                     detected_timezone_abbreviation.c_str() : "UTC";
            
            LOG_INFO("NTP_UTILS", "Time set: %04d-%02d-%02d %02d:%02d:%02d %s",
                         local_time->tm_year + 1900,
                         local_time->tm_mon + 1,
                         local_time->tm_mday,
                         local_time->tm_hour,
                         local_time->tm_min,
                         local_time->tm_sec,
                         tz_display);
            
            MQTT_LOG_INFO("NTP", "Time synced: %04d-%02d-%02d %02d:%02d:%02d %s",
                          local_time->tm_year + 1900,
                          local_time->tm_mon + 1,
                          local_time->tm_mday,
                          local_time->tm_hour,
                          local_time->tm_min,
                          local_time->tm_sec,
                          tz_display);
            
            time_initialized = true;
            last_ntp_sync = millis();
            return true;
        }
    }
    
    LOG_INFO("NTP_UTILS", "All NTP servers failed");
    return false;
}

bool test_internet_connectivity() {
    if (!is_network_connected()) return false;
    
    WiFiClient client;
    bool connected = client.connect(INTERNET_TEST_HOST, INTERNET_TEST_PORT);
    client.stop();
    return connected;
}

bool is_network_connected() {
    // Check Ethernet first (if available)
    IPAddress eth_ip = ETH.localIP();
    if (eth_ip != IPAddress(0, 0, 0, 0)) {
        return true;
    }
    
    // Check WiFi as fallback
    IPAddress wifi_ip = WiFi.localIP();
    return (wifi_ip != IPAddress(0, 0, 0, 0));
}

bool is_internet_reachable() {
    return internet_connected;
}

bool configure_timezone_from_location() {
    LOG_INFO("TZ_CONFIG", "Getting timezone from location...");
    String tz_name = get_timezone_from_location();
    
    LOG_INFO("TZ_CONFIG", "Received timezone name: '%s'", tz_name.c_str());
    
    if (tz_name.length() == 0 || tz_name == "UTC") {
        LOG_ERROR("TZ_CONFIG", "REJECTED: Got default UTC (detection failed)");
        MQTT_LOG_ERROR("TZ", "Detection failed - got UTC default");
        return false;
    }
    
    // ESP32 supports POSIX timezone strings directly
    // We can use configTime() with the timezone name, or map common timezones to POSIX strings
    // For simplicity, use common POSIX timezone strings based on the timezone name
    
    String posix_tz = "";
    
    // Map common timezone names to POSIX format
    // Format: STD offset [DST [offset],start[/time],end[/time]]
    if (tz_name.startsWith("Europe/London")) {
        posix_tz = "GMT0BST,M3.5.0/1,M10.5.0";
        detected_timezone_abbreviation = "GMT";
    } else if (tz_name.startsWith("Europe/Paris") || tz_name.startsWith("Europe/Berlin") || 
               tz_name.startsWith("Europe/Rome") || tz_name.startsWith("Europe/Madrid")) {
        posix_tz = "CET-1CEST,M3.5.0,M10.5.0/3";
        detected_timezone_abbreviation = "CET";
    } else if (tz_name.startsWith("America/New_York")) {
        posix_tz = "EST5EDT,M3.2.0,M11.1.0";
        detected_timezone_abbreviation = "EST";
    } else if (tz_name.startsWith("America/Chicago")) {
        posix_tz = "CST6CDT,M3.2.0,M11.1.0";
        detected_timezone_abbreviation = "CST";
    } else if (tz_name.startsWith("America/Denver")) {
        posix_tz = "MST7MDT,M3.2.0,M11.1.0";
        detected_timezone_abbreviation = "MST";
    } else if (tz_name.startsWith("America/Los_Angeles")) {
        posix_tz = "PST8PDT,M3.2.0,M11.1.0";
        detected_timezone_abbreviation = "PST";
    } else if (tz_name.startsWith("Australia/Sydney")) {
        posix_tz = "AEST-10AEDT,M10.1.0,M4.1.0/3";
        detected_timezone_abbreviation = "AEST";
    } else if (tz_name.startsWith("Asia/Tokyo")) {
        posix_tz = "JST-9";
        detected_timezone_abbreviation = "JST";
    } else if (tz_name.startsWith("Asia/Shanghai") || tz_name.startsWith("Asia/Hong_Kong")) {
        posix_tz = "CST-8";
        detected_timezone_abbreviation = "CST";
    } else if (tz_name.startsWith("Asia/Dubai")) {
        posix_tz = "GST-4";
        detected_timezone_abbreviation = "GST";
    } else {
        // For unknown timezones, just use UTC
        LOG_WARN("TZ_CONFIG", "Unknown timezone '%s', using UTC", tz_name.c_str());
        posix_tz = "UTC0";
        detected_timezone_abbreviation = "UTC";
    }
    
    // Apply detected timezone
    setenv("TZ", posix_tz.c_str(), 1);
    tzset();
    LOG_INFO("TZ_CONFIG", "✓ Timezone configured: %s -> %s", tz_name.c_str(), posix_tz.c_str());
    MQTT_LOG_NOTICE("TZ", "Configured: %s (%s)", tz_name.c_str(), posix_tz.c_str());
    return true;
}

bool get_formatted_time(char* buffer, size_t buffer_size) {
    if (!time_initialized) {
        snprintf(buffer, buffer_size, "Time not synced");
        return false;
    }
    
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    const char* tz = detected_timezone_abbreviation.length() > 0 ? 
                     detected_timezone_abbreviation.c_str() : "UTC";
    
    snprintf(buffer, buffer_size, "%02d/%02d/%04d %02d:%02d:%02d %s",
             t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
             t->tm_hour, t->tm_min, t->tm_sec, tz);
    return true;
}

bool force_sync_ntp() {
    last_ntp_sync = 0;
    return get_ntp_time();
}

unsigned long get_last_ntp_sync_time() {
    return last_ntp_sync;
}

bool is_time_synchronized() {
    return time_initialized;
}
