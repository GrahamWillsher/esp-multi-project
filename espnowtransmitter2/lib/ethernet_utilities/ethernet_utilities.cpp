/**
 * Network Time Utilities for ESP32
 * 
 * On ESP32, the WiFi classes (WiFiUDP, WiFiClient) work transparently with
 * both WiFi and Ethernet connections. The network stack automatically routes
 * packets through the active interface, so no special "Ethernet-specific" code
 * is needed.
 * 
 * Features:
 * - NTP time synchronization with automatic timezone detection
 * - Periodic internet connectivity monitoring  
 * - Background FreeRTOS task for automatic updates
 * - Works with WiFi or Ethernet (or both)
 */

#include "ethernet_utilities.h"
#include <ArduinoJson.h>
#include <WiFiClient.h>

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
 * @brief Get timezone information from IP geolocation service
 * @return POSIX timezone string, or "UTC0" if failed
 */
static String get_timezone_from_location() {
    if (!is_network_connected()) {
        Serial.println("[NTP_UTILS] No network connection for timezone detection");
        return "UTC0";
    }
    
    const char* host = "worldtimeapi.org";
    const char* path = "/api/ip";
    
    WiFiClient client;
    Serial.printf("[NTP_UTILS] Connecting to %s...\n", host);
    
    if (!client.connect(host, 80)) {
        Serial.printf("[NTP_UTILS] Failed to connect to %s\n", host);
        return "UTC0";
    }
    
    // Send HTTP GET request
    client.print("GET ");
    client.print(path);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(host);
    client.println("Connection: close");
    client.println();
    
    // Read response (skip headers, get JSON body)
    String response = "";
    bool headersPassed = false;
    unsigned long timeout = millis() + 10000;
    
    while (client.connected() && millis() < timeout) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (!headersPassed && line == "\r") {
                headersPassed = true;
                continue;
            }
            if (headersPassed) {
                response += line;
            }
        }
    }
    client.stop();
    
    if (response.length() == 0) {
        Serial.println("[NTP_UTILS] No response from timezone service");
        return "UTC0";
    }
    
    // Parse JSON response
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
        Serial.printf("[NTP_UTILS] JSON parsing failed: %s\n", error.c_str());
        return "UTC0";
    }
    
    // Extract timezone info
    String timezone_name = doc["timezone"].as<String>();
    String abbreviation = doc["abbreviation"].as<String>();
    int utc_offset = doc["utc_offset"].as<int>();
    bool dst = doc["dst"].as<bool>();
    int dst_offset = doc["dst_offset"].as<int>();
    String dst_from = doc["dst_from"].as<String>();
    String dst_until = doc["dst_until"].as<String>();
    
    detected_timezone_name = timezone_name;
    detected_timezone_abbreviation = abbreviation;
    
    Serial.printf("[NTP_UTILS] Detected timezone: %s (%s), offset: %+d hours, DST: %s\n", 
                 timezone_name.c_str(), abbreviation.c_str(), utc_offset/3600,
                 dst ? "active" : "inactive");
    
    // Generate POSIX timezone string (POSIX uses opposite sign for offset)
    int offsetHours = utc_offset / 3600;
    int offsetMinutes = abs(utc_offset % 3600) / 60;
    
    String posixTz = abbreviation.length() > 0 ? abbreviation : "UTC";
    
    if (utc_offset == 0) {
        posixTz += "0";
    } else {
        posixTz += String(-offsetHours);  // POSIX format inverts sign
        if (offsetMinutes > 0) {
            posixTz += ":" + String(offsetMinutes);
        }
    }
    
    // Add comprehensive DST rules for all major regions
    if (dst_offset != 0 || dst) {
        int dstOffsetHours = (utc_offset + dst_offset) / 3600;
        
        // Generate DST abbreviation
        String dstAbbr = abbreviation;
        if (dstAbbr.endsWith("ST")) {
            dstAbbr.remove(dstAbbr.length() - 2);
            dstAbbr += "DT";
        } else if (dstAbbr.endsWith("T")) {
            dstAbbr.remove(dstAbbr.length() - 1);
            dstAbbr += "DT";
        } else {
            dstAbbr += "DT";
        }
        
        posixTz += dstAbbr + String(-dstOffsetHours);
        
        // Comprehensive DST rules for all major regions
        // Format: ,Mm.w.d/time,Mm.w.d/time where:
        // m = month, w = week (1-5, 5=last), d = day of week (0=Sunday), time = hour
        
        // UK / GMT / BST (Last Sunday March 1AM - Last Sunday October 2AM)
        if (timezone_name.indexOf("London") >= 0 || timezone_name.indexOf("Europe/London") >= 0 ||
            abbreviation == "GMT" || abbreviation == "BST") {
            posixTz += ",M3.5.0/1,M10.5.0/2";
            Serial.println("[NTP_UTILS] Applied UK/GMT DST rules");
        }
        // European Union (Last Sunday March 2AM - Last Sunday October 3AM)
        else if (timezone_name.indexOf("Europe") >= 0 && timezone_name.indexOf("London") < 0) {
            posixTz += ",M3.5.0/2,M10.5.0/3";
            Serial.println("[NTP_UTILS] Applied EU DST rules");
        }
        // US & Canada (2nd Sunday March 2AM - 1st Sunday November 2AM)
        else if (timezone_name.indexOf("America/New_York") >= 0 || 
                 timezone_name.indexOf("America/Chicago") >= 0 ||
                 timezone_name.indexOf("America/Denver") >= 0 ||
                 timezone_name.indexOf("America/Los_Angeles") >= 0 ||
                 timezone_name.indexOf("America/Anchorage") >= 0 ||
                 timezone_name.indexOf("America/Toronto") >= 0 ||
                 timezone_name.indexOf("America/Vancouver") >= 0) {
            posixTz += ",M3.2.0/2,M11.1.0/2";
            Serial.println("[NTP_UTILS] Applied US/Canada DST rules");
        }
        // Australia (1st Sunday October 2AM - 1st Sunday April 3AM)
        else if (timezone_name.indexOf("Australia/Sydney") >= 0 ||
                 timezone_name.indexOf("Australia/Melbourne") >= 0 ||
                 timezone_name.indexOf("Australia/Canberra") >= 0 ||
                 timezone_name.indexOf("Australia/Hobart") >= 0 ||
                 timezone_name.indexOf("Australia/Adelaide") >= 0) {
            posixTz += ",M10.1.0/2,M4.1.0/3";
            Serial.println("[NTP_UTILS] Applied Australia (southeast) DST rules");
        }
        // New Zealand (Last Sunday September 2AM - 1st Sunday April 3AM)
        else if (timezone_name.indexOf("Pacific/Auckland") >= 0 ||
                 timezone_name.indexOf("New_Zealand") >= 0) {
            posixTz += ",M9.5.0/2,M4.1.0/3";
            Serial.println("[NTP_UTILS] Applied New Zealand DST rules");
        }
        // Brazil (3rd Sunday October 0AM - 3rd Sunday February 0AM)
        else if (timezone_name.indexOf("America/Sao_Paulo") >= 0) {
            posixTz += ",M10.3.0/0,M2.3.0/0";
            Serial.println("[NTP_UTILS] Applied Brazil DST rules");
        }
        // Chile (2nd Saturday August 24:00 - 2nd Saturday May 24:00)
        else if (timezone_name.indexOf("America/Santiago") >= 0) {
            posixTz += ",M8.2.6/24,M5.2.6/24";
            Serial.println("[NTP_UTILS] Applied Chile DST rules");
        }
        // Israel (Last Friday March 2AM - Last Sunday October 2AM)
        else if (timezone_name.indexOf("Asia/Jerusalem") >= 0) {
            posixTz += ",M3.5.5/2,M10.5.0/2";
            Serial.println("[NTP_UTILS] Applied Israel DST rules");
        }
        // Mexico (1st Sunday April 2AM - Last Sunday October 2AM)
        else if (timezone_name.indexOf("America/Mexico_City") >= 0 ||
                 timezone_name.indexOf("America/Cancun") >= 0) {
            posixTz += ",M4.1.0/2,M10.5.0/2";
            Serial.println("[NTP_UTILS] Applied Mexico DST rules");
        }
        // Cuba (2nd Sunday March 0AM - 1st Sunday November 1AM)
        else if (timezone_name.indexOf("America/Havana") >= 0) {
            posixTz += ",M3.2.0/0,M11.1.0/1";
            Serial.println("[NTP_UTILS] Applied Cuba DST rules");
        }
        // Iran (Day 1 Farvardin 0AM - Day 1 Mehr 0AM) - Approximate to March 21 - September 22
        else if (timezone_name.indexOf("Asia/Tehran") >= 0) {
            posixTz += ",M3.3.2/0,M9.3.2/0";
            Serial.println("[NTP_UTILS] Applied Iran DST rules (approximate)");
        }
        // Generic Northern Hemisphere fallback (2nd Sunday March 2AM - 1st Sunday November 2AM)
        else if (offsetHours >= 0 || timezone_name.indexOf("America") >= 0) {
            posixTz += ",M3.2.0/2,M11.1.0/2";
            Serial.println("[NTP_UTILS] Applied generic Northern Hemisphere DST rules");
        }
        // Generic Southern Hemisphere fallback (1st Sunday October 2AM - 1st Sunday April 2AM)
        else {
            posixTz += ",M10.1.0/2,M4.1.0/2";
            Serial.println("[NTP_UTILS] Applied generic Southern Hemisphere DST rules");
        }
        
        Serial.printf("[NTP_UTILS] DST offset: %+d hours\n", dst_offset/3600);
    }
    
    Serial.printf("[NTP_UTILS] POSIX timezone: %s\n", posixTz.c_str());
    return posixTz;
}

/**
 * @brief Network utilities task - handles periodic NTP sync and connectivity checks
 */
static void ethernet_utilities_task(void* parameter) {
    TickType_t last_ntp_check = 0;
    TickType_t last_ping_check = 0;
    
    Serial.println("[NTP_UTILS] Network utilities task started");
    
    // Wait for network to stabilize, then do initial NTP sync
    vTaskDelay(pdMS_TO_TICKS(2000));
    get_ntp_time();
    
    while (true) {
        TickType_t current_time = xTaskGetTickCount();
        
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
                    Serial.printf("[NTP_UTILS] Internet: %s\n", 
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
    Serial.println("[NTP_UTILS] Initializing network time utilities...");
    ntp_udp.begin(NTP_LOCAL_PORT);
    Serial.printf("[NTP_UTILS] NTP client ready on port %d\n", NTP_LOCAL_PORT);
    return true;
}

bool start_ethernet_utilities_task() {
    if (ethernet_utils_task_handle != NULL) {
        Serial.println("[NTP_UTILS] Task already running");
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
        Serial.println("[NTP_UTILS] Background task started");
        return true;
    } else {
        Serial.println("[NTP_UTILS] Failed to start task");
        return false;
    }
}

void stop_ethernet_utilities_task() {
    if (ethernet_utils_task_handle != NULL) {
        vTaskDelete(ethernet_utils_task_handle);
        ethernet_utils_task_handle = NULL;
        Serial.println("[NTP_UTILS] Background task stopped");
    }
}

bool get_ntp_time() {
    // Configure timezone on first call or retry every 30 minutes if failed
    bool should_retry_timezone = false;
    
    if (!timezone_configured) {
        should_retry_timezone = true;
    } else if (detected_timezone_abbreviation.length() == 0 || 
               detected_timezone_abbreviation == "UTC") {
        // Retry if we're using default UTC and enough time has passed
        if (millis() - last_timezone_attempt >= NTP_SYNC_INTERVAL_MS) {
            should_retry_timezone = true;
            Serial.println("[NTP_UTILS] Retrying timezone detection...");
        }
    }
    
    if (should_retry_timezone) {
        last_timezone_attempt = millis();
        if (configure_timezone_from_location()) {
            timezone_configured = true;
        } else {
            Serial.println("[NTP_UTILS] Timezone detection failed, using UTC (will retry in 30 min)");
            if (!timezone_configured) {
                setenv("TZ", "UTC0", 1);
                tzset();
                timezone_configured = true;
            }
        }
    }
    
    // Skip if recently synced
    if (time_initialized && (millis() - last_ntp_sync < NTP_SYNC_INTERVAL_MS)) {
        return true;
    }
    
    if (!is_network_connected()) {
        Serial.println("[NTP_UTILS] No network connection");
        return false;
    }
    
    Serial.println("[NTP_UTILS] Syncing time from NTP...");
    
    const char* servers[] = {NTP_SERVER1, NTP_SERVER2};
    
    for (int i = 0; i < 2; i++) {
        const char* server = servers[i];
        Serial.printf("[NTP_UTILS] Trying %s...\n", server);
        
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
            
            Serial.printf("[NTP_UTILS] Time set: %04d-%02d-%02d %02d:%02d:%02d %s\n",
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
    
    Serial.println("[NTP_UTILS] All NTP servers failed");
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
    String tz = get_timezone_from_location();
    if (tz == "UTC0") return false;
    
    setenv("TZ", tz.c_str(), 1);
    tzset();
    Serial.printf("[NTP_UTILS] Timezone set: %s\n", tz.c_str());
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
