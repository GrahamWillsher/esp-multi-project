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
#include <log_routed.h>
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

// Public IP address
static String public_ip_address = "";
static unsigned long last_public_ip_check = 0;

// Timezone information
static String detected_timezone_name = "";
static String detected_timezone_abbreviation = "";

constexpr uint32_t kGeoLookupPerProviderTimeoutMs = 5000;
constexpr uint32_t kGeoLookupIdleTimeoutMs = 1000;
constexpr size_t kGeoResponseCapacity = 768;

struct TimezoneLookupResult {
    char timezone_name[64]{};
    char timezone_abbreviation[16]{};
    char city[48]{};
    char country[48]{};
    char public_ip[32]{};
};

struct GeoProvider {
    const char* host;
    const char* path;
    bool (*parser)(JsonDocument&, TimezoneLookupResult&);
};

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

static bool read_http_body(WiFiClient& client,
                           char* body,
                           size_t body_capacity,
                           uint32_t total_timeout_ms,
                           uint32_t idle_timeout_ms) {
    if (body == nullptr || body_capacity < 2) {
        return false;
    }

    bool headers_passed = false;
    size_t body_length = 0;
    char header_line[192]{};
    size_t header_len = 0;

    const unsigned long start_time = millis();
    unsigned long last_data_time = start_time;

    while ((millis() - start_time) < total_timeout_ms) {
        while (client.available()) {
            const char ch = static_cast<char>(client.read());
            last_data_time = millis();

            if (!headers_passed) {
                if (ch == '\n') {
                    if (header_len == 0) {
                        headers_passed = true;
                    }
                    header_len = 0;
                } else if (ch != '\r') {
                    if (header_len < (sizeof(header_line) - 1)) {
                        header_line[header_len++] = ch;
                    }
                }
                continue;
            }

            if ((body_length + 1) >= body_capacity) {
                LOG_WARN("TZ_LOOKUP", "Body truncated (capacity=%u)", static_cast<unsigned>(body_capacity));
                return false;
            }
            body[body_length++] = ch;
        }

        if (headers_passed && !client.connected() && !client.available()) {
            break;
        }

        if ((millis() - last_data_time) > idle_timeout_ms) {
            break;
        }

        delay(5);
    }

    body[body_length] = '\0';
    return headers_passed && body_length > 0;
}

static bool parse_ip_api_response(JsonDocument& doc, TimezoneLookupResult& out) {
    const char* status = doc["status"] | "";
    if (strcmp(status, "success") != 0) {
        return false;
    }

    strlcpy(out.timezone_name, doc["timezone"] | "", sizeof(out.timezone_name));
    strlcpy(out.city, doc["city"] | "", sizeof(out.city));
    strlcpy(out.country, doc["country"] | "", sizeof(out.country));
    strlcpy(out.public_ip, doc["query"] | "", sizeof(out.public_ip));
    out.timezone_abbreviation[0] = '\0';

    return out.timezone_name[0] != '\0';
}

static bool parse_worldtime_response(JsonDocument& doc, TimezoneLookupResult& out) {
    strlcpy(out.timezone_name, doc["timezone"] | "", sizeof(out.timezone_name));
    strlcpy(out.timezone_abbreviation, doc["abbreviation"] | "", sizeof(out.timezone_abbreviation));
    strlcpy(out.public_ip, doc["client_ip"] | "", sizeof(out.public_ip));
    return out.timezone_name[0] != '\0';
}

static bool http_get_json(const GeoProvider& provider,
                          StaticJsonDocument<768>& doc,
                          TimezoneLookupResult& out_result) {
    WiFiClient client;
    if (!client.connect(provider.host, 80)) {
        LOG_WARN("TZ_LOOKUP", "Connect failed: %s", provider.host);
        return false;
    }

    client.print("GET ");
    client.print(provider.path);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(provider.host);
    client.println("Connection: close");
    client.println();

    static char body[kGeoResponseCapacity];
    body[0] = '\0';
    if (!read_http_body(client,
                        body,
                        sizeof(body),
                        kGeoLookupPerProviderTimeoutMs,
                        kGeoLookupIdleTimeoutMs)) {
        client.stop();
        LOG_WARN("TZ_LOOKUP", "No valid body from %s", provider.host);
        return false;
    }
    client.stop();

    DeserializationError error = deserializeJson(doc, body);
    if (error) {
        LOG_WARN("TZ_LOOKUP", "JSON parse failed for %s: %s", provider.host, error.c_str());
        return false;
    }

    return provider.parser(doc, out_result);
}

static bool lookup_timezone_with_fallback(TimezoneLookupResult& out) {
    static const GeoProvider kProviders[] = {
        {"ip-api.com", "/json/?fields=status,timezone,country,city,query", parse_ip_api_response},
        {"worldtimeapi.org", "/api/ip", parse_worldtime_response},
        {"timeapi.world", "/api/ip", parse_worldtime_response},
    };

    for (const auto& provider : kProviders) {
        StaticJsonDocument<768> doc;
        TimezoneLookupResult candidate{};

        LOG_INFO("TZ_LOOKUP", "Trying provider: %s", provider.host);
        if (http_get_json(provider, doc, candidate)) {
            out = candidate;
            LOG_INFO("TZ_LOOKUP", "Provider success: %s -> %s", provider.host, out.timezone_name);
            return true;
        }

        LOG_WARN("TZ_LOOKUP", "Provider failed: %s", provider.host);
    }

    return false;
}

static bool map_timezone_name_to_posix(const char* tz_name,
                                       char* posix_tz,
                                       size_t posix_tz_len,
                                       char* tz_abbrev,
                                       size_t tz_abbrev_len) {
    if (tz_name == nullptr || posix_tz == nullptr || tz_abbrev == nullptr ||
        posix_tz_len == 0 || tz_abbrev_len == 0) {
        return false;
    }

    struct Mapping {
        const char* prefix;
        const char* posix;
        const char* abbrev;
    };

    static const Mapping kMappings[] = {
        {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0", "GMT"},
        {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3", "CET"},
        {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3", "CET"},
        {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3", "CET"},
        {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3", "CET"},
        {"America/New_York", "EST5EDT,M3.2.0,M11.1.0", "EST"},
        {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0", "CST"},
        {"America/Denver", "MST7MDT,M3.2.0,M11.1.0", "MST"},
        {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0", "PST"},
        {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3", "AEST"},
        {"Asia/Tokyo", "JST-9", "JST"},
        {"Asia/Shanghai", "CST-8", "CST"},
        {"Asia/Hong_Kong", "CST-8", "CST"},
        {"Asia/Dubai", "GST-4", "GST"},
        {"UTC", "UTC0", "UTC"},
    };

    for (const auto& mapping : kMappings) {
        if (strncmp(tz_name, mapping.prefix, strlen(mapping.prefix)) == 0) {
            strlcpy(posix_tz, mapping.posix, posix_tz_len);
            strlcpy(tz_abbrev, mapping.abbrev, tz_abbrev_len);
            return true;
        }
    }

    strlcpy(posix_tz, "UTC0", posix_tz_len);
    strlcpy(tz_abbrev, "UTC", tz_abbrev_len);
    return false;
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

                UBaseType_t stack_words = uxTaskGetStackHighWaterMark(nullptr);
                LOG_INFO("NET_UTILS", "Task stack headroom before TZ lookup: %u bytes",
                         static_cast<unsigned>(stack_words * sizeof(StackType_t)));
                
                LOG_INFO("NET_UTILS", "===== Timezone & IP detection attempt #%d =====", timezone_retry_count);
                
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
                    log_routed(LogSink::Mqtt,
                               RoutedLevel::Notice,
                               "TZ",
                               "Configured: %s (%s)",
                               detected_timezone_name.c_str(),
                               detected_timezone_abbreviation.c_str());
                    // Re-sync time to apply new timezone
                    last_ntp_sync = 0;
                    get_ntp_time();
                } else {
                    LOG_WARN("NET_UTILS", "Timezone detection attempt #%d FAILED - will retry in %d seconds",
                             timezone_retry_count, TIMEZONE_RETRY_DELAY_MS/1000);
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
    LOG_INFO("TZ_CONFIG", "Getting timezone from location with fallback...");

    if (!is_network_connected()) {
        LOG_WARN("TZ_CONFIG", "No network connection for timezone lookup");
        return false;
    }

    TimezoneLookupResult result{};
    if (!lookup_timezone_with_fallback(result)) {
        LOG_ERROR("TZ_CONFIG", "All timezone providers failed");
        return false;
    }

    char posix_tz[64]{};
    char tz_abbrev[16]{};
    const bool known_mapping = map_timezone_name_to_posix(result.timezone_name,
                                                           posix_tz,
                                                           sizeof(posix_tz),
                                                           tz_abbrev,
                                                           sizeof(tz_abbrev));

    if (!known_mapping) {
        LOG_WARN("TZ_CONFIG", "Unknown timezone mapping: %s (fallback UTC0)", result.timezone_name);
    }

    setenv("TZ", posix_tz, 1);
    tzset();

    detected_timezone_name = result.timezone_name;
    detected_timezone_abbreviation = tz_abbrev;
    public_ip_address = result.public_ip;
    last_public_ip_check = millis();

    LOG_INFO("TZ_CONFIG", "Timezone configured: %s -> %s", result.timezone_name, posix_tz);
    log_routed(LogSink::Mqtt,
               RoutedLevel::Notice,
               "TZ",
               "Configured: %s (%s)",
               result.timezone_name,
               posix_tz);
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
