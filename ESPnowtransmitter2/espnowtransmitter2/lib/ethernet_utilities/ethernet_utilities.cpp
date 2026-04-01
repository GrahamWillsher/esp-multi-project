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
#include <ethernet_config.h>
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
static bool timezone_auto_detected = false;
static unsigned long last_timezone_attempt = 0;
static int16_t cached_utc_offset_min = 0;  // Cached UTC offset in minutes; refreshed on each TZ update

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
constexpr uint32_t kTimezoneRetryDelayMs = 30000;
constexpr uint32_t kTimezoneRefreshIntervalMs = NTP_SYNC_INTERVAL_MS;

struct TimezoneLookupResult {
    char timezone_name[64]{};
    char timezone_abbreviation[16]{};
    char city[48]{};
    char country[48]{};
    char public_ip[32]{};
    int32_t utc_offset_seconds = 0;
    bool utc_offset_valid = false;
};

struct GeoProvider {
    const char* host;
    const char* path;
    bool (*parser)(JsonDocument&, TimezoneLookupResult&);
};

static bool configure_timezone_from_location_internal(bool* out_changed);

static void refresh_detected_timezone_abbreviation_from_system_time() {
    time_t now = time(nullptr);
    if (now <= 0) {
        return;
    }

    struct tm local_tm{};
    localtime_r(&now, &local_tm);

    char tz_buf[16] = {0};
    if (strftime(tz_buf, sizeof(tz_buf), "%Z", &local_tm) > 0) {
        detected_timezone_abbreviation = tz_buf;
    }
}

static bool parse_strftime_utc_offset_min(const char* tz_offset, int16_t* out_offset_min) {
    if (tz_offset == nullptr || out_offset_min == nullptr) {
        return false;
    }

    const int sign = (tz_offset[0] == '-') ? -1 : ((tz_offset[0] == '+') ? 1 : 0);
    if (sign == 0) {
        return false;
    }

    int hours = 0;
    int minutes = 0;

    // Accept both %z variants: +HHMM and +HH:MM.
    if (sscanf(tz_offset + 1, "%2d:%2d", &hours, &minutes) == 2) {
        // Parsed +HH:MM.
    } else if (sscanf(tz_offset + 1, "%2d%2d", &hours, &minutes) == 2) {
        // Parsed +HHMM.
    } else {
        return false;
    }

    *out_offset_min = static_cast<int16_t>(sign * (hours * 60 + minutes));
    return true;
}

static int16_t get_utc_offset_min_at(time_t epoch_utc) {
    if (epoch_utc <= 0) {
        return 0;
    }

    struct tm local_tm{};
    localtime_r(&epoch_utc, &local_tm);

    char tz_offset_buf[8] = {0};
    if (strftime(tz_offset_buf, sizeof(tz_offset_buf), "%z", &local_tm) == 0) {
        return 0;
    }

    int16_t offset_min = 0;
    if (!parse_strftime_utc_offset_min(tz_offset_buf, &offset_min)) {
        return 0;
    }

    return offset_min;
}

// Recompute and cache the UTC offset in minutes from the current TZ environment.
// Called whenever setenv("TZ",...) + tzset() are invoked so per-heartbeat cost is zero.
static void refresh_cached_utc_offset() {
    const time_t now = time(nullptr);
    cached_utc_offset_min = get_utc_offset_min_at(now);
}


static bool parse_utc_offset_hhmm(const char* utc_offset, int32_t* out_offset_seconds) {
    if (utc_offset == nullptr || out_offset_seconds == nullptr) {
        return false;
    }

    const int sign = (utc_offset[0] == '-') ? -1 : ((utc_offset[0] == '+') ? 1 : 0);
    if (sign == 0) {
        return false;
    }

    int hours = 0;
    int minutes = 0;
    if (sscanf(utc_offset + 1, "%d:%d", &hours, &minutes) < 1) {
        return false;
    }

    *out_offset_seconds = sign * ((hours * 3600) + (minutes * 60));
    return true;
}

static void sanitize_timezone_abbreviation(const char* input,
                                          char* output,
                                          size_t output_len) {
    if (output == nullptr || output_len == 0) {
        return;
    }

    output[0] = '\0';
    if (input == nullptr || input[0] == '\0') {
        strlcpy(output, "LOC", output_len);
        return;
    }

    size_t write_index = 0;
    for (size_t read_index = 0; input[read_index] != '\0' && write_index < (output_len - 1); ++read_index) {
        const char ch = input[read_index];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
            output[write_index++] = ch;
        }
    }
    output[write_index] = '\0';

    if (write_index < 3) {
        strlcpy(output, "LOC", output_len);
    }
}

static bool build_fixed_offset_posix_tz(int32_t utc_offset_seconds,
                                        const char* tz_abbrev_hint,
                                        char* posix_tz,
                                        size_t posix_tz_len,
                                        char* tz_abbrev,
                                        size_t tz_abbrev_len) {
    if (posix_tz == nullptr || posix_tz_len == 0 || tz_abbrev == nullptr || tz_abbrev_len == 0) {
        return false;
    }

    char safe_abbrev[16] = {0};
    sanitize_timezone_abbreviation(tz_abbrev_hint, safe_abbrev, sizeof(safe_abbrev));
    strlcpy(tz_abbrev, safe_abbrev, tz_abbrev_len);

    const int32_t abs_offset = (utc_offset_seconds < 0) ? -utc_offset_seconds : utc_offset_seconds;
    const int hours = static_cast<int>(abs_offset / 3600);
    const int minutes = static_cast<int>((abs_offset % 3600) / 60);
    const int posix_sign = (utc_offset_seconds >= 0) ? -1 : 1;

    const int written = (minutes == 0)
        ? snprintf(posix_tz, posix_tz_len, "%s%d", safe_abbrev, posix_sign * hours)
        : snprintf(posix_tz, posix_tz_len, "%s%d:%02d", safe_abbrev, posix_sign * hours, minutes);

    return written > 0 && static_cast<size_t>(written) < posix_tz_len;
}

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
    if (!doc["offset"].isNull()) {
        out.utc_offset_seconds = doc["offset"] | 0;
        out.utc_offset_valid = true;
    }

    return out.timezone_name[0] != '\0';
}

static bool parse_worldtime_response(JsonDocument& doc, TimezoneLookupResult& out) {
    strlcpy(out.timezone_name, doc["timezone"] | "", sizeof(out.timezone_name));
    strlcpy(out.timezone_abbreviation, doc["abbreviation"] | "", sizeof(out.timezone_abbreviation));
    strlcpy(out.public_ip, doc["client_ip"] | "", sizeof(out.public_ip));

    if (!doc["utc_offset"].isNull()) {
        int32_t parsed_offset = 0;
        if (parse_utc_offset_hhmm(doc["utc_offset"] | "", &parsed_offset)) {
            out.utc_offset_seconds = parsed_offset;
            out.utc_offset_valid = true;
        }
    }

    if (!out.utc_offset_valid && !doc["raw_offset"].isNull()) {
        const int32_t raw_offset = doc["raw_offset"] | 0;
        const int32_t dst_offset = doc["dst_offset"] | 0;
        out.utc_offset_seconds = raw_offset + dst_offset;
        out.utc_offset_valid = true;
    }

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
        {"ip-api.com", "/json/?fields=status,timezone,country,city,query,offset", parse_ip_api_response},
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

    bool timezone_changed = false;
    (void)configure_timezone_from_location_internal(&timezone_changed);
    last_timezone_attempt = millis();
    
    // Do initial NTP sync
    get_ntp_time();
    
    int timezone_retry_count = 0;
    
    while (true) {
        TickType_t current_time = xTaskGetTickCount();
        
        // Timezone auto-detection + periodic refresh.
        // Known timezone names use full POSIX DST rules; unknown names fall back to the provider's current UTC offset.
        if (is_network_connected()) {
            unsigned long time_since_last_attempt = millis() - last_timezone_attempt;
            const uint32_t required_interval = timezone_auto_detected ? kTimezoneRefreshIntervalMs
                                                                      : kTimezoneRetryDelayMs;
            if (time_since_last_attempt >= required_interval || last_timezone_attempt == 0) {
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
                
                bool timezone_rule_changed = false;
                if (configure_timezone_from_location_internal(&timezone_rule_changed)) {
                    LOG_INFO("NET_UTILS", "✓✓✓ SUCCESS! Timezone configured: %s (%s) ✓✓✓",
                             detected_timezone_name.c_str(),
                             detected_timezone_abbreviation.c_str());
                    log_routed(LogSink::Mqtt,
                               RoutedLevel::Notice,
                               "TZ",
                               "Configured: %s (%s)",
                               detected_timezone_name.c_str(),
                               detected_timezone_abbreviation.c_str());
                    if (timezone_rule_changed) {
                        last_ntp_sync = 0;
                        get_ntp_time();
                    }
                } else {
                    LOG_WARN("NET_UTILS", "Timezone detection attempt #%d FAILED - will retry in %d seconds",
                             timezone_retry_count, kTimezoneRetryDelayMs / 1000);
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
    // Configure a DST-aware default timezone on first call.
    // Geolocation may still replace this later, but we should not remain on raw UTC
    // in locales such as the UK where summer/winter transitions matter.
    if (!timezone_configured) {
        setenv("TZ", EthernetConfig::NTP::DEFAULT_POSIX_TZ, 1);
        tzset();
        detected_timezone_name = EthernetConfig::NTP::DEFAULT_TIMEZONE_NAME;
        refresh_detected_timezone_abbreviation_from_system_time();
        refresh_cached_utc_offset();
        timezone_configured = true;
        LOG_INFO("NTP_UTILS", "Initial timezone: %s -> %s (will auto-detect/override if available)",
                 EthernetConfig::NTP::DEFAULT_TIMEZONE_NAME,
                 EthernetConfig::NTP::DEFAULT_POSIX_TZ);
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
            refresh_detected_timezone_abbreviation_from_system_time();
            refresh_cached_utc_offset();
            
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

static bool configure_timezone_from_location_internal(bool* out_changed) {
    LOG_INFO("TZ_CONFIG", "Getting timezone from location with fallback...");

    if (out_changed != nullptr) {
        *out_changed = false;
    }

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

    bool used_offset_fallback = false;
    if (!known_mapping) {
        if (result.utc_offset_valid &&
            build_fixed_offset_posix_tz(result.utc_offset_seconds,
                                        result.timezone_abbreviation,
                                        posix_tz,
                                        sizeof(posix_tz),
                                        tz_abbrev,
                                        sizeof(tz_abbrev))) {
            used_offset_fallback = true;
            LOG_WARN("TZ_CONFIG", "Unknown timezone mapping: %s (using offset fallback %s)",
                     result.timezone_name,
                     posix_tz);
        } else {
            LOG_WARN("TZ_CONFIG", "Unknown timezone mapping: %s (fallback UTC0)", result.timezone_name);
        }
    }

    const char* current_tz = getenv("TZ");
    const bool tz_changed = (current_tz == nullptr) || (strcmp(current_tz, posix_tz) != 0);

    setenv("TZ", posix_tz, 1);
    tzset();

    detected_timezone_name = result.timezone_name;
    detected_timezone_abbreviation = tz_abbrev;
    refresh_detected_timezone_abbreviation_from_system_time();
    refresh_cached_utc_offset();
    public_ip_address = result.public_ip;
    last_public_ip_check = millis();
    timezone_configured = true;
    timezone_auto_detected = true;

    if (out_changed != nullptr) {
        *out_changed = tz_changed;
    }

    LOG_INFO("TZ_CONFIG", "Timezone configured: %s -> %s%s",
             result.timezone_name,
             posix_tz,
             used_offset_fallback ? " (offset fallback)" : "");
    log_routed(LogSink::Mqtt,
               RoutedLevel::Notice,
               "TZ",
               "Configured: %s (%s)",
               result.timezone_name,
               posix_tz);
    return true;
}

bool configure_timezone_from_location() {
    return configure_timezone_from_location_internal(nullptr);
}

bool get_formatted_time(char* buffer, size_t buffer_size) {
    if (!time_initialized) {
        snprintf(buffer, buffer_size, "Time not synced");
        return false;
    }
    
    time_t now = time(nullptr);
    struct tm local_tm{};
    localtime_r(&now, &local_tm);

    char tz_buf[16] = {0};
    if (strftime(tz_buf, sizeof(tz_buf), "%Z", &local_tm) == 0) {
        strlcpy(tz_buf, "UTC", sizeof(tz_buf));
    }

    snprintf(buffer, buffer_size, "%02d/%02d/%04d %02d:%02d:%02d %s",
             local_tm.tm_mday, local_tm.tm_mon + 1, local_tm.tm_year + 1900,
             local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec, tz_buf);
    return true;
}

bool is_geolocation_configured() {
    return timezone_auto_detected;
}

int16_t get_cached_utc_offset_min() {
    return cached_utc_offset_min;
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
