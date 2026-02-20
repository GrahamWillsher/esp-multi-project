#ifdef HW_LILYGO_T_CONNECT_PRO
#include "hw_lilygo_t_connect_pro.h"
#include "../../datalayer/datalayer.h"
#include "../utils/events.h"
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_mac.h"
#include <Ethernet.h>
#include <EthernetClient.h>
#include <EthernetUdp.h>
#include "../wifi/wifi.h"


#include <JPEGDecoder.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Color definitions for GFX library
#define BLACK    0x0000
#define WHITE    0xFFFF
#define RED      0xF800
#define GREEN    0x07E0
#define BLUE     0x001F
#define CYAN     0x07FF
#define MAGENTA  0xF81F
#define YELLOW   0xFFE0

// External version number declaration
extern const char* version_number;

// LCD hardware objects
// Arduino_DataBus *bus = nullptr;
// Arduino_GFX *gfx = nullptr;

// Global variables for timezone
String detectedTimezoneName = "";  // Store the detected timezone name for abbreviation lookup
String detectedTimezoneAbbreviation = "";  // Store the detected timezone abbreviation from API

// Display control flags (actual definitions with initialization)
bool displayTimeEnabled = true;           // Default: show time
bool displayEthernetStatusEnabled = true; // Default: show Ethernet status

// Global variables for ethernet status indicator task
TaskHandle_t ethernetStatusTaskHandle = NULL;
volatile bool ethernetStatusTaskRunning = false;

// Note: Using external Adafruit NeoPixel library instead of local copy
// to avoid linker conflicts

EthernetUDP Udp;
const char* ntpServer = NTP_SERVER1;        // Use define directly
const char* ntpServerBackup = NTP_SERVER2;  // Use define directly
const int ntpPort = 123;
const char* internetTestHost = "8.8.8.8";   // Google DNS for internet connectivity testing
const uint16_t internetTestPort = 53;        // DNS port for connectivity test
const int localPort = 2390;
byte packetBuffer[48];

// Create SPI bus for ST7796 display
Arduino_DataBus *bus = new Arduino_HWSPI(
    SCREEN_DC /* DC */, SCREEN_CS /* CS */, SCREEN_SCLK /* SCK */,
    SCREEN_MOSI /* MOSI */, SCREEN_MISO /* MISO */);

// Create ST7796 display driver
Arduino_GFX *gfx = new Arduino_ST7796(
    bus, SCREEN_RST /* RST */, ROTATION_LANDSCAPE_FLIPPED /* rotation */, true /* IPS */,
    SCREEN_WIDTH /* width */, SCREEN_HEIGHT /* height */,
    49 /* col offset 1 */, 0 /* row offset 1 */, 
    49 /* col_offset2 */, 0 /* row_offset2 */);


// W5500 Ethernet configuration
static bool ethernet_initialized = false;
static bool ethernet_connected = false;
static unsigned long ethernet_last_check = 0;

// Network configuration - uses same settings as WiFi but DIFFERENT MAC address
byte Ethernetmac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE};  // UNIQUE MAC address for Ethernet (different from WiFi)

// Function to generate unique MAC addresses for different interfaces
// void generate_unique_mac_addresses(void) {
//     uint8_t baseMac[6];
    
//     // Get the ESP32's built-in WiFi MAC address as base
//     esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    
//     Serial.print("ESP32 Base WiFi MAC: ");
//     for (int i = 0; i < 6; i++) {
//         if (i > 0) Serial.print(":");
//         if (baseMac[i] < 16) Serial.print("0");
//         Serial.print(baseMac[i], HEX);
//     }
//     Serial.println();
    
//     // Generate Ethernet MAC by modifying the last byte of WiFi MAC
//     // This ensures they're related but unique
//     for (int i = 0; i < 6; i++) {
//         Ethernetmac[i] = baseMac[i];
//     }
//     // Make Ethernet MAC unique by adding 1 to the last byte
//     // Handle overflow to avoid 0xFF -> 0x00 transition
//     if (Ethernetmac[5] == 0xFF) {
//         Ethernetmac[5] = 0xFE;  // Avoid 0xFF -> 0x00 rollover
//     } else {
//         Ethernetmac[5] += 1;    // Simple increment
//     }
    
//     Serial.print("Generated Ethernet MAC: ");
//     for (int i = 0; i < 6; i++) {
//         if (i > 0) Serial.print(":");
//         if (Ethernetmac[i] < 16) Serial.print("0");
//         Serial.print(Ethernetmac[i], HEX);
//     }
//     Serial.println();
    
//     // Verify they're different
//     bool identical = true;
//     for (int i = 0; i < 6; i++) {
//         if (baseMac[i] != Ethernetmac[i]) {
//             identical = false;
//             break;
//         }
//     }
    
//     if (identical) {
//         Serial.println("WARNING: WiFi and Ethernet MAC addresses are identical!");
//         Serial.println("This may cause network conflicts. Forcing difference...");
//         Ethernetmac[5] = (Ethernetmac[5] + 1) & 0xFF;
//         if (Ethernetmac[5] == baseMac[5]) {
//             Ethernetmac[4] = (Ethernetmac[4] + 1) & 0xFF;
//         }
//     } else {
//         Serial.println("✓ MAC addresses are unique - no conflicts expected");
//     }
// }

// CPU usage monitoring variables
static unsigned long lastIdleTime = 0;
static unsigned long lastTotalTime = 0;
static float cpuUsagePercent = 0.0;

// LCD display constants - used across multiple functions
const int text_base_width = 6;     // Base character width in pixels
const int text_base_height = 8;    // Base character height in pixels  
const int TextSize_2 = 2;            // Text size multiplier
const int LCD_MARGIN = 10;         // Left margin for labels
const char* splashFile = "/BatteryEmulator4.jpg";  // Splash screen image file

// GFX text size enumeration
enum GfxTextSize {
    TEXT_SIZE_1 = 1,
    TEXT_SIZE_2,
    TEXT_SIZE_3,
    TEXT_SIZE_4,
    TEXT_SIZE_5
};

// Helper function to calculate text width for centering
int16_t calculateTextWidth(const String& text, GfxTextSize size) {
    return text.length() * text_base_width * static_cast<int>(size);
}

// **FULLY AUTOMATED** - Labels auto-generated from header macro list!
// To add/remove/change labels, ONLY modify LCD_LABEL_LIST in the .h file

// Auto-generate label definitions with sizeof() character counting
static int label_counter = 0;
#define LABEL_ENTRY(name, text, x, y) \
    const LCDLabel LCD_##name = {text, sizeof(text) - 1, x, y, label_counter++};
LCD_LABEL_LIST
#undef LABEL_ENTRY

// Auto-generate array of all labels
#define LABEL_ENTRY(name, text, x, y) &LCD_##name,
const LCDLabel* const LCD_LABELS[] = {
    LCD_LABEL_LIST
};
#undef LABEL_ENTRY

// Auto-calculate array count
const int LCD_LABELS_COUNT = sizeof(LCD_LABELS) / sizeof(LCD_LABELS[0]);


void setupLCDDisplay() {

    pinMode(SCREEN_CS, OUTPUT);
    digitalWrite(SCREEN_CS, HIGH);

    // Configure PWM for backlight control using channel-specific ESP32 core API
    ledcAttachChannel(SCREEN_BL, 2000, 8, 1);  // pin, freq, resolution, channel
    ledcWriteChannel(1, 0);  // Start with backlight OFF to prevent white flash


    gfx->begin();
    gfx->fillScreen(BLACK);  // Immediately set screen to black to prevent white flash

}

void setupEthernetAdapter() {

    pinMode(W5500_CS_PIN, OUTPUT);
    digitalWrite(W5500_CS_PIN, HIGH);

}



// Function to get CPU temperature in Celsius
float get_cpu_temperature(void) {
    return temperatureRead();  // Built-in ESP32 function returns temperature in Celsius
}

// Function to calculate CPU usage using FreeRTOS task stats
float Calculate_cpu_usage(void) {
    static unsigned long lastMeasurementTime = 0;
    unsigned long currentTime = millis();
    
    // Only calculate every 1000ms to avoid overhead
    if (currentTime - lastMeasurementTime < 1000) {
        return cpuUsagePercent;
    }
    lastMeasurementTime = currentTime;
    
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    // Method 1: Using FreeRTOS runtime stats (most accurate)
    TaskStatus_t *taskStatusArray;
    volatile UBaseType_t taskCount;
    unsigned long totalRunTime, idleRunTime = 0;
    
    taskCount = uxTaskGetNumberOfTasks();
    taskStatusArray = (TaskStatus_t*)pvPortMalloc(taskCount * sizeof(TaskStatus_t));
    
    if (taskStatusArray != NULL) {
        taskCount = uxTaskGetSystemState(taskStatusArray, taskCount, &totalRunTime);
        
        // Find IDLE task runtime
        for (UBaseType_t i = 0; i < taskCount; i++) {
            if (strstr(taskStatusArray[i].pcTaskName, "IDLE") != NULL) {
                idleRunTime += taskStatusArray[i].ulRunTimeCounter;
            }
        }
        
        // Calculate CPU usage percentage
        if (totalRunTime > lastTotalTime) {
            unsigned long totalDelta = totalRunTime - lastTotalTime;
            unsigned long idleDelta = idleRunTime - lastIdleTime;
            cpuUsagePercent = 100.0 - ((float)idleDelta / (float)totalDelta * 100.0);
        }
        
        lastTotalTime = totalRunTime;
        lastIdleTime = idleRunTime;
        
        vPortFree(taskStatusArray);
    }
#else
    // Method 2: Simple approximation using heap and stack info
    size_t freeHeap = esp_get_free_heap_size();
    size_t totalHeap = esp_get_minimum_free_heap_size();
    
    // Rough estimation based on memory pressure and task switching
    static size_t lastFreeHeap = freeHeap;
    float memoryPressure = (float)(lastFreeHeap - freeHeap) / 1024.0; // KB/sec
    cpuUsagePercent = constrain(memoryPressure * 0.1 + random(5, 25), 0, 100);
    lastFreeHeap = freeHeap;
#endif
    
    return cpuUsagePercent;
}

// Validate static IP configuration from webserver settings
bool validate_static_ip_config(void) {
    if (!static_IP_enabled) {
        return true;  // DHCP mode, no validation needed
    }
    
    // Check if all IP components are valid (0-255)
    if (static_local_IP1 > 255 || static_local_IP2 > 255 || static_local_IP3 > 255 || static_local_IP4 > 255) {
        Serial.println("ERROR: Invalid static IP address components");
        return false;
    }
    
    if (static_gateway1 > 255 || static_gateway2 > 255 || static_gateway3 > 255 || static_gateway4 > 255) {
        Serial.println("ERROR: Invalid gateway address components");
        return false;
    }
    
    if (static_subnet1 > 255 || static_subnet2 > 255 || static_subnet3 > 255 || static_subnet4 > 255) {
        Serial.println("ERROR: Invalid subnet mask components");
        return false;
    }
    
    // Check for obviously invalid IP addresses
    if (static_local_IP1 == 0 && static_local_IP2 == 0 && static_local_IP3 == 0 && static_local_IP4 == 0) {
        Serial.println("ERROR: Static IP cannot be 0.0.0.0");
        return false;
    }
    
    return true;
}

// New function: Initialize Ethernet in main setup (blocking, one-time)
// void init_ethernet_if_available(void) {
//   Serial.println(">>> DEBUG: init_ethernet_if_available() STARTED <<<");
//   Serial.flush();
  
//   comm_interface interface = network_interface();
//   Serial.printf(">>> DEBUG: network_interface() returned = %d (0=None, 8=Ethernet) <<<\n", (int)interface);
//   Serial.flush();
  
//   if (interface == comm_interface::Ethernet) {
//     Serial.println(">>> DEBUG: Ethernet detected - calling initEthernet() <<<");
//     Serial.flush();
//     // Hardware supports Ethernet (already verified by network_interface())
//     initEthernet();
//     delay(2000); // Wait for link establishment
//     Serial.println(">>> DEBUG: initEthernet() completed <<<");
//   } else {
//     Serial.printf(">>> DEBUG: Ethernet NOT detected - interface=%d <<<\n", (int)interface);
//     Serial.println(">>> DEBUG: Check: 1) W5500 hardware, 2) SPI connections, 3) available_interfaces() HAL method <<<");
//   }
//   Serial.flush();
  
//   Serial.println(">>> DEBUG: init_ethernet_if_available() FINISHED <<<");
//   Serial.flush();
// }



// Initialize Ethernet hardware - Simple approach using Ethernet.begin()
bool initEthernet(void) {
    static bool hardware_initialized = false;
    
    Serial.println("=== initEthernet() START ===");
    Serial.flush();

    
//SPI.begin(W5500_SCLK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN, W5500_CS_PIN);


    pinMode(W5500_CS_PIN, OUTPUT);
    digitalWrite(W5500_CS_PIN, HIGH);


    // One-time hardware initialization
    if (!hardware_initialized) {
        Serial.println("DEBUG: First-time hardware initialization");
        Serial.flush();
        
        // Generate unique MAC addresses
        Serial.println("DEBUG: Generating unique MAC addresses...");
        //generate_unique_mac_addresses();
        Serial.println("DEBUG: MAC address generation completed");
        Serial.flush();
        
        // Optional: Hardware reset using RST pin (for reliability)
        // if (W5500_RST_PIN != -1) {
        //     Serial.printf("DEBUG: Performing W5500 hardware reset using pin %d\n", W5500_RST_PIN);
        //     pinMode(W5500_RST_PIN, OUTPUT);
        //     digitalWrite(W5500_RST_PIN, LOW);   // Assert reset
        //     delay(50);                          // Hold reset for 50ms
        //     digitalWrite(W5500_RST_PIN, HIGH);  // Release reset
        //     delay(200);                         // Wait for W5500 to fully initialize
        //     Serial.println("DEBUG: W5500 hardware reset completed");
        // } else {
        //     Serial.println("DEBUG: W5500_RST_PIN not defined, skipping hardware reset");
        // }
        // Serial.flush();
        
        // Tell Ethernet library which CS pin to use (REQUIRED)
        Serial.printf("DEBUG: Initializing Ethernet library with CS pin %d\n", W5500_CS_PIN);
        Ethernet.init(W5500_CS_PIN);
        Serial.println("DEBUG: Ethernet.init() completed");
        
        hardware_initialized = true;
        Serial.flush();
    } else {
        Serial.println("DEBUG: Hardware already initialized, skipping initialization");
        Serial.flush();
    }
    
    // Configure network settings and start Ethernet
    Serial.println("DEBUG: Starting Ethernet configuration...");
    bool ethernet_started = false;
    
    if (static_IP_enabled) {
        Serial.println("DEBUG: Using static IP configuration");
        
        // Build IP addresses from webserver settings with Ethernet IP offset by +1
        
        Serial.printf("DEBUG: Static IP: %d.%d.%d.%d\n", static_local_IP1, static_local_IP2, static_local_IP3, static_local_IP4 + 1);
        Serial.printf("DEBUG: Gateway: %d.%d.%d.%d\n", static_gateway1, static_gateway2, static_gateway3, static_gateway4);
        Serial.printf("DEBUG: Subnet: %d.%d.%d.%d\n", static_subnet1, static_subnet2, static_subnet3, static_subnet4);
        Serial.flush();
        
        // Ethernet.begin() with static IP - handles all SPI setup internally
        // Use the actual static IP configuration variables, not hardcoded values
        IPAddress static_ip(static_local_IP1, static_local_IP2, static_local_IP3, static_local_IP4 + 1);  // WiFi IP + 1
        IPAddress dns_server(static_gateway1, static_gateway2, static_gateway3, static_gateway4);      // Use gateway as DNS
        IPAddress gateway(static_gateway1, static_gateway2, static_gateway3, static_gateway4);
        IPAddress subnet(static_subnet1, static_subnet2, static_subnet3, static_subnet4);
        
        Serial.printf("DEBUG: Configuring Ethernet with - IP: %s, Gateway: %s, Subnet: %s\n",
                     static_ip.toString().c_str(), gateway.toString().c_str(), subnet.toString().c_str());
        Serial.flush();
        
        // CRITICAL: Ensure 5V power is enabled for W5500 before reset
        // Serial.println("DEBUG: Ensuring 5V power is enabled for W5500...");
        // pinMode(10, OUTPUT);  // PIN_5V_EN = GPIO 10
        // digitalWrite(10, HIGH);  // Enable 5V power
        delay(100);  // Wait for power to stabilize
        Serial.println("DEBUG: 5V power enabled");
        Serial.flush();
        
        // CRITICAL: Force W5500 hardware reset before configuration to clear any corrupted state
        Serial.println("DEBUG: Performing W5500 hardware reset to ensure clean state...");
        pinMode(W5500_RST_PIN, OUTPUT);
        digitalWrite(W5500_RST_PIN, LOW);   // Assert reset
        delay(200);                         // Hold reset for 200ms (longer for reliability)
        digitalWrite(W5500_RST_PIN, HIGH);  // Release reset
        delay(500);                         // Wait longer for W5500 to complete reset sequence
        Serial.println("DEBUG: W5500 hardware reset completed");
        Serial.flush();
        
        // Additional SPI diagnostic
        Serial.println("DEBUG: SPI pin configuration check...");
        Serial.printf("DEBUG: W5500 pins - CS:%d, RST:%d, SCLK:%d, MISO:%d, MOSI:%d\n", 
                     W5500_CS_PIN, W5500_RST_PIN, W5500_SCLK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN);
        Serial.flush();
        

        Serial.println("DEBUG: Calling Ethernet.begin() with static IP configuration...");
        Ethernet.begin(Ethernetmac, static_ip, dns_server, gateway, subnet);
        Serial.println("DEBUG: Ethernet.begin() call completed");
        ethernet_started = true;
    } else {
        Serial.println("DEBUG: Attempting DHCP configuration");
        Serial.flush();
        
        // Ethernet.begin() with DHCP - handles all SPI setup internally  
        int dhcp_result = Ethernet.begin(Ethernetmac);
        Serial.printf("DEBUG: Ethernet.begin() DHCP result = %d (0=failed, 1=success)\n", dhcp_result);
        Serial.flush();
        
        if (dhcp_result == 0) {
            Serial.println("ERROR: DHCP configuration failed!");
            Serial.flush();
            ethernet_connected = false;
            return false;
        }
        ethernet_started = true;
    }
    Serial.flush();
    
    if (!ethernet_started) {
        Serial.println("ERROR: Failed to start Ethernet configuration");
        ethernet_connected = false;
        return false;
    }
    
    // Wait for Ethernet configuration to complete with proper timing
    Serial.println("DEBUG: Waiting for Ethernet initialization to complete...");
    Serial.flush();
    

    Serial.println("DEBUG: Ethernet initialization delay completed");
    Serial.flush();
    
    // Final IP address verification with multiple attempts
    Serial.println("DEBUG: Performing final IP address verification...");
    IPAddress localIP;
    bool ip_verified = false;
    
    for (int verify_attempt = 1; verify_attempt <= 3; verify_attempt++) {
        localIP = Ethernet.localIP();
        Serial.printf("DEBUG: IP verification attempt %d/3 - IP = %d.%d.%d.%d\n", 
                     verify_attempt, localIP[0], localIP[1], localIP[2], localIP[3]);
        Serial.flush();
        
        if (localIP != IPAddress(0, 0, 0, 0)) {
            Serial.printf("DEBUG: IP verification successful on attempt %d\n", verify_attempt);
            ip_verified = true;
            break;
        }
        
        if (verify_attempt < 3) {
            Serial.println("DEBUG: IP still showing 0.0.0.0, waiting 500ms and retrying...");
            delay(500); // Short delay between verification attempts
            //esp_task_wdt_reset();
        }
    }
    
    if (!ip_verified) {
        Serial.println("ERROR: IP verification failed - still showing 0.0.0.0 after multiple attempts");
        if (static_IP_enabled) {
            Serial.println("Static IP configuration appears to have failed");
            Serial.println("Check: 1) W5500 SPI communication, 2) Network settings, 3) Hardware connections");
        } else {
            Serial.println("DHCP appears to have failed despite initial success indication");
            Serial.println("Check: 1) DHCP server logs, 2) Network connectivity, 3) MAC address conflicts");
        }
        Serial.flush();
        ethernet_connected = false;
        return false;
    }
    
    Serial.println("DEBUG: IP address assignment successful");
    Serial.flush();
    
    // Check link status with progressive timing - W5500 needs time for link establishment
    Serial.println("DEBUG: Waiting for physical link establishment...");
    Serial.flush();
    
    // Progressive link status checking - allow up to 10 seconds for link to establish
    EthernetLinkStatus link_status = EthernetLinkStatus::Unknown;
    bool link_established = false;
    
    for (int link_attempt = 1; link_attempt <= 10; link_attempt++) {
        delay(1000); // Wait 1 second between checks
        //esp_task_wdt_reset();
        
        link_status = Ethernet.linkStatus();
        Serial.printf("DEBUG: Link check attempt %d/10 - Link status = %d (0=Unknown, 1=LinkON, 2=LinkOFF)\n", 
                     link_attempt, link_status);
        Serial.flush();
        
        if (link_status == LinkON) {
            Serial.printf("DEBUG: Physical link established after %d seconds!\n", link_attempt);
            link_established = true;
            break;
        } else if (link_status == LinkOFF) {
            if (link_attempt <= 5) {
                Serial.printf("DEBUG: Link still down, waiting... (%d/10 seconds)\n", link_attempt);
            } else {
                Serial.printf("WARNING: Link still down after %d seconds - possible cable issue\n", link_attempt);
            }
        } else {
            Serial.printf("DEBUG: Link status unknown, continuing to wait... (%d/10 seconds)\n", link_attempt);
        }
        Serial.flush();
    }
    
    // Final link status evaluation
    if (link_established) {
        Serial.println("SUCCESS: Physical link confirmed - Ethernet connection fully established");
    } else if (link_status == LinkOFF) {
        Serial.println("WARNING: Physical link shows LinkOFF but we have valid IP");
        Serial.println("This may indicate: 1) Loose cable, 2) Switch/router issues, 3) Timing problems");
        Serial.println("Proceeding as IP configuration succeeded...");
    } else {
        Serial.println("WARNING: Link status remains Unknown but we have valid IP");
        Serial.println("This may indicate: 1) W5500 communication issues, 2) Library limitations");
        Serial.println("Proceeding as IP configuration succeeded...");
    }
    Serial.flush();
    
    // Additional Static IP Validation Tests
  
    // Success
    Serial.println("SUCCESS: Ethernet initialization completed successfully!");
    Serial.printf("Final IP: %s\n", localIP.toString().c_str());
    Serial.printf("Gateway: %s\n", Ethernet.gatewayIP().toString().c_str());
    Serial.printf("Subnet: %s\n", Ethernet.subnetMask().toString().c_str());
    Serial.printf("DNS: %s\n", Ethernet.dnsServerIP().toString().c_str());
    Serial.flush();
    
    ethernet_connected = true;
    ethernet_initialized = true;
    
    // Call comprehensive static IP testing after successful initialization
    if (static_IP_enabled && ethernet_connected) {
        Serial.println("DEBUG: Waiting for W5500 registers to stabilize before testing...");
        Serial.flush();
        
        // W5500 needs additional time for internal registers to fully update
        // This is especially important for IP configuration registers
        delay(3000); // 3 second delay for register stabilization
        //esp_task_wdt_reset();
        
        Serial.println("DEBUG: W5500 register stabilization complete - running comprehensive tests...");
        Serial.flush();
        
        test_static_ip_connectivity();
    }
    
    Serial.println("=== initEthernet() SUCCESS ===");
    Serial.flush();
    return true;
}



// bool initEthernet() {
//     static unsigned long lastResetAttempt = 0;
//     static unsigned long lastFullCheck = 0;
//     static bool lastConnectionStatus = false;
//     const unsigned long RESET_INTERVAL = 5000; // 5 seconds
//     const unsigned long FULL_CHECK_INTERVAL = 60000; // 1 minute
    
//     unsigned long currentTime = millis();
    
//     // Only do full check every minute, otherwise return cached status
//     if (currentTime - lastFullCheck < FULL_CHECK_INTERVAL && lastFullCheck != 0) {
//         return lastConnectionStatus;
//     }
    
//     lastFullCheck = currentTime;
//     Serial.println("Performing full Ethernet connection check...");
    
//     // Check if Ethernet hardware is initialized
//     if (Ethernet.hardwareStatus() == EthernetNoHardware) {
//         Serial.println("Ethernet shield was not found.");
//         lastConnectionStatus = false;
//         return false;
//     }
    
//     // Check link status
//     if (Ethernet.linkStatus() == LinkOFF) {
//         Serial.println("Ethernet cable is not connected. Will check again in 1 minute...");
//         lastConnectionStatus = false;
//         return false;
//     }
    
//     // Check if we have a valid IP address
//     IPAddress localIP = Ethernet.localIP();
//     if (localIP == IPAddress(0, 0, 0, 0)) {
//         Serial.println("No IP address assigned - cable connected but no IP.");
        
//         // Attempt to reset Ethernet connection every 5 seconds (only during full check)
//         if (currentTime - lastResetAttempt >= RESET_INTERVAL) {
//             lastResetAttempt = currentTime;
//             Serial.println("Attempting to reset Ethernet connection...");
            
//             // Network configuration from Battery_Emulator.ino
//             uint8_t mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
//             IPAddress ip(192, 168, 1, 25);
//             IPAddress dns(192, 168, 1, 1);
//             IPAddress gateway(192, 168, 1, 1);
//             IPAddress subnet(255, 255, 255, 0);
            
//             // Attempt to reinitialize Ethernet with static IP
//             Ethernet.begin(mac, ip, dns, gateway, subnet);
//             delay(100); // Give it a moment to initialize
            
//             // Check if the reset was successful by verifying IP assignment
//             IPAddress newIP = Ethernet.localIP();
//             if (newIP != IPAddress(0, 0, 0, 0)) {
//                 Serial.printf("Ethernet reset successful. New IP: %s\n", newIP.toString().c_str());
//                 lastConnectionStatus = true;
//                 return true;
//             } else {
//                 Serial.println("Ethernet reset failed - will retry next full check.");
//                 lastConnectionStatus = false;
//                 return false;
//             }
//         }
//         lastConnectionStatus = false;
//         return false;
//     }
    
//     Serial.printf("Ethernet connection OK. IP: %s\n", localIP.toString().c_str());
//     lastConnectionStatus = true;
//     return true;
// }

// Initialize W5500 Ethernet adapter
// bool init_w5500_ethernet(void) {

//     static unsigned long lastResetAttempt = 0;
//     static unsigned long lastFullCheck = 0;
//     static bool lastConnectionStatus = false;
//     static bool hardware_initialized = false;
//     const unsigned long RESET_INTERVAL = 5000; // 5 seconds
//     const unsigned long FULL_CHECK_INTERVAL = 60000; // 1 minute
    
//     unsigned long currentTime = millis();
    
//     // Initialize hardware on first run
//     if (!hardware_initialized) {
//         Serial.println("Initializing W5500 hardware for first time...");
        
//         // Generate unique MAC addresses first
//         generate_unique_mac_addresses();
        
//         // Initialize SPI with W5500-specific pins
//         Serial.printf("Initializing SPI with pins: SCLK=%d, MISO=%d, MOSI=%d, CS=%d\n", 
//                      W5500_SCLK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN, W5500_CS_PIN);
//         SPI.begin(W5500_SCLK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN, W5500_CS_PIN);
//         Serial.println("SPI.begin() completed");
        
//         // Configure CS pin
//         pinMode(W5500_CS_PIN, OUTPUT);
//         digitalWrite(W5500_CS_PIN, HIGH); // Ensure CS is high (inactive)
//         Serial.printf("CS pin %d configured and set HIGH\n", W5500_CS_PIN);
        
//         // Hardware reset using RST pin
//         Serial.printf("Performing W5500 hardware reset using pin %d\n", W5500_RST_PIN);
//         pinMode(W5500_RST_PIN, OUTPUT);
//         digitalWrite(W5500_RST_PIN, LOW);   // Assert reset
//         delay(50);                          // Hold reset for 50ms
//         digitalWrite(W5500_RST_PIN, HIGH);  // Release reset
//         delay(200);                         // Wait for W5500 to fully initialize
//         Serial.println("W5500 hardware reset completed");
        
//         // Initialize Ethernet library with CS pin
//         Ethernet.init(W5500_CS_PIN);
//         Serial.printf("Ethernet.init() called with CS pin %d\n", W5500_CS_PIN);
        
//         // Use proper network configuration from the start
//         Serial.println("Configuring W5500 with network settings...");
//         if (static_IP_enabled) {
//             Serial.println("Using static IP configuration from webserver settings...");
            
//             // Build IP addresses from webserver settings with Ethernet IP offset by +1
//             IPAddress static_ip(static_local_IP1, static_local_IP2, static_local_IP3, static_local_IP4 + 1);  // WiFi IP + 1
//             IPAddress gateway(static_gateway1, static_gateway2, static_gateway3, static_gateway4);
//             IPAddress subnet(static_subnet1, static_subnet2, static_subnet3, static_subnet4);
//             IPAddress dns_server(8, 8, 8, 8);  // Default DNS, could be made configurable
            
//             Serial.printf("Ethernet Static IP: %d.%d.%d.%d (WiFi IP + 1)\n", static_local_IP1, static_local_IP2, static_local_IP3, static_local_IP4 + 1);
//             Serial.printf("Gateway: %d.%d.%d.%d\n", static_gateway1, static_gateway2, static_gateway3, static_gateway4);
//             Serial.printf("Subnet: %d.%d.%d.%d\n", static_subnet1, static_subnet2, static_subnet3, static_subnet4);
//             Serial.flush(); // Ensure all configuration messages are transmitted
            
//             // Configure with static IP
//             Ethernet.begin(Ethernetmac, static_ip, dns_server, gateway, subnet);
            
//             // DIAGNOSTIC: Check IP immediately after Ethernet.begin() (static IP)
//             // IPAddress immediate_ip_static = Ethernet.localIP();
//             // Serial.printf("*** STATIC IP DIAGNOSTIC - IP immediately after Ethernet.begin(): %d.%d.%d.%d ***\n", 
//             //              immediate_ip_static[0], immediate_ip_static[1], immediate_ip_static[2], immediate_ip_static[3]);
//             // if (immediate_ip_static[0] == immediate_ip_static[1] && immediate_ip_static[1] == immediate_ip_static[2] && immediate_ip_static[2] == immediate_ip_static[3] && immediate_ip_static[0] != 0) {
//             //     Serial.printf("*** SUSPICIOUS PATTERN DETECTED RIGHT AFTER STATIC CONFIG! ***\n");
//             // }
//             // Serial.flush();
            
//             // Serial.println("W5500 configured with static IP");
//             // Serial.flush(); // Ensure message is transmitted
//         } else {
//             Serial.println("Attempting DHCP configuration...");
//             Serial.flush(); // Ensure message is transmitted
            
//             if (Ethernet.begin(Ethernetmac) == 0) {
//                 Serial.println("DHCP failed on initial attempt");
//                 Serial.flush();
//             } else {
//                 // DIAGNOSTIC: Check IP immediately after Ethernet.begin() (DHCP)
//                 IPAddress immediate_ip_dhcp = Ethernet.localIP();
//                 Serial.printf("*** DHCP DIAGNOSTIC - IP immediately after Ethernet.begin(): %d.%d.%d.%d ***\n", 
//                              immediate_ip_dhcp[0], immediate_ip_dhcp[1], immediate_ip_dhcp[2], immediate_ip_dhcp[3]);
//                 if (immediate_ip_dhcp[0] == immediate_ip_dhcp[1] && immediate_ip_dhcp[1] == immediate_ip_dhcp[2] && immediate_ip_dhcp[2] == immediate_ip_dhcp[3] && immediate_ip_dhcp[0] != 0) {
//                     Serial.printf("*** SUSPICIOUS PATTERN DETECTED RIGHT AFTER DHCP CONFIG! ***\n");
//                 }
//                 Serial.flush();
                
//                 Serial.println("DHCP configuration successful");
//                 Serial.flush();
//             }
//         }
        
//         delay(1000); // Give hardware time to initialize
//         esp_task_wdt_reset(); // Reset watchdog after initialization delay
//         hardware_initialized = true;
//         Serial.println("Hardware initialization complete");
//     }
    
//     // Only do full check every minute, otherwise return cached status
//     if (currentTime - lastFullCheck < FULL_CHECK_INTERVAL && lastFullCheck != 0) {
//         return lastConnectionStatus;
//     }
    
//     // lastFullCheck = currentTime;
//     // Serial.println("Performing full Ethernet connection check...");
    
//     // // Test basic SPI communication by trying to get link status first
//     // Serial.println("Testing basic Ethernet communication...");
//     // EthernetLinkStatus link_status = Ethernet.linkStatus();
//     // Serial.printf("Link status: %d (0=Unknown, 1=LinkON, 2=LinkOFF)\n", link_status);
    
//     // // Try to get IP address (this uses different registers than hardwareStatus)
//     // IPAddress local_ip = Ethernet.localIP();
//     // Serial.printf("Local IP: %d.%d.%d.%d\n", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
    
//     // // NOW check if Ethernet hardware was detected (this often fails even when everything else works)
//     // EthernetHardwareStatus hw_status = Ethernet.hardwareStatus();
//     // Serial.printf("Hardware status: %d (0=None, 1=W5100, 2=W5200, 3=W5500)\n", hw_status);
    
//     // if (hw_status == EthernetNoHardware) {
//     //     Serial.println("WARNING: hardwareStatus() returned 'NoHardware' but this is often incorrect for W5500");
//     //     Serial.println("This is a known issue with Arduino Ethernet library - checking alternative methods...");
        
//     //     // Alternative check: if we have a valid IP or link status, hardware is probably working
//     //     if (local_ip != IPAddress(0, 0, 0, 0) || link_status != EthernetLinkStatus::Unknown) {
//     //         Serial.println("SUCCESS: Alternative hardware detection shows W5500 is working!");
//     //         Serial.println("Ignoring hardwareStatus() and proceeding with connection test...");
//     //         // Don't return false here - continue with connection testing
//     //     } else {
//     //         Serial.println("ERROR: Both hardwareStatus() and alternative detection failed");
//     //         Serial.println("Check: 1) SPI wiring, 2) CS pin configuration, 3) Power supply");
//     //         lastConnectionStatus = false;
//     //         return false;
//     //     }
//     // } else {
//     //     Serial.printf("SUCCESS: Hardware status correctly detected W5500 (status=%d)\n", hw_status);
//     // }
    
//     // Serial.println("✓ W5500 hardware detected successfully!");
    
//     // // Check link status
//     // if (Ethernet.linkStatus() == LinkOFF) {
//     //     Serial.println("Ethernet cable is not connected. Will check again in 1 minute...");
//     //     lastConnectionStatus = false;
//     //     return false;
//     // }
    
//     // // Check if we have a valid IP address
//     // IPAddress localIP = Ethernet.localIP();
    
//     // // // DIAGNOSTIC: Check for suspicious IP patterns that might indicate memory corruption
//     // if (localIP[0] == localIP[1] && localIP[1] == localIP[2] && localIP[2] == localIP[3] && localIP[0] != 0) {
//     //     Serial.printf("*** SUSPICIOUS IP PATTERN DETECTED IN init_w5500_ethernet() ***\n");
//     //     Serial.printf("IP: %d.%d.%d.%d - All octets identical!\n", localIP[0], localIP[1], localIP[2], localIP[3]);
//     //     Serial.printf("This may indicate memory corruption or library bug.\n");
//     //     Serial.printf("Hex: %02X.%02X.%02X.%02X\n", localIP[0], localIP[1], localIP[2], localIP[3]);
//     //     Serial.printf("Binary: %08b.%08b.%08b.%08b\n", localIP[0], localIP[1], localIP[2], localIP[3]);
//     //     Serial.println("***********************************************************\n");
//     //     Serial.flush();
//     // }
    
//     // if (localIP == IPAddress(0, 0, 0, 0)) {
//     //     Serial.println("No IP address assigned - cable connected but no IP.");
        
//     //     // DISABLED: Ethernet reset logic that causes IP changes
//     //     // This aggressive reset was causing the IP address to change repeatedly
//     //     Serial.println("NOTICE: Ethernet reset disabled to prevent IP address changes");
//     //     Serial.println("If network is not working, please restart the device");
//     //     ethernet_connected = false;
//     //     lastConnectionStatus = false;
//     //     return false;
        
//     //     /*
//     //     // OLD RESET LOGIC - COMMENTED OUT TO PREVENT IP CHANGES
//     //     // Attempt to reset Ethernet connection every 5 seconds (only during full check)
//     //     if (currentTime - lastResetAttempt >= RESET_INTERVAL) {
//     //         lastResetAttempt = currentTime;
//     //         Serial.println("Attempting to reset Ethernet connection...");
//     //         esp_task_wdt_reset(); // Reset watchdog before potentially long network operations


//     //         if (static_IP_enabled) {
//     //                 Serial.println("Using static IP configuration from webserver settings...");
                    
//     //                 // Build IP addresses from webserver settings with Ethernet IP offset by +1
//     //                 IPAddress static_ip(static_local_IP1, static_local_IP2, static_local_IP3, static_local_IP4 + 1);  // WiFi IP + 1
//     //                 IPAddress gateway(static_gateway1, static_gateway2, static_gateway3, static_gateway4);
//     //                 IPAddress subnet(static_subnet1, static_subnet2, static_subnet3, static_subnet4);
//     //                 IPAddress dns_server(8, 8, 8, 8);  // Default DNS, could be made configurable
                    
//     //                 Serial.printf("Ethernet Static IP: %d.%d.%d.%d (WiFi IP + 1)\n", static_local_IP1, static_local_IP2, static_local_IP3, static_local_IP4 + 1);
//     //                 Serial.printf("Gateway: %d.%d.%d.%d\n", static_gateway1, static_gateway2, static_gateway3, static_gateway4);
//     //                 Serial.printf("Subnet: %d.%d.%d.%d\n", static_subnet1, static_subnet2, static_subnet3, static_subnet4);
//     //                 Serial.flush(); // Ensure all configuration messages are transmitted
                    
//     //                 // Configure with static IP
//     //                 Ethernet.begin(Ethernetmac, static_ip, dns_server, gateway, subnet);
//     //                 esp_task_wdt_reset(); // Reset watchdog after Ethernet.begin() which can take time
//     //                 delay(100); // Give it a moment to initialize
//     //                 Serial.println("W5500 configured with static IP");
//     //                 Serial.flush(); // Ensure message is transmitted
//     //             } else {
//     //                 Serial.println("Attempting DHCP configuration...");
//     //                 Serial.flush(); // Ensure message is transmitted
                    
//     //                 if (Ethernet.begin(Ethernetmac) == 0) {
//     //                     Serial.println("DHCP failed and no static IP configured in webserver!");
//     //                     Serial.println("Please configure static IP in webserver settings or ensure DHCP is available.");
//     //                     Serial.flush(); // Ensure error messages are transmitted
//     //                     return false;
//     //                 }
//     //                 esp_task_wdt_reset(); // Reset watchdog after DHCP which can take significant time
//     //                 Serial.println("DHCP configuration successful");
//     //                 Serial.flush(); // Ensure message is transmitted
//     //             }

            
//     //         // Network configuration from Battery_Emulator.ino
//     //         // uint8_t mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
//     //         // IPAddress ip(192, 168, 1, 25);
//     //         // IPAddress dns(192, 168, 1, 1);
//     //         // IPAddress gateway(192, 168, 1, 1);
//     //         // IPAddress subnet(255, 255, 255, 0);
            
//     //         // Attempt to reinitialize Ethernet with static IP
//     //         //Ethernet.begin(Ethernetmac, static_ip, dns_server, gateway, subnet);
//     //         //delay(100); // Give it a moment to initialize
            
//     //         // Check if the reset was successful by verifying IP assignment
//     //         IPAddress newIP = Ethernet.localIP();
//     //         if (newIP != IPAddress(0, 0, 0, 0)) {
//     //             Serial.printf("Ethernet set/reset successful. IP: %s\n", newIP.toString().c_str());
                
//     //             // Update ethernet_connected status
//     //             ethernet_connected = true;
//     //             Serial.println("ethernet_connected flag set to TRUE");
                
//     //             lastConnectionStatus = true;
//     //             return true;
//     //         } else {
//     //             Serial.println("Ethernet reset failed - will retry next full check.");
//     //             ethernet_connected = false;
//     //             lastConnectionStatus = false;
//     //             return false;
//     //         }
//     //     }
//     //     lastConnectionStatus = false;
//     //     return false;
//     //     */ // END OF COMMENTED RESET LOGIC
//     // }
    
//     //Serial.printf("Ethernet connection OK. IP: %s\n", localIP.toString().c_str());
    
//     // Update ethernet_connected status for successful connection
//     ethernet_connected = true;
//     Serial.println("ethernet_connected flag set to TRUE - connection verified");
    
//     lastConnectionStatus = true;
//     return true;

//     // Serial.println("Initializing W5500 Ethernet adapter...");
//     // Serial.flush(); // Ensure message is transmitted before proceeding
    
//     // // Generate unique MAC addresses to prevent network conflicts
//     // generate_unique_mac_addresses();
    
//     // // CRITICAL: Enable 5V power first - W5500 needs 5V power to operate
//     // pinMode(esp32hal->PIN_5V_EN(), OUTPUT);
//     // digitalWrite(esp32hal->PIN_5V_EN(), HIGH);
//     // delay(100);
//     // Serial.println("5V power enabled for W5500");
//     // Serial.flush(); // Ensure message is transmitted
    
//     // // Print current network configuration
//     // // print_network_config();
    
//     // // // Validate network configuration
//     // // if (!validate_static_ip_config()) {
//     // //     Serial.println("Invalid network configuration - Ethernet initialization aborted");
//     // //     return false;
//     // // }
    
//     // // Configure W5500 reset pin and perform standard reset sequence
//     // if (W5500_RST_PIN != -1) {
//     //     Serial.println("Performing W5500 hardware reset sequence...");
//     //     pinMode(W5500_RST_PIN, OUTPUT);
        
//     //     // Standard W5500 reset sequence as per datasheet
//     //     digitalWrite(W5500_RST_PIN, LOW);   // Assert reset
//     //     delay(1);                            // Minimum 500ns, use 1ms to be safe
//     //     digitalWrite(W5500_RST_PIN, HIGH);  // Release reset
//     //     delay(1600);                        // Wait for PLL lock (1.6ms typical)
        
//     //     esp_task_wdt_reset(); // Reset watchdog after delays
//     //     Serial.println("W5500 hardware reset completed");
//     // } else {
//     //     Serial.println("WARNING: W5500_RST_PIN not defined - skipping hardware reset");
//     //     delay(100);
//     //     esp_task_wdt_reset();
//     // }
    
//     // // if (W5500_INT_PIN != -1) {
//     // //     pinMode(W5500_INT_PIN, INPUT_PULLUP);
//     // //     Serial.println("W5500 interrupt pin configured");
//     // // }
    
//     // // Configure CS pin as output and set HIGH (inactive) BEFORE SPI init
//     // pinMode(W5500_CS_PIN, OUTPUT);
//     // digitalWrite(W5500_CS_PIN, HIGH);
//     // delay(100);
//     // Serial.println("W5500 CS pin configured HIGH (inactive)");
    
//     // // Initialize SPI for W5500 with standard settings
//     // SPI.begin(W5500_SCLK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN, W5500_CS_PIN);
    
//     // // Configure SPI settings for W5500 - use conservative 8MHz speed
//     // SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
//     // SPI.endTransaction();
    
//     // Serial.println("SPI bus initialized for W5500 at 8MHz");
//     // Serial.flush();
    
//     // // Initialize Ethernet library with CS pin immediately after SPI setup
//     // Ethernet.init(W5500_CS_PIN);
//     // Serial.println("Ethernet library initialized with W5500 CS pin");
//     // Serial.flush();
    
//     // // Give the W5500 time to be ready for operations
//     // delay(200);  // Shorter delay, similar to working implementation
//     // esp_task_wdt_reset(); // Reset watchdog after delays
    
//     // // Print pin configuration for debugging
//     // Serial.printf("W5500 Pin Configuration - CS: %d, RST: %d, SCLK: %d, MISO: %d, MOSI: %d\n", 
//     //               W5500_CS_PIN, W5500_RST_PIN, W5500_SCLK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN);
//     // Serial.flush();
    
//     // // Verify W5500 hardware detection
//     // Serial.println("Checking W5500 hardware detection...");
//     // Serial.flush();
    
//     // EthernetHardwareStatus hw_status = Ethernet.hardwareStatus();
//     // Serial.printf("Hardware status: %d\n", hw_status);
    
//     // if (hw_status == EthernetNoHardware) {
//     //     Serial.println("ERROR: W5500 hardware not detected!");
//     //     Serial.println("Check wiring and connections");
//     //     return false;
//     // }
    
//     // Serial.println("W5500 hardware detected successfully");
//     // Serial.flush();
    
//     // // Check if static IP is enabled in webserver settings
//     // if (static_IP_enabled) {
//     //     Serial.println("Using static IP configuration from webserver settings...");
        
//     //     // Build IP addresses from webserver settings with Ethernet IP offset by +1
//     //     IPAddress static_ip(static_local_IP1, static_local_IP2, static_local_IP3, static_local_IP4 + 1);  // WiFi IP + 1
//     //     IPAddress gateway(static_gateway1, static_gateway2, static_gateway3, static_gateway4);
//     //     IPAddress subnet(static_subnet1, static_subnet2, static_subnet3, static_subnet4);
//     //     IPAddress dns_server(8, 8, 8, 8);  // Default DNS, could be made configurable
        
//     //     Serial.printf("Ethernet Static IP: %d.%d.%d.%d (WiFi IP + 1)\n", static_local_IP1, static_local_IP2, static_local_IP3, static_local_IP4 + 1);
//     //     Serial.printf("Gateway: %d.%d.%d.%d\n", static_gateway1, static_gateway2, static_gateway3, static_gateway4);
//     //     Serial.printf("Subnet: %d.%d.%d.%d\n", static_subnet1, static_subnet2, static_subnet3, static_subnet4);
//     //     Serial.flush(); // Ensure all configuration messages are transmitted
        
//     //     // Configure with static IP
//     //     Ethernet.begin(Ethernetmac, static_ip, dns_server, gateway, subnet);
//     //     Serial.println("W5500 configured with static IP");
//     //     Serial.flush(); // Ensure message is transmitted
//     // } else {
//     //     Serial.println("Attempting DHCP configuration...");
//     //     Serial.flush(); // Ensure message is transmitted
        
//     //     if (Ethernet.begin(Ethernetmac) == 0) {
//     //         Serial.println("DHCP failed and no static IP configured in webserver!");
//     //         Serial.println("Please configure static IP in webserver settings or ensure DHCP is available.");
//     //         Serial.flush(); // Ensure error messages are transmitted
//     //         return false;
//     //     }
//     //     Serial.println("DHCP configuration successful");
//     //     Serial.flush(); // Ensure message is transmitted
//     // }
    
//     // // Give Ethernet more time to initialize with detailed progress monitoring
//     // Serial.println("Waiting for W5500 initialization and link establishment...");
//     // Serial.flush();
    
//     // // Wait up to 15 seconds for link establishment
//     // for (int i = 0; i < 15; i++) {
//     //     delay(1000);
//     //     esp_task_wdt_reset();
        
//     //     // Check hardware and link status every second
//     //     if (Ethernet.hardwareStatus() == EthernetNoHardware) {
//     //         Serial.println("Hardware lost during initialization!");
//     //         return false;
//     //     }
        
//     //     if (Ethernet.linkStatus() == LinkON) {
//     //         Serial.printf("Link established after %d seconds\n", i + 1);
//     //         break;
//     //     }
        
//     //     if (i % 3 == 0) {
//     //         Serial.printf("Waiting for link... (%d/15 seconds)\n", i + 1);
//     //     }
//     // }
    
//     // // Final link status check
//     // Serial.println("Performing final W5500 link status check...");
//     // Serial.flush();
    
//     // if (Ethernet.linkStatus() == LinkON) {
//     //     ethernet_connected = true;
//     //     ethernet_initialized = true;
        
//     //     Serial.println("W5500 Ethernet initialized successfully!");
//     //     Serial.print("IP Address: ");
//     //     Serial.println(Ethernet.localIP());
//     //     Serial.print("Subnet Mask: ");
//     //     Serial.println(Ethernet.subnetMask());
//     //     Serial.print("Gateway: ");
//     //     Serial.println(Ethernet.gatewayIP());
//     //     Serial.print("DNS Server: ");
//     //     Serial.println(Ethernet.dnsServerIP());
//     //     Serial.flush(); // Ensure all success messages are transmitted
//     //     Serial.print("MAC Address: ");
//     //     for (int i = 0; i < 6; i++) {
//     //         if (i > 0) Serial.print(":");
//     //         if (Ethernetmac[i] < 16) Serial.print("0");
//     //         Serial.print(Ethernetmac[i], HEX);
//     //     }
//     //     Serial.println();
//     //     Serial.flush(); // Ensure MAC address is fully transmitted
        
//     //     return true;
//     // } else {
//     //     Serial.println("W5500 Ethernet initialization failed - no link detected");
//     //     Serial.println("Check: 1) W5500 wiring, 2) Network cable, 3) Pin definitions");
//     //     Serial.flush(); // Ensure error messages are transmitted
//     //     ethernet_connected = false;
//     //     ethernet_initialized = false;
//     //     return false;
//     // }
// }

// Function to check Ethernet connection status (lightweight version of initEthernet)
bool checkEthernetConnection(void) {
    // Check if Ethernet hardware is initialized
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        return false;
    }
    
    // Check link status
    if (Ethernet.linkStatus() == LinkOFF) {
        return false;
    }
    
    // Check if we have a valid IP address
    IPAddress localIP = Ethernet.localIP();
    return (localIP != IPAddress(0, 0, 0, 0));
}


// Check Ethernet connection status - WORKAROUND: Link status only, avoid localIP()
void check_ethernet_status(void) {
    unsigned long currentTime = millis();
    
    // Check every 15 seconds (less aggressive)
    if (currentTime - ethernet_last_check > 15000) {
        ethernet_last_check = currentTime;
        
        if (ethernet_initialized) {
            bool was_connected = ethernet_connected;
            
            // WORKAROUND: Only check link status, avoid localIP() which returns corrupted data
            EthernetLinkStatus link_status = Ethernet.linkStatus();
            
            // Base connection status purely on physical link
            ethernet_connected = (link_status == LinkON);
            
            // Log status changes
            if (was_connected && !ethernet_connected) {
                Serial.println("Ethernet connection lost - Physical link down (check cable)");
            } else if (!was_connected && ethernet_connected) {
                Serial.println("Ethernet connection restored - Physical link up");
                // Don't call Ethernet.localIP() here as it returns corrupted data
                Serial.println("Note: IP address display may show configured/cached value due to W5500 localIP() corruption");
            }
            
            // If link status is unknown, try a basic communication test
            if (link_status == EthernetLinkStatus::Unknown) {
                Serial.println("WARNING: Cannot read W5500 link status - hardware communication issue");
                ethernet_connected = false;
                ethernet_initialized = false;
            }
        }
    }
}

// Get Ethernet connection status
bool is_ethernet_connected(void) {
    return ethernet_connected;
}

// Get Ethernet IP address as string - WORKAROUND: Avoid corrupted localIP() calls
std::string get_ethernet_ip(void) {
    if (ethernet_connected) {
        // WORKAROUND: Return configured IP instead of reading from W5500
        // This avoids the corrupted localIP() readings that show 1.1.1.1, 2.2.2.2, etc.
        
        if (static_IP_enabled) {
            // Return the static IP we configured (WiFi IP + 1)
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", 
                    static_local_IP1, static_local_IP2, static_local_IP3, static_local_IP4 + 1);
            return std::string(ip_str);
        } else {
            // For DHCP, we can't know the exact IP without reading it from W5500
            // Return generic status since localIP() is corrupted
            return "DHCP Connected";
        }
    }
    return "Not Connected";
}

// Comprehensive static IP validation and testing
bool test_static_ip_connectivity(void) {
    if (!static_IP_enabled || !ethernet_connected) {
        Serial.println("Static IP testing skipped - not in static IP mode or not connected");
        return false;
    }
    
    Serial.println("=== COMPREHENSIVE STATIC IP TESTING ===");
    Serial.flush();
    
    // Build expected configuration values (what we passed to Ethernet.begin)
    IPAddress expected_ip(static_local_IP1, static_local_IP2, static_local_IP3, static_local_IP4 + 1);  // WiFi IP + 1
    IPAddress expected_gateway(static_gateway1, static_gateway2, static_gateway3, static_gateway4);
    IPAddress expected_subnet(static_subnet1, static_subnet2, static_subnet3, static_subnet4);
    IPAddress expected_dns(static_gateway1, static_gateway2, static_gateway3, static_gateway4);  // We use gateway as DNS
    
    Serial.println("Expected Configuration:");
    Serial.printf("Expected IP: %s\n", expected_ip.toString().c_str());
    Serial.printf("Expected Gateway: %s\n", expected_gateway.toString().c_str());
    Serial.printf("Expected Subnet: %s\n", expected_subnet.toString().c_str());
    Serial.printf("Expected DNS: %s\n", expected_dns.toString().c_str());
    Serial.flush();
    
    // Test 1: Progressive Configuration Verification
    Serial.println("\nTest 1: Progressive Configuration Verification");
    Serial.println("Testing each parameter individually until all match...");
    Serial.flush();
    
    bool config_matches = false;
    bool ip_verified = false, gateway_verified = false, subnet_verified = false, dns_verified = false;
    
    // Progressive verification - test each parameter individually with targeted delays
    for (int attempt = 1; attempt <= 10; attempt++) {
        Serial.printf("\nProgressive check attempt %d/10:\n", attempt);
        
        // Test IP address first
        if (!ip_verified) {
            IPAddress current_ip = Ethernet.localIP();
            Serial.printf("  Testing IP: Current=%s, Expected=%s -> ", 
                         current_ip.toString().c_str(), expected_ip.toString().c_str());
            
            if (current_ip == expected_ip) {
                Serial.println("✓ IP VERIFIED");
                ip_verified = true;
            } else {
                Serial.println("✗ IP pending...");
            }
        }
        
        // Test Gateway only after IP is verified
        if (ip_verified && !gateway_verified) {
            IPAddress current_gateway = Ethernet.gatewayIP();
            Serial.printf("  Testing Gateway: Current=%s, Expected=%s -> ", 
                         current_gateway.toString().c_str(), expected_gateway.toString().c_str());
            
            if (current_gateway == expected_gateway) {
                Serial.println("✓ GATEWAY VERIFIED");
                gateway_verified = true;
            } else {
                Serial.println("✗ Gateway pending...");
            }
        }
        
        // Test Subnet only after Gateway is verified
        if (gateway_verified && !subnet_verified) {
            IPAddress current_subnet = Ethernet.subnetMask();
            Serial.printf("  Testing Subnet: Current=%s, Expected=%s -> ", 
                         current_subnet.toString().c_str(), expected_subnet.toString().c_str());
            
            if (current_subnet == expected_subnet) {
                Serial.println("✓ SUBNET VERIFIED");
                subnet_verified = true;
            } else {
                Serial.println("✗ Subnet pending...");
            }
        }
        
        // Test DNS only after Subnet is verified
        if (subnet_verified && !dns_verified) {
            IPAddress current_dns = Ethernet.dnsServerIP();
            Serial.printf("  Testing DNS: Current=%s, Expected=%s -> ", 
                         current_dns.toString().c_str(), expected_dns.toString().c_str());
            
            if (current_dns == expected_dns) {
                Serial.println("✓ DNS VERIFIED");
                dns_verified = true;
            } else {
                Serial.println("✗ DNS pending...");
            }
        }
        
        // All individual parameters verified - do final comprehensive check
        if (ip_verified && gateway_verified && subnet_verified && dns_verified) {
            Serial.println("\nAll individual parameters verified! Performing final comprehensive check...");
            
            // Final verification - read all values at once to ensure consistency
            IPAddress final_ip = Ethernet.localIP();
            IPAddress final_gateway = Ethernet.gatewayIP();
            IPAddress final_subnet = Ethernet.subnetMask();
            IPAddress final_dns = Ethernet.dnsServerIP();
            
            bool final_ip_ok = (final_ip == expected_ip);
            bool final_gateway_ok = (final_gateway == expected_gateway);
            bool final_subnet_ok = (final_subnet == expected_subnet);
            bool final_dns_ok = (final_dns == expected_dns);
            
            Serial.printf("Final Check - IP: %s, Gateway: %s, Subnet: %s, DNS: %s\n",
                         final_ip_ok ? "✓" : "✗", final_gateway_ok ? "✓" : "✗",
                         final_subnet_ok ? "✓" : "✗", final_dns_ok ? "✓" : "✗");
            
            if (final_ip_ok && final_gateway_ok && final_subnet_ok && final_dns_ok) {
                Serial.printf("SUCCESS: All configuration values verified and stable after %d attempts!\n", attempt);
                config_matches = true;
                break;
            } else {
                Serial.println("WARNING: Final check failed - some values changed during verification");
                Serial.println("Resetting verification flags and continuing...");
                ip_verified = gateway_verified = subnet_verified = dns_verified = false;
            }
        }
        
        // Progressive delay strategy
        if (attempt < 10) {
            int delay_ms;
            if (attempt <= 3) {
                delay_ms = 1000;  // 1s for first 3 attempts
            } else if (attempt <= 6) {
                delay_ms = 2000;  // 2s for attempts 4-6
            } else {
                delay_ms = 3000;  // 3s for final attempts
            }
            
            Serial.printf("Waiting %d seconds before next check...\n", delay_ms/1000);
            Serial.flush();
            delay(delay_ms);
            //esp_task_wdt_reset();
        }
    }
    
    if (!config_matches) {
        Serial.println("ERROR: W5500 register values do not match configured values after 5 attempts");
        Serial.println("This indicates a W5500 communication or timing issue");
        Serial.println("Proceeding with tests using expected values...");
        Serial.flush();
        
        // Use expected values for remaining tests since W5500 registers are unreliable
        IPAddress current_ip = expected_ip;
        IPAddress current_gateway = expected_gateway;
        IPAddress current_subnet = expected_subnet;
        IPAddress current_dns = expected_dns;
    
    } else {
        Serial.println("SUCCESS: W5500 registers match configured values");
        
        // Use actual values from W5500 for remaining tests
        IPAddress current_ip = Ethernet.localIP();
        IPAddress current_gateway = Ethernet.gatewayIP();
        IPAddress current_subnet = Ethernet.subnetMask();
        IPAddress current_dns = Ethernet.dnsServerIP();
    }
    
    // Get final test values (either from successful W5500 read or expected fallback)
    IPAddress test_ip, test_gateway, test_subnet, test_dns;
    if (config_matches) {
        test_ip = Ethernet.localIP();
        test_gateway = Ethernet.gatewayIP();
        test_subnet = Ethernet.subnetMask();
        test_dns = Ethernet.dnsServerIP();
    } else {
        test_ip = expected_ip;
        test_gateway = expected_gateway;
        test_subnet = expected_subnet;
        test_dns = expected_dns;
    }
    
    Serial.printf("\nUsing values for network tests:\n");
    Serial.printf("Test IP: %s\n", test_ip.toString().c_str());
    Serial.printf("Test Gateway: %s\n", test_gateway.toString().c_str());
    Serial.printf("Test Subnet: %s\n", test_subnet.toString().c_str());
    Serial.printf("Test DNS: %s\n", test_dns.toString().c_str());
    
    // Test 2: Network Calculations
    Serial.println("\nTest 2: Network Calculations");
    
    // Calculate network address
    uint32_t ip_int = (uint32_t(test_ip[0]) << 24) | (uint32_t(test_ip[1]) << 16) | 
                      (uint32_t(test_ip[2]) << 8) | uint32_t(test_ip[3]);
    uint32_t subnet_int = (uint32_t(test_subnet[0]) << 24) | (uint32_t(test_subnet[1]) << 16) | 
                          (uint32_t(test_subnet[2]) << 8) | uint32_t(test_subnet[3]);
    uint32_t network_int = ip_int & subnet_int;
    uint32_t broadcast_int = network_int | (~subnet_int);
    
    IPAddress network_addr = IPAddress((network_int >> 24) & 0xFF, (network_int >> 16) & 0xFF, 
                                      (network_int >> 8) & 0xFF, network_int & 0xFF);
    IPAddress broadcast_addr = IPAddress((broadcast_int >> 24) & 0xFF, (broadcast_int >> 16) & 0xFF, 
                                        (broadcast_int >> 8) & 0xFF, broadcast_int & 0xFF);
    
    Serial.printf("Network Address: %s\n", network_addr.toString().c_str());
    Serial.printf("Broadcast Address: %s\n", broadcast_addr.toString().c_str());
    
    // Test 3: Gateway Reachability (simple check)
    Serial.println("\nTest 3: Gateway Validation");
    uint32_t gateway_int = (uint32_t(test_gateway[0]) << 24) | (uint32_t(test_gateway[1]) << 16) | 
                           (uint32_t(test_gateway[2]) << 8) | uint32_t(test_gateway[3]);
    
    if ((gateway_int & subnet_int) == network_int) {
        Serial.println("✓ Gateway is in same subnet as IP address");
    } else {
        Serial.println("✗ WARNING: Gateway is NOT in same subnet as IP address");
        Serial.println("This may cause routing problems");
    }
    
    // Test 4: IP Address Validation
    Serial.println("\nTest 4: IP Address Validation");
    if (test_ip == IPAddress(0, 0, 0, 0)) {
        Serial.println("✗ ERROR: IP address is 0.0.0.0 (invalid)");
        return false;
    } else if (test_ip == broadcast_addr) {
        Serial.println("✗ ERROR: IP address is broadcast address (invalid)");
        return false;
    } else if (test_ip == network_addr) {
        Serial.println("✗ ERROR: IP address is network address (invalid)");
        return false;
    } else {
        Serial.println("✓ IP address is valid");
    }
    
    // Test 5: Basic ARP test (attempt to create UDP socket)
    Serial.println("\nTest 5: Basic Network Stack Test");
    EthernetUDP test_udp;
    if (test_udp.begin(12345)) {
        Serial.println("✓ UDP socket creation successful - network stack is working");
        test_udp.stop();
    } else {
        Serial.println("✗ WARNING: UDP socket creation failed - possible network stack issue");
    }
    
    Serial.println("=== STATIC IP TESTING COMPLETE ===");
    Serial.flush();
    return true;
}

// Print current network configuration from webserver settings
void print_network_config(void) {
    Serial.println("=== Network Configuration ===");
    Serial.printf("Static IP Enabled: %s\n", static_IP_enabled ? "YES" : "NO");
    
    if (static_IP_enabled) {
        Serial.printf("Static IP: %d.%d.%d.%d\n", static_local_IP1, static_local_IP2, static_local_IP3, static_local_IP4);
        Serial.printf("Gateway: %d.%d.%d.%d\n", static_gateway1, static_gateway2, static_gateway3, static_gateway4);
        Serial.printf("Subnet: %d.%d.%d.%d\n", static_subnet1, static_subnet2, static_subnet3, static_subnet4);
    } else {
        Serial.println("DHCP Mode - IP will be assigned automatically");
    }
    Serial.println("============================");
}

// void init_lcd_display(void) {
//     Serial.println("Initializing T-Connect Pro LCD display");
    
//     // **CRITICAL**: Enable the backlight first - without this, display is invisible!
//     pinMode(SCREEN_BL, OUTPUT);
//     digitalWrite(SCREEN_BL, HIGH);
//     Serial.println("Backlight enabled on pin 46");
    
//     delay(100); // Give backlight time to turn on

//     // Create SPI bus for ST7796 display
//     bus = new Arduino_HWSPI(
//         SCREEN_DC /* DC */, SCREEN_CS /* CS */, SCREEN_SCLK /* SCK */,
//         SCREEN_MOSI /* MOSI */, SCREEN_MISO /* MISO */);
    
//     // Create ST7796 display driver
//     gfx = new Arduino_ST7796(
//         bus, SCREEN_RST /* RST */, ROTATION_LANDSCAPE_FLIPPED /* rotation */, true /* IPS */,
//         SCREEN_WIDTH /* width */, SCREEN_HEIGHT /* height */,
//         49 /* col offset 1 */, 0 /* row offset 1 */, 
//         49 /* col_offset2 */, 0 /* row_offset2 */);
    
//     // Initialize the display
//     Serial.println("Calling gfx->begin()");
//     gfx->begin();
//     Serial.println("Setting rotation to 3");
//     gfx->setRotation(ROTATION_LANDSCAPE_FLIPPED);
//     Serial.println("Filling screen with BLACK");
//     gfx->fillScreen(BLACK);
    
//     delay(500); // Give display time to initialize
    
//     // Draw initial static labels
//     Serial.println("Drawing white text");
//     gfx->setTextColor(WHITE);
//     gfx->setTextSize(TextSize_2);
    
//     // Center the Battery Emulator title with version
//     String title = String("Battery Emulator (") + version_number + ")";
//     int16_t title_width = title.length() * text_base_width * TextSize_2; // Calculate width using defined constants
//     int16_t title_x = (SCREEN_HEIGHT - title_width) / 2; // Center horizontally (rotated screen)
//     gfx->setCursor(title_x, 10);
//     gfx->println(title);
    
//     // Create dynamic subtitle from automated label names with smart truncation
//     // Calculate available space: screen_width - IP_space - margins
//     const int max_ip_chars = 17;  // "xxx.xxx.xxx.xxx *" (15 + 2 for " *")
//     const int ip_display_width = max_ip_chars * text_base_width * TextSize_2;
//     const int left_margin = 10;
//     const int spacing = 10; 
//     const int available_subtitle_width = SCREEN_HEIGHT - left_margin - ip_display_width - spacing;
//     const int max_subtitle_chars = available_subtitle_width / (text_base_width * TextSize_2);
    
//     // Build subtitle from HAL class name with smart truncation
//     String hardwarename = esp32hal->name();  // Use the HAL class name dynamically
    
//     // Smart truncation if subtitle too long
//     if (hardwarename.length() > max_subtitle_chars) {
//         hardwarename = hardwarename.substring(0, max_subtitle_chars - 4) + "...";
//     }
    
//     gfx->setCursor(left_margin, 40);
//     gfx->println(hardwarename);
    
//     // Draw static data labels using constants
//     gfx->setTextSize(TextSize_2);
//     gfx->setCursor(LCD_MARGIN, 70);
//     gfx->print(LCD_VOLTAGE.text);  // "Voltage: "
//     gfx->setCursor(LCD_MARGIN, 100);
//     gfx->print(LCD_CURRENT.text);  // "Current: "
//     gfx->setCursor(LCD_MARGIN, 130);
//     gfx->print(LCD_SOC.text);  // "SoC: "
//     gfx->setCursor(LCD_MARGIN, 160);
//     gfx->print(LCD_TEMP.text);  // "Temp: "
//     gfx->setCursor(LCD_MARGIN, 190);
//     gfx->print(LCD_CPU_TEMP.text);  // "CPU Temp: "
// //    gfx->setCursor(200, 160);
//     // gfx->setCursor(200, 190);
//     // gfx->print("Ethernet: ");
    
//     Serial.println("LCD initialization complete");
    
//     // Test pattern to verify display is working
//     // Serial.println("Drawing test pattern");
//     // gfx->fillRect(50, 50, 100, 50, RED);
//     // gfx->fillRect(200, 100, 50, 100, GREEN);
//     // gfx->fillRect(100, 200, 150, 30, BLUE);
    
//     // Serial.printf("Display size: %dx%d\n", gfx->width(), gfx->height());

//     delay(3000);
// }

void init_LCDDisplay_template(void) {
    Serial.println("Initializing T-Connect Pro LCD display");
    
     gfx->fillScreen(BLACK);
     setBacklightBrightness(Backlight_on);
    
    delay(500); // Give display time to initialize
    
    // Draw initial static labels
    Serial.println("Drawing white text");
    gfx->setTextColor(WHITE);
    gfx->setTextSize(TextSize_2);
    
    // Center the Battery Emulator title with version
    String title = String("Battery Emulator (") + version_number + ")";
    int16_t title_width = title.length() * text_base_width * TextSize_2; // Calculate width using defined constants
    int16_t title_x = (SCREEN_HEIGHT - title_width) / 2; // Center horizontally (rotated screen)
    gfx->setCursor(title_x, 10);
    gfx->println(title);
    
    // Create dynamic subtitle from automated label names with smart truncation
    // Calculate available space: screen_width - IP_space - margins
    const int max_ip_chars = 17;  // "xxx.xxx.xxx.xxx *" (15 + 2 for " *")
    const int ip_display_width = max_ip_chars * text_base_width * TextSize_2;
    const int left_margin = 10;
    const int spacing = 10; 
    const int available_subtitle_width = SCREEN_HEIGHT - left_margin - ip_display_width - spacing;
    const int max_subtitle_chars = available_subtitle_width / (text_base_width * TextSize_2);
    
    // Build subtitle from HAL class name with smart truncation
    String hardwarename = esp32hal->name();  // Use the HAL class name dynamically
    
    // Smart truncation if subtitle too long
    if (hardwarename.length() > max_subtitle_chars) {
        hardwarename = hardwarename.substring(0, max_subtitle_chars - 4) + "...";
    }
    
    gfx->setCursor(left_margin, 40);
    gfx->println(hardwarename);
    
    // Draw static data labels using constants
    gfx->setTextSize(TextSize_2);
    gfx->setCursor(LCD_MARGIN, 70);
    gfx->print(LCD_VOLTAGE.text);  // "Voltage: "
    gfx->setCursor(LCD_MARGIN, 100);
    gfx->print(LCD_CURRENT.text);  // "Current: "
    gfx->setCursor(LCD_MARGIN, 130);
    gfx->print(LCD_SOC.text);  // "SoC: "
    gfx->setCursor(LCD_MARGIN, 160);
    gfx->print(LCD_TEMP.text);  // "Temp: "
    gfx->setCursor(LCD_MARGIN, 190);
    gfx->print(LCD_CPU_TEMP.text);  // "CPU Temp: "
//    gfx->setCursor(200, 160);
    // gfx->setCursor(200, 190);
    // gfx->print("Ethernet: ");
    
    Serial.println("LCD initialization complete");
    
    // Test pattern to verify display is working
    // Serial.println("Drawing test pattern");
    // gfx->fillRect(50, 50, 100, 50, RED);
    // gfx->fillRect(200, 100, 50, 100, GREEN);
    // gfx->fillRect(100, 200, 150, 30, BLUE);
    
    // Serial.printf("Display size: %dx%d\n", gfx->width(), gfx->height());

    delay(3000);
}


void cleanup_lcd_display(void) {
    Serial.println("Cleaning up LCD display");
    if (gfx) {
        delete gfx;
        gfx = nullptr;
    }
    if (bus) {
        delete bus;
        bus = nullptr;
    }
}

void Update_lcd_battery_display(void) {
    // Check for critical error state - display warning and return early
    static bool errorMessageDisplayed = false;
    
    // If error message already displayed, just check if error is still active and return

    // Helper function to calculate data position
    auto calculateDataX = [&](const LCDLabel& label) -> int {
        return LCD_MARGIN + (label.char_count * text_base_width * TextSize_2);
    };
    
    // Update LCD display with battery information using Arduino_GFX
    static unsigned long lastUpdate = 0;
    static bool firstRun = true;
    unsigned long currentTime = millis();
    
    if (firstRun) {
        Serial.println("update_lcd_battery_display() called for first time");
        firstRun = false;
    }
    
    // Update LCD every 1000ms (1 second) to avoid flicker
    if (currentTime - lastUpdate > 1000) {
        lastUpdate = currentTime;

        if (errorMessageDisplayed) {
            if (get_event_pointer(EVENT_ERROR_OPEN_CONTACTOR)->occurences > 0) {
                //Serial.println("Error still active - keeping error message displayed");
                return; // Error still active, keep showing error message
            }
            // Error cleared - reset flag and continue to reinitialize display
            errorMessageDisplayed = false;
            init_LCDDisplay_template();
        }
    
        // Check for new error condition
        if (get_event_pointer(EVENT_ERROR_OPEN_CONTACTOR)->occurences > 0) {
            Serial.println("EVENT_ERROR_OPEN_CONTACTOR detected - displaying error message");
            
            // CRITICAL: Ensure display power remains ON during error display
            // BMS_POWER (GPIO_NUM_1) might be turned off during error conditions
            pinMode(esp32hal->PIN_5V_EN(), OUTPUT);
            digitalWrite(esp32hal->PIN_5V_EN(), HIGH);  // Keep 5V rail powered
            
            // Ensure backlight pin is powered
            pinMode(SCREEN_BL, OUTPUT);
            digitalWrite(SCREEN_BL, HIGH);
            
            // Clear the screen with black background
            gfx->fillScreen(BLACK);
            setBacklightBrightness(Backlight_on);
            
            // Display critical error message with power-safe approach and centered text
            gfx->setTextColor(RED, BLACK);
            gfx->setTextSize(TEXT_SIZE_4);
        
            // Calculate screen center
            int16_t screenCenterX = gfx->width() / 2;
            int16_t screenCenterY = gfx->height() / 2;
            
            // Display main error message (centered)
            String mainMsg = "CRITICAL ERROR!";
            int16_t mainMsgWidth = calculateTextWidth(mainMsg, TEXT_SIZE_4);
            int16_t mainMsgX = screenCenterX - (mainMsgWidth / 2);
            int16_t mainMsgY = screenCenterY - (text_base_height * TEXT_SIZE_4 * 2); // Above center
            gfx->setCursor(mainMsgX, mainMsgY);
            gfx->println(mainMsg);
            
            // Display error details (centered)
            gfx->setTextColor(WHITE, BLACK);
            gfx->setTextSize(TEXT_SIZE_3);
            
            String line1 = "Contactors opened.";
            int16_t line1Width = calculateTextWidth(line1, TEXT_SIZE_3);
            int16_t line1X = screenCenterX - (line1Width / 2);
            int16_t line1Y = screenCenterY - (text_base_height * TEXT_SIZE_3 / 2); // Near center
            gfx->setCursor(line1X, line1Y);
            gfx->println(line1);
            
            String line2 = "Power cycle required.";
            int16_t line2Width = calculateTextWidth(line2, TEXT_SIZE_3);
            int16_t line2X = screenCenterX - (line2Width / 2);
            int16_t line2Y = screenCenterY + (text_base_height * TEXT_SIZE_3); // Below center
            gfx->setCursor(line2X, line2Y);
            gfx->println(line2);
            
            // Force display update and power protection
            Serial.println("Critical error message displayed on LCD with power protection");
            
            errorMessageDisplayed = true;
            return; // Exit early - don't display normal battery info during critical error
        }




        // Serial.println("Updating LCD display");
        // Serial.printf("Screen size: %dx%d\n", gfx->width(), gfx->height());
        
        // Store previous values for black overwrite
        static float lastVoltage = -1;
        static float lastCurrent = -1;
        static int lastSoC = -1;
        static float lastTemp = -1;
        static EMULATOR_STATUS lastStatus = EMULATOR_STATUS::STATUS_UPDATING;
        static float lastCpuUsage = -1;
        static float lastCpuTemp = -999;
        static bool lastEthernetStatus = false;
        static String lastEthernetStatusText = "";  // Track the actual text displayed
        
        // Get current values
        float currentVoltage = datalayer.battery.status.voltage_dV / 10.0;
        float currentCurrent = datalayer.battery.status.current_dA / 10.0;
        int currentSoC = datalayer.battery.status.reported_soc / 100;
        float currentTemp = datalayer.battery.status.temperature_min_dC / 10.0;
        EMULATOR_STATUS currentStatus = get_emulator_status();
        float currentCpuUsage = Calculate_cpu_usage();
        float currentCpuTemp = get_cpu_temperature();
        
        //temporarily removed since no ethernet included.
        
        // // Check Ethernet status
        // check_ethernet_status();
        // bool currentEthernetStatus = is_ethernet_connected();
        
        // // WORKAROUND: Avoid direct Ethernet.localIP() call due to corruption
        // // Serial.print("Frist time Ethernet IP: ");
        // // Serial.println(Ethernet.localIP().toString());
        
        // // Print current IP address information
        // Serial.println("=== Network Status ===");
        // Serial.println("Checking network connection status...");
        // Serial.println("Ethernetstatus: " + String(currentEthernetStatus ? "Connected" : "Disconnected"));
        // if (currentEthernetStatus) {
        //     Serial.println("Ethernet: Connected (Link Up)");
        //     Serial.print("Ethernet IP: ");
        //     Serial.println(get_ethernet_ip().c_str());
            
        //     // Display complete network configuration for troubleshooting
        //     Serial.print("Ethernet Gateway: ");
        //     Serial.println(Ethernet.gatewayIP().toString().c_str());
        //     Serial.print("Ethernet Subnet: ");
        //     Serial.println(Ethernet.subnetMask().toString().c_str());
        //     Serial.print("Ethernet DNS: ");
        //     Serial.println(Ethernet.dnsServerIP().toString().c_str());
        // } else {
        //     Serial.println("Ethernet: Disconnected (Link Down)");
        // }
        
        // if (WiFi.status() == WL_CONNECTED) {
        //     Serial.println("WiFi: Connected");
        //     Serial.print("WiFi IP: ");
        //     Serial.println(WiFi.localIP().toString());
        // } else {
        //     Serial.println("WiFi: Disconnected");
        // }
        // Serial.println("=====================");
        
        // // Display IP address on LCD next to T-Connect Pro
        // static String lastDisplayedIP = "";
        // String currentIP = "";
        // bool isWiFi = false;
        
        // // Determine which connection has priority and get IP - Ethernet first
        // if (currentEthernetStatus) {
        //     currentIP = String(get_ethernet_ip().c_str());
        //     isWiFi = false;
        // } else if (WiFi.status() == WL_CONNECTED) {
        //     currentIP = WiFi.localIP().toString() + " *";  // Add asterisk for WiFi
        //     isWiFi = true;
        // }
        
        // // Update LCD if IP changed or first run
        // if (currentIP != lastDisplayedIP) {
        //     // Clear old IP display area - clear full width to ensure complete erasure
        //     if (lastDisplayedIP.length() > 0) {
        //         // Calculate right-justified position for old IP to clear it properly
        //         int oldIpWidth = lastDisplayedIP.length() * text_base_width * TextSize_2;
        //         int oldIpX = SCREEN_HEIGHT - oldIpWidth - 5; // 5 pixel right margin
        //         gfx->setCursor(oldIpX, 40);
        //         gfx->setTextColor(BLACK);
        //         gfx->setTextSize(TextSize_2);
        //         gfx->print(lastDisplayedIP);
        //     }
            
        //     // Display new IP address right-justified
        //     if (currentIP.length() > 0) {
        //         // Calculate right-justified position based on IP text length
        //         int ipWidth = currentIP.length() * text_base_width * TextSize_2;
        //         int ipX = SCREEN_HEIGHT - ipWidth - 5; // 5 pixel right margin
        //         gfx->setCursor(ipX, 40);
        //         gfx->setTextColor(CYAN);
        //         gfx->setTextSize(TextSize_2);
        //         gfx->print(currentIP);
        //     }
            
        //     lastDisplayedIP = currentIP;
        //     Serial.println("Updated IP address display on LCD (right-justified): " + currentIP);
        // }
        
        // Update voltage if changed
        if (lastVoltage != currentVoltage) {
            int voltageX = calculateDataX(LCD_VOLTAGE);  // Calculate position after "Voltage: "
            gfx->setCursor(voltageX, 70);
            if (lastVoltage >= 0) {  // Clear old value by overwriting in black
                gfx->setTextColor(BLACK);
                gfx->printf("%.1fV", lastVoltage);
            }
            gfx->setCursor(voltageX, 70);
            gfx->setTextColor(CYAN);
            gfx->printf("%.1fV", currentVoltage);
            lastVoltage = currentVoltage;
            Serial.println("Updated voltage display");
        }
        
        // Update current if changed
        if (lastCurrent != currentCurrent) {
            int currentX = calculateDataX(LCD_CURRENT);  // Calculate position after "Current: "
            gfx->setCursor(currentX, 100);
            if (lastCurrent >= -999) {  // Clear old value by overwriting in black
                gfx->setTextColor(BLACK);
                gfx->printf("%.1fA", lastCurrent);
            }
            gfx->setCursor(currentX, 100);
            gfx->setTextColor(CYAN);
            gfx->printf("%.1fA", currentCurrent);
            lastCurrent = currentCurrent;
            Serial.println("Updated current display");
        }
        
        // Update SoC if changed
        if (lastSoC != currentSoC) {
            int socX = calculateDataX(LCD_SOC);  // Calculate position after "SoC: "
            gfx->setCursor(socX, 130);
            if (lastSoC >= 0) {  // Clear old value by overwriting in black
                gfx->setTextColor(BLACK);
                gfx->printf("%d%%", lastSoC);
            }
            gfx->setCursor(socX, 130);
            gfx->setTextColor(CYAN);
            gfx->printf("%d%%", currentSoC);
            lastSoC = currentSoC;
            Serial.println("Updated SoC display");
        }
        
        // Update temperature if changed
        if (lastTemp != currentTemp) {
            int tempX = calculateDataX(LCD_TEMP);  // Calculate position after "Temp: "
            gfx->setCursor(tempX, 160);
            if (lastTemp >= -999) {  // Clear old value by overwriting in black
                gfx->setTextColor(BLACK);
                gfx->printf("%.1fC", lastTemp);
            }
            gfx->setCursor(tempX, 160);
            gfx->setTextColor(CYAN);
            gfx->printf("%.1fC", currentTemp);
            lastTemp = currentTemp;
            Serial.println("Updated temperature display");
        }
        
        // Update status if changed
        // if (lastStatus != currentStatus) {
        //     gfx->setCursor(100, 190);
            
        //     // Clear old status by overwriting in black
        //     if (lastStatus != EMULATOR_STATUS::STATUS_UPDATING) {  // Skip first time
        //         gfx->setTextColor(BLACK);
        //         switch (lastStatus) {
        //             case EMULATOR_STATUS::STATUS_OK:
        //                 gfx->print("OK");
        //                 break;
        //             case EMULATOR_STATUS::STATUS_WARNING:
        //                 gfx->print("WARNING");
        //                 break;
        //             case EMULATOR_STATUS::STATUS_ERROR:
        //                 gfx->print("ERROR");
        //                 break;
        //             case EMULATOR_STATUS::STATUS_UPDATING:
        //                 gfx->print("UPDATING");
        //                 break;
        //         }
        //     }
            
        //     // Draw new status with appropriate color
        //     gfx->setCursor(100, 190);
        //     Serial.printf("Emulator status: %d\n", (int)currentStatus);
        //     switch (currentStatus) {
        //         case EMULATOR_STATUS::STATUS_OK:
        //             gfx->setTextColor(GREEN);
        //             Serial.println("Setting status to GREEN (OK)");
        //             gfx->print("OK");
        //             break;
        //         case EMULATOR_STATUS::STATUS_WARNING:
        //             gfx->setTextColor(YELLOW);
        //             Serial.println("Setting status to YELLOW (WARNING)");
        //             gfx->print("WARNING");
        //             break;
        //         case EMULATOR_STATUS::STATUS_ERROR:
        //             gfx->setTextColor(RED);
        //             Serial.println("Setting status to RED (ERROR)");
        //             gfx->print("ERROR");
        //             break;
        //         case EMULATOR_STATUS::STATUS_UPDATING:
        //             gfx->setTextColor(BLUE);
        //             Serial.println("Setting status to BLUE (UPDATING)");
        //             gfx->print("UPDATING");
        //             break;
        //     }
        //     lastStatus = currentStatus;
        //     Serial.println("Updated status display");
        // }
        
        // Update CPU usage if changed (with 1% threshold to reduce flicker)
        // if (abs(lastCpuUsage - currentCpuUsage) >= 1.0) {
        //     gfx->setCursor(300, 190);
        //     if (lastCpuUsage >= 0) {  // Clear old value by overwriting in black
        //         gfx->setTextColor(BLACK);
        //         gfx->printf("%.1f%%", lastCpuUsage);
        //     }
        //     gfx->setCursor(300, 190);
            
        //     // Color-code CPU usage: Green<50%, Yellow 50-80%, Red>80%
        //     if (currentCpuUsage < 50.0) {
        //         gfx->setTextColor(GREEN);
        //     } else if (currentCpuUsage < 80.0) {
        //         gfx->setTextColor(YELLOW);
        //     } else {
        //         gfx->setTextColor(RED);
        //     }
        //     gfx->printf("%.1f%%", currentCpuUsage);
        //     lastCpuUsage = currentCpuUsage;
        //     Serial.printf("Updated CPU usage display: %.1f%%\n", currentCpuUsage);
        // }
        
        // Update CPU temperature if changed (with 0.5°C threshold to reduce flicker)
        if (abs(lastCpuTemp - currentCpuTemp) >= 0.5) {
            int cpuTempX = calculateDataX(LCD_CPU_TEMP);  // Calculate position after "CPU Temp: "
            gfx->setCursor(cpuTempX, 190);
            if (lastCpuTemp > -999) {  // Clear old value by overwriting in black
                gfx->setTextColor(BLACK);
                gfx->printf("%.1fC", lastCpuTemp);
            }
            gfx->setCursor(cpuTempX, 190);
            
            // Color-code CPU temperature: Green<65°C, Yellow 65-80°C, Red>80°C
            if (currentCpuTemp < 65.0) {
                gfx->setTextColor(GREEN);
            } else if (currentCpuTemp < 80.0) {
                gfx->setTextColor(YELLOW);
            } else {
                gfx->setTextColor(RED);
            }
            gfx->printf("%.1fC", currentCpuTemp);
            lastCpuTemp = currentCpuTemp;
            Serial.printf("Updated CPU temperature display: %.1fC\n", currentCpuTemp);
        }
        
        // Update Ethernet status if changed
        // Serial.println("Ethernet Status: " + String(currentEthernetStatus));
//         if (lastEthernetStatus != currentEthernetStatus) {
//             // Clear old status text by overwriting it in black (if we have previous text)
//             if (lastEthernetStatusText.length() > 0) {
//  //               gfx->setCursor(320, 160);
//                 gfx->setCursor(320, 190);
//                 gfx->setTextColor(BLACK);
//                 gfx->print(lastEthernetStatusText);
//             }
            
//             // Determine new status text and color
//             String newStatusText;
//             uint16_t newStatusColor;
//             if (currentEthernetStatus) {
//                 newStatusText = "Connected";
//                 newStatusColor = GREEN;
//                 Serial.println("Updated Ethernet display: Connected");
//             } else {
//                 newStatusText = "Disconnected";
//                 newStatusColor = RED;
//                 Serial.println("Updated Ethernet display: Disconnected");
//             }
            
//             // Display current Ethernet status with color coding
// //            gfx->setCursor(320, 160);
//             gfx->setCursor(320, 190);
//             gfx->setTextColor(newStatusColor);
//             gfx->print(newStatusText);
            
//             // Update tracking variables
//             lastEthernetStatus = currentEthernetStatus;
//             lastEthernetStatusText = newStatusText;
//         }
        
//        Serial.println("LCD update completed");
    }
}

// KeySearchResult ethernetClientHttpGet(const char* apiCall, const char* keyToFind, const char* varName) {
//     KeySearchResult result;
//     result.name = varName;
//     // ...function logic...
//     return result;
// }

//uint8_t Current_Rotation_1 = 1;

String BaseURL = "";
//FS &fsTPRO = LittleFS;

const char* ntpServer1;
const char* ntpServer2;
const char* time_zone;





// LED status indicator function - displays LED simulation on LCD
void Display_LED_Status(uint32_t pixel_color) {
    if (!gfx) return; // Safety check

//    Serial.printf("Displaying LED status with color: 0x%08X\n", pixel_color);
//`    Serial.println(pixel_color, HEX);

    // Extract RGB components from 32-bit color (format: 0x00RRGGBB)
    uint8_t red = (pixel_color >> 16) & 0xFF;
    uint8_t green = (pixel_color >> 8) & 0xFF;
    uint8_t blue = pixel_color & 0xFF;
    
    // Position LED indicator at top-right corner
    int led_radius = 12;             // 12 pixel radius
    int led_x = SCREEN_HEIGHT - (led_radius + 2);  // led_radius pixels from right edge + padding of 2
    int led_y = SCREEN_WIDTH/2;                  // Centered vertically

    
    // Convert RGB to 16-bit color format for display
    uint16_t display_color = gfx->color565(red, green, blue);
    
    gfx->fillCircle(led_x, led_y, led_radius, display_color);

    // // Clear previous LED by drawing black circle with border
    // gfx->fillCircle(led_x, led_y, led_radius + 2, BLACK);
    
    // // Draw LED indicator with white border for visibility
    // gfx->drawCircle(led_x, led_y, led_radius + 1, WHITE);
    // gfx->fillCircle(led_x, led_y, led_radius, display_color);
}



// Function to check internet connectivity by connecting to external site
bool checkInternetConnectivity(const char* testHost, uint16_t testPort, uint32_t timeoutMs) {
    static unsigned long lastInternetCheck = 0;
    static bool lastInternetStatus = false;
    const unsigned long INTERNET_CHECK_INTERVAL = 60000; // 1 minute
    
    unsigned long currentTime = millis();
    
    // Only do full internet check every minute, otherwise return cached status
    if (currentTime - lastInternetCheck < INTERNET_CHECK_INTERVAL && lastInternetCheck != 0) {
        return lastInternetStatus;
    }
    
    lastInternetCheck = currentTime;
    Serial.printf("Performing full internet connectivity test to %s:%d...\n", testHost, testPort);
    
    // First check if Ethernet is connected and has a valid IP
    if (!is_ethernet_connected()) {
        Serial.println("Internet test skipped - Ethernet not connected");
        lastInternetStatus = false;
        return false;
    }
    
    // Check if we have a valid IP address
    IPAddress localIP = Ethernet.localIP();
    if (localIP == IPAddress(0, 0, 0, 0)) {
        Serial.println("Internet test skipped - no valid IP address");
        lastInternetStatus = false;
        return false;
    }
    
    EthernetClient client;
    unsigned long startTime = millis();
    
    // Attempt to connect to external server
    if (client.connect(testHost, testPort)) {
        unsigned long connectTime = millis() - startTime;
        Serial.printf("Internet connectivity OK. Connected in %lu ms\n", connectTime);
        client.stop();
        lastInternetStatus = true;
        return true;
    } else {
        unsigned long failTime = millis() - startTime;
        Serial.printf("Internet connectivity failed. Timeout after %lu ms\n", failTime);
        client.stop();
        lastInternetStatus = false;
        return false;
    }
}

// Comprehensive startup sequence with parallel ethernet initialization
// Display splash screen content without timing control (separated from displaySplashScreen)
void displaySplashScreenContent() {
    Serial.println("Displaying splash screen content...");
    
    // Set rotation to match the rest of the application
    gfx->setRotation(ROTATION_LANDSCAPE_FLIPPED);
    
    // Clear screen with black background
    gfx->fillScreen(BLACK);
    
    // Initialize LittleFS if not already done
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: Could not initialize LittleFS!");
        // Show error message on screen
        gfx->setTextColor(RED, BLACK);
        gfx->setTextSize(1);
        gfx->setCursor(10, 10);
        gfx->println("LittleFS Error");
        return;
    }

    // Debug the filesystem
    //debugLittleFSFiles();

    // Try to display JPG splash screen from LittleFS
    bool imageDisplayed = false;
    
    if (LittleFS.exists(splashFile)) {
        Serial.printf("Found %s, displaying...\n", splashFile);
        displaySplashJpeg(splashFile);
        imageDisplayed = true;
    }
    
    if (!imageDisplayed) {
        Serial.println("No splash image found, showing text splash");
        // Fallback text splash screen
        gfx->setTextColor(WHITE, BLACK);
        gfx->setTextSize(2);
        
        // Center the text on screen
        int16_t x = (gfx->width() - (strlen("T-Connect Pro") * 12)) / 2;
        int16_t y = gfx->height() / 2 - 20;
        
        gfx->setCursor(x, y);
        gfx->println("T-Connect Pro");
        
        // Add version info
        gfx->setTextSize(1);
        x = (gfx->width() - (strlen("Battery Emulator") * 6)) / 2;
        y += 30;
        gfx->setCursor(x, y);
        gfx->println("Battery Emulator");
    }
}


// Helper function to convert LCDColor to 16-bit color565 format
uint16_t LCDtoColor565(const LCDColor& color) {
    return gfx->color565(color.r, color.g, color.b);
}

// Get Ethernet connection status from connectivity task
bool getEthernetStatus(void) {
    // Use the global volatile variables from connectivity monitoring
    extern volatile bool ethernetConnected;
    return ethernetConnected;
}

// Get Internet connection status from connectivity task
bool getInternetStatus(void) {
    // Use the global volatile variables from connectivity monitoring
    extern volatile bool internetConnected;
    return internetConnected;
}

// Display JPEG splash screen from LittleFS
void displaySplashJpeg(const char* filename) {
    Serial.printf("Attempting to display JPEG: %s\n", filename);
    
    // Try to decode the JPEG file from LittleFS
    if (JpegDec.decodeFsFile(filename)) {
        Serial.println("JPEG decode successful");
        Serial.printf("JPEG dimensions: %d x %d\n", JpegDec.width, JpegDec.height);
        
        // Calculate position to center the image on screen
        int16_t xpos = (gfx->width() - JpegDec.width) / 2;
        int16_t ypos = (gfx->height() - JpegDec.height) / 2;
        
        // Ensure we don't go negative
        if (xpos < 0) xpos = 0;
        if (ypos < 0) ypos = 0;
        
        Serial.printf("Displaying at position: %d, %d\n", xpos, ypos);
        
        // Display the image pixel by pixel
        uint16_t *pImg;
        uint32_t mcu_w = JpegDec.MCUWidth;
        uint32_t mcu_h = JpegDec.MCUHeight;
        uint32_t max_x = JpegDec.width;
        uint32_t max_y = JpegDec.height;
        
        uint32_t min_w = (mcu_w < max_x) ? mcu_w : max_x;
        uint32_t min_h = (mcu_h < max_y) ? mcu_h : max_y;
        
        uint32_t win_w = min_w;
        uint32_t win_h = min_h;
        
        max_x += xpos;
        max_y += ypos;
        
        // Read each MCU block and display it
        while (JpegDec.read()) {
            pImg = JpegDec.pImage;
            
            int32_t mcu_x = JpegDec.MCUx * mcu_w + xpos;
            int32_t mcu_y = JpegDec.MCUy * mcu_h + ypos;
            
            if ((mcu_x + win_w) <= max_x) win_w = mcu_w;
            else win_w = max_x - mcu_x;
            
            if ((mcu_y + win_h) <= max_y) win_h = mcu_h;
            else win_h = max_y - mcu_y;
            
            if (win_w > 0 && win_h > 0) {
                gfx->draw16bitRGBBitmap(mcu_x, mcu_y, pImg, win_w, win_h);
            }
        }
        
        Serial.println("JPEG display completed");
        // Timing and backlight control now handled by startup sequence
    } else {
        Serial.printf("ERROR: Failed to decode JPEG file %s\n", filename);
        
        // Show error on screen
        gfx->setTextColor(RED, BLACK);
        gfx->setTextSize(1);
        gfx->setCursor(10, 10);
        gfx->printf("JPEG decode failed: %s", filename);
    }
}

// Debug LittleFS files
void debugLittleFSFiles() {
    Serial.println("=== LittleFS File List ===");
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("Failed to open root directory");
        return;
    }
    
    File file = root.openNextFile();
    int fileCount = 0;
    while (file) {
        Serial.printf("File: %s (Size: %d bytes)\n", file.name(), file.size());
        fileCount++;
        file = root.openNextFile();
    }
    Serial.printf("Total files found: %d\n", fileCount);
    Serial.println("==========================");
    root.close();
}



// Draw ethernet status indicator in corner of splash screen
void drawEthernetStatusIndicator() {
    int indicatorX = gfx->width() - 20;
    int indicatorY = gfx->height() - 20;
    static bool indicatorState = false;
    static unsigned long lastUpdate = 0;
    
    uint16_t indicatorColor;
    
    // Determine indicator color based on ethernet status
    // During startup monitoring, show GREEN for ethernet connection (internet check comes later)
    if (getEthernetStatus()) {
        indicatorColor = LCDtoColor565(RGB_BRIGHT_GREEN);  // Green - ethernet connected
    } else {
        indicatorColor = LCDtoColor565(RGB_ORANGE);  // Orange - still connecting
    }
    
    // Update indicator state every 500ms for flashing effect
    if (millis() - lastUpdate >= 500) {
        lastUpdate = millis();
        indicatorState = !indicatorState;
    }
    
    // Always draw the indicator (flashing based on indicatorState)
    drawStatusIndicator(indicatorX, indicatorY, 8, indicatorColor, indicatorState);
}

// Function to draw a flashing status indicator dot
void drawStatusIndicator(int x, int y, int radius, uint16_t color, bool isOn) {
    if (isOn) {
        // Bright state - full color
        gfx->fillCircle(x, y, radius, color);
    } else {
        // Dim state - darker version of the actual color passed in
        // Extract RGB components from 16-bit color and dim them
        uint8_t r = ((color >> 11) & 0x1F) << 3;  // Extract red (5 bits) and scale to 8 bits
        uint8_t g = ((color >> 5) & 0x3F) << 2;   // Extract green (6 bits) and scale to 8 bits  
        uint8_t b = (color & 0x1F) << 3;          // Extract blue (5 bits) and scale to 8 bits
        
        // Dim the color by reducing each component by half
        r = r >> 1;
        g = g >> 1;
        b = b >> 1;
        
        // Convert back to 16-bit color
        uint16_t dimColor = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        
        gfx->fillCircle(x, y, radius, dimColor); // Draw dimmed circle at full radius
    }
}


// // Splash screen display function
// void displaySplashScreen() {
//     Serial.println("Displaying splash screen...");
    
//     // Set rotation to match the rest of the application (landscape mode)
//     gfx->setRotation(ROTATION_LANDSCAPE_FLIPPED);
    
//     // Clear screen with black background
//     gfx->fillScreen(BLACK);
    
//     // Initialize LittleFS if not already done
//     if (!LittleFS.begin(true)) {
//         Serial.println("ERROR: Could not initialize LittleFS!");
//         // Show error message on screen
//         gfx->setTextColor(RED, BLACK);
//         gfx->setTextSize(1);
//         gfx->setCursor(10, 10);
//         gfx->println("LittleFS Error");
//         if (ethernetPresent && xTaskGetCurrentTaskHandle() != NULL) {
//             vTaskDelay(pdMS_TO_TICKS(2000)); // Task-friendly delay
//         } else if (ethernetPresent) {
//             delay(2000); // Fallback to Arduino delay if not in task context
//         }
//         return;
//     }
    
//     // Debug the filesystem
//     debugLittleFSFiles();
    
//     // Try to display JPG splash screen from LittleFS
//     // Check multiple possible file names
//     const char* possibleFiles[] = {
//         "/BatteryEmulator4.jpg",
//         "/Battery Emulator.jpg", 
//         "/BatteryEmulator3.jpg",
//         "/BatteryEmulator2.jpg"
//     };
    
//     bool imageDisplayed = false;
//     int arraySize = sizeof(possibleFiles) / sizeof(possibleFiles[0]);
    
//     for (int i = 0; i < arraySize; i++) {
//         if (LittleFS.exists(possibleFiles[i])) {
//             Serial.printf("Found %s, attempting to display...\n", possibleFiles[i]);
            
//             // Actually display the JPEG image
//             displaySplashJpeg(possibleFiles[i]);
            
//             imageDisplayed = true;
//             if (ethernetPresent && xTaskGetCurrentTaskHandle() != NULL) {
//                 vTaskDelay(pdMS_TO_TICKS(3000)); // Show splash for 3 seconds (task-friendly)
//             } else if (ethernetPresent) {
//                 delay(3000); // Fallback to Arduino delay if not in task context
//             }
//             break;
//         }
//     }
    
//     if (!imageDisplayed) {
//         Serial.println("No splash image files found, showing text splash screen");
//         // Fallback text splash screen if JPG not found
//         gfx->setTextColor(WHITE, BLACK);
//         gfx->setTextSize(2);
        
//         // Center the text on screen
//         int16_t x = (gfx->width() - (strlen("T-Connect Pro") * 12)) / 2;
//         int16_t y = gfx->height() / 2 - 20;
        
//         gfx->setCursor(x, y);
//         gfx->println("T-Connect Pro");
        
//         // Add version or additional info
//         gfx->setTextSize(1);
//         x = (gfx->width() - (strlen("Battery Emulator") * 6)) / 2;
//         y += 30;
//         gfx->setCursor(x, y);
//         gfx->println("Battery Emulator");
        
//         Serial.println("Text splash screen displayed");
        
//         // Ethernet status indicator is now running as background task
//         if (ethernetPresent && xTaskGetCurrentTaskHandle() != NULL) {
//             vTaskDelay(pdMS_TO_TICKS(6000)); // Show text splash for 6 seconds
//         } else if (ethernetPresent) {
//             delay(6000); // Fallback to Arduino delay if not in task context
//         }
//     }
    
//         // Clear screen after splash
//     gfx->fillScreen(BLACK);
// }

// ===== Display Backlight Control Functions =====

// Internal variable to track current brightness (more reliable than ledcRead)
static uint8_t currentBacklightBrightness = Backlight_on;  // Default to full brightness

// Set backlight brightness (0-255, where 0=off, 255=full brightness)
void setBacklightBrightness(uint8_t brightness) {
    ledcWrite(SCREEN_BL, brightness);  // Channel 1 is used for backlight PWM
    currentBacklightBrightness = brightness;  // Track internally
}

// Get current backlight brightness
uint8_t getBacklightBrightness() {
    // Use internal tracking instead of ledcRead() to avoid timing issues
    return currentBacklightBrightness;
}

// Initialize/sync brightness with current PWM state (call this after PWM setup)
void initBacklightBrightness() {
    // Try to read from PWM and sync with internal tracking
    uint8_t pwmValue = ledcRead(1);
    currentBacklightBrightness = pwmValue;  // Always sync with current PWM value
    Serial.printf("Synced backlight brightness with PWM: %d\n", currentBacklightBrightness);
}

// Helper function to get cached brightness calculation array
// Returns a pointer to a static array with brightness values for smooth fading
uint8_t* getBrightnessArray(uint8_t startBrightness, uint8_t endBrightness, uint16_t steps) {
    // Cache structure to avoid recalculation for common fade patterns
    struct BrightnessCache {
        uint8_t start;
        uint8_t end;
        uint16_t steps;
        uint8_t* values;
        bool valid;
    };
    
    static BrightnessCache cache = {0, 0, 0, nullptr, false};
    
    // Check if we can use cached values (same start, end, and steps)
    if (cache.valid && cache.start == startBrightness && 
        cache.end == endBrightness && cache.steps == steps) {
        Serial.println("Using cached brightness array");
        return cache.values;
    }
    
    // Free old cache if it exists
    if (cache.values != nullptr) {
        delete[] cache.values;
        cache.values = nullptr;
    }
    
    // Allocate new array for brightness values
    cache.values = new uint8_t[steps + 1];
    if (cache.values == nullptr) {
        Serial.println("ERROR: Failed to allocate brightness cache array");
        cache.valid = false;
        return nullptr;
    }
    
    // Calculate brightness values using linear interpolation
    float startFloat = (float)startBrightness;
    float endFloat = (float)endBrightness;
    float brightnessRange = endFloat - startFloat;
    
    for (uint16_t i = 0; i <= steps; i++) {
        float progress = (float)i / (float)steps;  // 0.0 to 1.0
        cache.values[i] = (uint8_t)(startFloat + (brightnessRange * progress));
    }
    
    // Update cache metadata
    cache.start = startBrightness;
    cache.end = endBrightness;
    cache.steps = steps;
    cache.valid = true;
    
    Serial.printf("Calculated new brightness array: %d->%d in %d steps\n", 
                  startBrightness, endBrightness, steps);
    return cache.values;
}

// Fade backlight to target brightness over specified duration
void fadeBacklight(uint8_t targetBrightness, uint32_t durationMs, uint16_t steps, bool ethernetPresent) {
    uint8_t currentBrightness = getBacklightBrightness();
    
    if (currentBrightness == targetBrightness) {
        Serial.printf("Backlight already at target brightness: %d\n", targetBrightness);
        return;  // Already at target brightness
    }
    
    // Ensure minimum values to prevent division by zero or too fast updates
    if (steps < 1) steps = 1;
    if (durationMs < 50) durationMs = 50;
    
    // Calculate step delay - ensure it's consistent and reasonable
    uint32_t stepDelay = durationMs / steps;
    if (stepDelay < 20) stepDelay = 20;  // Minimum delay of 20ms per step for smoother animation
    if (stepDelay > 500) stepDelay = 500; // Maximum delay to prevent too slow fading
    
    Serial.printf("Fading backlight from %d to %d in %d steps (%.1fms delay)\n", 
                  currentBrightness, targetBrightness, steps, (float)stepDelay);
    
    // Get cached brightness values to avoid recalculation
    uint8_t* brightnessValues = getBrightnessArray(currentBrightness, targetBrightness, steps);
    if (brightnessValues == nullptr) {
        Serial.println("ERROR: Failed to get brightness array - aborting fade");
        return;
    }
    
    // Smooth fade with pre-calculated values
    for (uint16_t step = 1; step <= steps; step++) {
        setBacklightBrightness(brightnessValues[step]);
        Serial.printf("Step %d/%d: Brightness %d\n", step, steps, brightnessValues[step]);
        // Consistent delay for smooth animation
        smartDelay(stepDelay, ethernetPresent);
    }
    
    // Final verification and force set to ensure we're exactly at target
    setBacklightBrightness(targetBrightness);
    
    Serial.printf("Backlight fade complete - final brightness: %d\n", targetBrightness);
}

// Overloaded fadeBacklight - allows setting ethernetPresent with default steps (85)
void fadeBacklight(uint8_t targetBrightness, uint32_t durationMs, bool ethernetPresent) {
    fadeBacklight(targetBrightness, durationMs, 85, ethernetPresent);  // Call main function with default steps=85
}


// Function to display Ethernet connection status on screen
void displayEthernetConnectionStatus(int x, int y, uint16_t textColor, uint16_t bgColor) {
    static String prevStatus = "";
    String currentStatus = "";
    uint16_t statusColor = textColor;
    
    // Check hardware status
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        currentStatus = "[Ethernet]: Hardware not found";
        statusColor = RED;
    }
    // Check cable status
    else if (Ethernet.linkStatus() == LinkOFF) {
        currentStatus = "[Ethernet]: Cable disconnected";
        statusColor = gfx->color565(255, 165, 0);  // Orange color
    }
    // Check IP assignment
    else {
        IPAddress localIP = Ethernet.localIP();
        if (localIP == IPAddress(0, 0, 0, 0)) {
            currentStatus = "[Ethernet]: Cable connected, no IP";
            statusColor = YELLOW;
        } else {
            currentStatus = "[Ethernet]: Connected - " + localIP.toString();
            statusColor = gfx->color565(0, 100, 0);  // Dark green color
        }
    }
    
    // Only update display if status changed
    if (currentStatus != prevStatus) {
        // Overwrite previous text in black if there was a previous status
        if (prevStatus != "") {
            gfx->setTextSize(2);
            gfx->setTextColor(BLACK);
            gfx->setCursor(x, y);
            gfx->print(prevStatus);
        }
        
        // Display new status
        gfx->setTextSize(2);
        gfx->setTextColor(statusColor);
        gfx->setCursor(x, y);
        gfx->print(currentStatus);
        
        prevStatus = currentStatus;
    }
}

// Function to display current time at bottom of screen, updating every second
void displayCurrentTime(int x, int y, uint16_t textColor, uint16_t bgColor) {
    static unsigned long lastTimeUpdate = 0;
    static String prevTimeStr = "";
    static String cachedTzAbbrev = "";  // Cache timezone abbreviation
    static bool tzAbbrevInitialized = false;
    const unsigned long TIME_UPDATE_INTERVAL = 1000; // 1 second
    
    // Only update display every second
    unsigned long currentMillis = millis();
    if (currentMillis - lastTimeUpdate < TIME_UPDATE_INTERVAL) {
        return;
    }
    
    lastTimeUpdate = currentMillis;
    
    // Get current time (use local time with DST)
    time_t now = time(nullptr);
    String currentTimeStr = "";
    
    if (now > 0) {
        struct tm * timeinfo = localtime(&now);  // Use localtime for DST-aware display
        
        // Get timezone abbreviation - retry periodically if we got default fallback values
        if (!tzAbbrevInitialized || (cachedTzAbbrev == "UTC" && detectedTimezoneAbbreviation.length() == 0)) {
            static unsigned long lastTimezoneRetry = 0;
            const unsigned long TIMEZONE_RETRY_INTERVAL = 300000; // 5 minutes
            
            // If we have fallback timezone and enough time has passed, retry timezone detection
            if ((cachedTzAbbrev == "UTC" && detectedTimezoneAbbreviation.length() == 0) && 
                (currentMillis - lastTimezoneRetry >= TIMEZONE_RETRY_INTERVAL)) {
                lastTimezoneRetry = currentMillis;
                Serial.printf("Retrying timezone detection due to fallback timezone '%s'...\n", cachedTzAbbrev.c_str());
                
                // Only attempt retry if we have stable internet connection and we're not already in a timezone configuration
                static bool timezoneRetryInProgress = false;
                if (getEthernetStatus() && getInternetStatus() && !timezoneRetryInProgress) {
                    timezoneRetryInProgress = true;  // Prevent concurrent retries
                    
                    // Yield to other tasks before starting network operation
                    vTaskDelay(pdMS_TO_TICKS(10));
                    
                    String newDetectedTz = getTimezoneFromLocation();
                    if (newDetectedTz != "UTC0" && detectedTimezoneName.length() > 0) {
                        // Successfully detected a new timezone
                        Serial.printf("Timezone retry successful: %s\n", detectedTimezoneName.c_str());
                        
                        // Reconfigure the system timezone
                        setenv("TZ", newDetectedTz.c_str(), 1);
                        tzset();
                        
                        // Update cached abbreviation from API response (if available)
                        if (detectedTimezoneAbbreviation.length() > 0) {
                            cachedTzAbbrev = detectedTimezoneAbbreviation;
                            Serial.printf("Updated timezone abbreviation from API: %s\n", cachedTzAbbrev.c_str());
                        } else {
                            // Fallback to simple region-based abbreviation
                            cachedTzAbbrev = "UTC";
                            if (detectedTimezoneName.indexOf("America/") >= 0) cachedTzAbbrev = "AMT";
                            else if (detectedTimezoneName.indexOf("Europe/") >= 0) cachedTzAbbrev = "CET";
                            else if (detectedTimezoneName.indexOf("Asia/") >= 0) cachedTzAbbrev = "AST";
                            else if (detectedTimezoneName.indexOf("Australia/") >= 0) cachedTzAbbrev = "AEST";
                            else if (detectedTimezoneName.indexOf("Africa/") >= 0) cachedTzAbbrev = "CAT";
                            else if (detectedTimezoneName.indexOf("Pacific/") >= 0) cachedTzAbbrev = "PST";
                            Serial.printf("Updated timezone abbreviation from region fallback: %s\n", cachedTzAbbrev.c_str());
                        }
                    } else {
                        Serial.println("Timezone retry failed, will try again in 5 minutes");
                    }
                    
                    timezoneRetryInProgress = false;  // Allow future retries
                    
                    // Yield to other tasks after network operation
                    vTaskDelay(pdMS_TO_TICKS(10));
                } else if (timezoneRetryInProgress) {
                    Serial.println("Timezone retry already in progress, skipping");
                } else {
                    Serial.println("No stable internet connection for timezone retry");
                }
            }
            
            // Initial timezone abbreviation setup (first call)
            if (!tzAbbrevInitialized) {
                cachedTzAbbrev = "UTC";  // Default
                
                // Use the detected timezone abbreviation from API first, fallback to database lookup
                if (detectedTimezoneAbbreviation.length() > 0) {
                    cachedTzAbbrev = detectedTimezoneAbbreviation;
                    Serial.printf("Using timezone abbreviation from API: %s\n", cachedTzAbbrev.c_str());
                } else if (detectedTimezoneName.length() > 0) {
                    // Fallback to simple region-based abbreviation
                    cachedTzAbbrev = "UTC";
                    if (detectedTimezoneName.indexOf("America/") >= 0) cachedTzAbbrev = "AMT";
                    else if (detectedTimezoneName.indexOf("Europe/") >= 0) cachedTzAbbrev = "CET";
                    else if (detectedTimezoneName.indexOf("Asia/") >= 0) cachedTzAbbrev = "AST";
                    else if (detectedTimezoneName.indexOf("Australia/") >= 0) cachedTzAbbrev = "AEST";
                    else if (detectedTimezoneName.indexOf("Africa/") >= 0) cachedTzAbbrev = "CAT";
                    else if (detectedTimezoneName.indexOf("Pacific/") >= 0) cachedTzAbbrev = "PST";
                    Serial.printf("Using region-based abbreviation fallback: %s -> %s\n", 
                                 detectedTimezoneName.c_str(), cachedTzAbbrev.c_str());
                } else {
                    // Fallback: try to extract from TZ environment variable
                    char* tzEnv = getenv("TZ");
                    if (tzEnv != nullptr) {
                        String tzString = String(tzEnv);
                        // Extract first 2-3 characters before any numbers or special characters
                        int i = 0;
                        while (i < tzString.length() && isalpha(tzString[i]) && i < 4) {
                            i++;
                        }
                        if (i > 0) {
                            cachedTzAbbrev = tzString.substring(0, i);
                        }
                    }
                    Serial.printf("Using fallback TZ parsing for abbreviation: %s\n", cachedTzAbbrev.c_str());
                }
                
                tzAbbrevInitialized = true;
                Serial.printf("Timezone abbreviation cached: %s\n", cachedTzAbbrev.c_str());
            }
        }
        
        char timeBuffer[40];  // Increased buffer size
        snprintf(timeBuffer, sizeof(timeBuffer), "%02d/%02d/%04d %02d:%02d:%02d %s",
            timeinfo->tm_mday,
            timeinfo->tm_mon + 1,
            timeinfo->tm_year + 1900,
            timeinfo->tm_hour,
            timeinfo->tm_min,
            timeinfo->tm_sec,
            cachedTzAbbrev.c_str());  // Use cached timezone abbreviation
        currentTimeStr = String(timeBuffer);
    } else {
        currentTimeStr = "Time not synchronized";
    }
    
    // Only update display if time changed
    if (currentTimeStr != prevTimeStr) {
        gfx->setTextSize(2);
        
        // Calculate character width for positioning
        const int charWidth = 12;  // Approximate width of each character at text size 2
        
        // Compare strings character by character and only update changed characters
        int maxLen = max(currentTimeStr.length(), prevTimeStr.length());
        for (int i = 0; i < maxLen; i++) {
            char currentChar = (i < currentTimeStr.length()) ? currentTimeStr[i] : ' ';
            char prevChar = (i < prevTimeStr.length()) ? prevTimeStr[i] : ' ';
            
            if (currentChar != prevChar) {
                // Calculate position for this character
                int charX = x + (i * charWidth);
                
                // Erase the old character by drawing it in black
                if (prevChar != ' ' && prevTimeStr.length() > 0) {
                    gfx->setTextColor(BLACK);
                    gfx->setCursor(charX, y);
                    gfx->print(prevChar);
                }
                
                // Draw the new character in the correct color
                if (currentChar != ' ') {
                    gfx->setTextColor(textColor);
                    gfx->setCursor(charX, y);
                    gfx->print(currentChar);
                }
            }
        }
        
        prevTimeStr = currentTimeStr;
    }
}


// ===== Enhanced Connectivity Monitoring with Interrupts and Tasks =====

// Global variables for connectivity status
volatile bool ethernetLinkChanged = false;
volatile bool ethernetConnected = false;
volatile bool internetConnected = false;
TaskHandle_t connectivityTaskHandle = NULL;

// Ethernet configuration flag
bool ethernetPresent = true;  // Default to true for backwards compatibility

// Interrupt Service Routine for W5500 Ethernet chip
void IRAM_ATTR ethernetInterruptHandler() {
    ethernetLinkChanged = true;
    // Note: Can't use Serial.println in ISR, but we can set a flag
    // The task will detect this flag and print a message
}

// FreeRTOS task for connectivity monitoring
void connectivityMonitorTask(void* parameter) {
    const TickType_t checkInterval = pdMS_TO_TICKS(5000); // 5 seconds for faster response
    const TickType_t internetInterval = pdMS_TO_TICKS(60000); // 60 seconds for internet check
    TickType_t lastInternetCheck = 0;
    
    Serial.println("=== Connectivity monitoring task started ===");
    
    // Print initial status with timestamp
    time_t now = time(nullptr);
    if (now > 0) {
        struct tm * timeinfo = localtime(&now);
        Serial.printf("[%02d:%02d:%02d] Initial connectivity status check...\n",
                     timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    } else {
        Serial.println("Initial connectivity status check...");
    }
    bool initialEthernetStatus = false;
    if (Ethernet.hardwareStatus() != EthernetNoHardware) {
        Serial.printf("Ethernet hardware: %s\n", 
                     (Ethernet.hardwareStatus() == EthernetW5500) ? "W5500 detected" : "Other hardware");
        Serial.printf("Link status: %s\n", 
                     (Ethernet.linkStatus() == LinkON) ? "Link UP" : "Link DOWN");
        Serial.printf("Local IP: %s\n", Ethernet.localIP().toString().c_str());
        
        if (Ethernet.linkStatus() == LinkON && Ethernet.localIP() != IPAddress(0, 0, 0, 0)) {
            initialEthernetStatus = true;
        }
    } else {
        Serial.println("Ethernet hardware: NOT DETECTED");
    }
    
    ethernetConnected = initialEthernetStatus;
    Serial.printf("Initial Ethernet status: %s\n", ethernetConnected ? "CONNECTED" : "DISCONNECTED");
    
    while (true) {
        bool statusChanged = false;
        
        // Check if interrupt was triggered (link status changed)
        if (ethernetLinkChanged) {
            ethernetLinkChanged = false;
            Serial.println("*** INTERRUPT TRIGGERED - Ethernet link status changed ***");
            statusChanged = true;
        }
        
        // Check Ethernet connection status
        bool newEthernetStatus = false;
        if (Ethernet.hardwareStatus() != EthernetNoHardware && 
            Ethernet.linkStatus() == LinkON && 
            Ethernet.localIP() != IPAddress(0, 0, 0, 0)) {
            newEthernetStatus = true;
        }
        
        // Log detailed status every 5th check (every 25 seconds)
        static int checkCount = 0;
        checkCount++;
        if (checkCount >= 5) {
            checkCount = 0;
            time_t now = time(nullptr);
            if (now > 0) {
                struct tm * timeinfo = localtime(&now);
                Serial.printf("[%02d:%02d:%02d] --- Periodic connectivity status ---\n",
                             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
            } else {
                Serial.println("--- Periodic connectivity status ---");
            }
            Serial.printf("Hardware: %s, Link: %s, IP: %s\n",
                         (Ethernet.hardwareStatus() == EthernetW5500) ? "W5500" : "None",
                         (Ethernet.linkStatus() == LinkON) ? "UP" : "DOWN",
                         Ethernet.localIP().toString().c_str());
            Serial.printf("Gateway: %s\n", Ethernet.gatewayIP().toString().c_str());
            Serial.printf("Subnet: %s\n", Ethernet.subnetMask().toString().c_str());
            Serial.printf("DNS: %s\n", Ethernet.dnsServerIP().toString().c_str());
        }
        
        if (newEthernetStatus != ethernetConnected) {
            ethernetConnected = newEthernetStatus;
            statusChanged = true;
            Serial.printf("*** ETHERNET STATUS CHANGED: %s ***\n", 
                         ethernetConnected ? "CONNECTED" : "DISCONNECTED");
            
            if (ethernetConnected) {
                Serial.printf("Ethernet reconnected - IP: %s\n", Ethernet.localIP().toString().c_str());
            } else {
                Serial.println("Ethernet disconnected - checking hardware/link status...");
                Serial.printf("Hardware status: %s\n", 
                             (Ethernet.hardwareStatus() == EthernetNoHardware) ? "No hardware" : "Hardware OK");
                Serial.printf("Link status: %s\n", 
                             (Ethernet.linkStatus() == LinkON) ? "Link OK" : "Link DOWN");
            }
        }
        
        // Check internet connectivity (every 60 seconds = 12 task cycles)
        TickType_t currentTime = xTaskGetTickCount();
        if (currentTime - lastInternetCheck >= internetInterval) {
            lastInternetCheck = currentTime;
            time_t now = time(nullptr);
            if (now > 0) {
                struct tm * timeinfo = localtime(&now);
                Serial.printf("[%02d:%02d:%02d] Checking internet connectivity...\n",
                             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
            } else {
                Serial.println("Checking internet connectivity...");
            }
            
            if (ethernetConnected) {
                // Test internet connectivity
                EthernetClient client;
                bool newInternetStatus = false;
                Serial.printf("Testing connection to %s:%d...\n", internetTestHost, internetTestPort);
                if (client.connect(internetTestHost, internetTestPort)) {
                    newInternetStatus = true;
                    Serial.println("Internet connectivity test: SUCCESS");
                    client.stop();
                } else {
                    Serial.println("Internet connectivity test: FAILED");
                }
                
                if (newInternetStatus != internetConnected) {
                    internetConnected = newInternetStatus;
                    statusChanged = true;
                    Serial.printf("*** INTERNET STATUS CHANGED: %s ***\n", 
                                 internetConnected ? "CONNECTED" : "DISCONNECTED");
                }
            } else if (internetConnected) {
                // Ethernet is down, so internet must be down too
                internetConnected = false;
                statusChanged = true;
                Serial.println("*** INTERNET STATUS CHANGED: DISCONNECTED (no Ethernet) ***");
            }
        }
        
        // If status changed, you could trigger other actions here
        if (statusChanged) {
            time_t now = time(nullptr);
            if (now > 0) {
                struct tm * timeinfo = localtime(&now);
                Serial.printf("[%02d:%02d:%02d] Current connectivity status - Ethernet: %s, Internet: %s\n",
                             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
                             ethernetConnected ? "UP" : "DOWN",
                             internetConnected ? "UP" : "DOWN");
            } else {
                Serial.printf("Current connectivity status - Ethernet: %s, Internet: %s\n",
                             ethernetConnected ? "UP" : "DOWN",
                             internetConnected ? "UP" : "DOWN");
            }
        }
        
        // Wait for next check or notification
        vTaskDelay(checkInterval);
    }
}


// Initialize Ethernet interrupt monitoring
void initEthernetInterrupt() {
    Serial.println("Initializing Ethernet interrupt monitoring...");
    
    // Configure W5500 interrupt pin
    pinMode(W5500_INT_PIN, INPUT_PULLUP);
    
    // Attach interrupt handler
    attachInterrupt(digitalPinToInterrupt(W5500_INT_PIN), ethernetInterruptHandler, FALLING);
    
    Serial.printf("Ethernet interrupt attached to pin %d\n", W5500_INT_PIN);
}


// Start the connectivity monitoring task   
void startConnectivityTask() {
    if (connectivityTaskHandle == NULL) {
        Serial.println("Starting connectivity monitoring task...");
        Serial.printf("Free heap before task creation: %d bytes\n", ESP.getFreeHeap());
        
        BaseType_t result = xTaskCreatePinnedToCore(
            connectivityMonitorTask,    // Task function
            "ConnectivityMonitor",      // Task name
            8192,                       // Stack size (bytes) - increased for safety
            NULL,                       // Task parameter
            2,                          // Task priority (0-25, higher = more priority)
            &connectivityTaskHandle,    // Task handle
            1                          // Core to run on (0 or 1) - use core 1
        );
        
        if (result == pdPASS && connectivityTaskHandle != NULL) {
            Serial.println("*** Connectivity monitoring task created successfully ***");
            Serial.printf("Task handle: 0x%x\n", (uint32_t)connectivityTaskHandle);
            delay(100); // Give task time to start and print initial message
        } else {
            Serial.println("*** FAILED to create connectivity monitoring task ***");
            Serial.printf("xTaskCreatePinnedToCore returned: %d\n", result);
            Serial.printf("Free heap after failed creation: %d bytes\n", ESP.getFreeHeap());
        }
    } else {
        Serial.println("Connectivity monitoring task already running");
        Serial.printf("Existing task handle: 0x%x\n", (uint32_t)connectivityTaskHandle);
    }
}

// Stop the connectivity monitoring task
void stopConnectivityTask() {
    if (connectivityTaskHandle != NULL) {
        Serial.println("Stopping connectivity monitoring task...");
        vTaskDelete(connectivityTaskHandle);
        connectivityTaskHandle = NULL;
        Serial.println("Connectivity monitoring task stopped");
    }
}

// Global variables for background ethernet initialization
TaskHandle_t ethernetInitTaskHandle = NULL;
volatile bool ethernetInitInProgress = false;
volatile bool ethernetInitCompleted = false;



// Background ethernet initialization task
void ethernetInitTask(void* parameter) {
    Serial.println("=== Background Ethernet Initialization Started ===");
    ethernetInitInProgress = true;
    
    // Proper Ethernet initialization sequence
    Serial.println("Initializing Ethernet hardware...");
    
    // Network configuration (same as Battery_Emulator.ino)
    uint8_t mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
    IPAddress ip(192, 168, 1, 243);
    IPAddress dns(192, 168, 1, 1);
    IPAddress gateway(192, 168, 1, 1);
    IPAddress subnet(255, 255, 255, 0);
    
    bool success = false;
    bool static_ip_set = true;
    // First try DHCP with timeout

if (static_ip_set) {
        Serial.println("Trying static IP...");
        // Fallback to static IP configuration
        Ethernet.begin(mac, ip, dns, gateway, subnet);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Give it time to initialize
        
        // Check if static IP was assigned correctly
        if (Ethernet.localIP() != IPAddress(0, 0, 0, 0)) {
            Serial.printf("Static IP successful - IP: %s\n", Ethernet.localIP().toString().c_str());
            Serial.printf("DNS: %s\n", dns.toString().c_str());
            Serial.printf("Gateway: %s\n", gateway.toString().c_str());
            Serial.printf("Subnet: %s\n", subnet.toString().c_str());
            success = true;
        } else {
            Serial.println("Static IP assignment failed");
        }
    }
else {
        Serial.println("Trying DHCP initialization...");
        (Ethernet.begin(mac, 10000, 10000) == 1);
        Serial.printf("DHCP successful - IP: %s\n", Ethernet.localIP().toString().c_str());
        success = true;
    } 

    
    // Additional verification
    if (success) {
        // Verify hardware status
        if (Ethernet.hardwareStatus() == EthernetNoHardware) {
            Serial.println("WARNING: Hardware check shows no ethernet hardware found!");
            success = false;
        } else {
            Serial.printf("Ethernet hardware detected: %s\n", 
                         (Ethernet.hardwareStatus() == EthernetW5500) ? "W5500" : "Other");
        }
        
        // Verify link status
        if (Ethernet.linkStatus() == LinkOFF) {
            Serial.println("WARNING: Ethernet cable not connected");
            success = false;
        } else {
            Serial.println("Ethernet link is UP");
        }
    }
    
    if (success) {
        Serial.println("*** Background Ethernet Initialization SUCCESS ***");
        Serial.printf("Final Ethernet status - IP: %s, Hardware: %s, Link: %s\n",
                     Ethernet.localIP().toString().c_str(),
                     (Ethernet.hardwareStatus() == EthernetW5500) ? "W5500" : "Other",
                     (Ethernet.linkStatus() == LinkON) ? "UP" : "DOWN");
        
        startConnectivityTask();  // Start the connectivity monitoring task
        getNTPTime();  // Sync time over ethernet
    } else {
        Serial.println("*** Background Ethernet Initialization FAILED ***");
        Serial.println("Troubleshooting info:");
        Serial.printf("Hardware Status: %d (0=NoHW, 1=W5100, 2=W5200, 3=W5500)\n", Ethernet.hardwareStatus());
        Serial.printf("Link Status: %d (0=Unknown, 1=ON, 2=OFF)\n", Ethernet.linkStatus());
        Serial.printf("Local IP: %s\n", Ethernet.localIP().toString().c_str());
    }
    
    ethernetInitInProgress = false;
    ethernetInitCompleted = true;
    ethernetInitTaskHandle = NULL;  // Task will delete itself
    
    Serial.println("=== Background Ethernet Initialization Complete ===");
    vTaskDelete(NULL);  // Delete this task
}


// Start background ethernet initialization (non-blocking)
void startBackgroundEthernetInit() {
    if (ethernetInitTaskHandle != NULL || ethernetInitInProgress) {
        Serial.println("Ethernet initialization already in progress");
        return;
    }
    
    Serial.println("Starting background ethernet initialization...");
    
    BaseType_t result = xTaskCreatePinnedToCore(
        ethernetInitTask,           // Task function
        "EthernetInit",            // Task name
        8192,                      // Stack size (bytes)
        NULL,                      // Task parameter
        1,                         // Task priority (lower than connectivity task)
        &ethernetInitTaskHandle,   // Task handle
        0                          // Core to run on (use core 0, opposite of connectivity task)
    );
    
    if (result != pdPASS) {
        Serial.println("Failed to create background ethernet initialization task");
        ethernetInitTaskHandle = NULL;
    }
}

// ===== NTP Time Management Task =====

// Global variables for NTP task
TaskHandle_t ntpTaskHandle = NULL;

// NTP Time Management Task
void ntpTimeTask(void* parameter) {
    const TickType_t ntpCheckInterval = pdMS_TO_TICKS(30000); // Check every 30 seconds
    const TickType_t displayUpdateInterval = pdMS_TO_TICKS(1000); // Update display every second
    TickType_t lastNtpCheck = 0;
    TickType_t lastDisplayUpdate = 0;
    
    Serial.println("=== NTP Time Management Task Started ===");
    
    while (true) {
        TickType_t currentTime = xTaskGetTickCount();
        
        // Check for NTP time sync every 30 seconds
        if (currentTime - lastNtpCheck >= ntpCheckInterval) {
            lastNtpCheck = currentTime;
            
            // Only attempt NTP sync if we have network connectivity
            if (getEthernetStatus() && getInternetStatus()) {
                time_t now = time(nullptr);
                if (now > 0) {
                    struct tm * timeinfo = localtime(&now);
                    Serial.printf("[%02d:%02d:%02d] Attempting NTP time sync...\n",
                                 timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
                } else {
                    Serial.println("Attempting NTP time sync...");
                }
                
                getNTPTime(); // This function already handles the actual sync logic
            } else {
                Serial.println("Network connectivity unavailable - skipping NTP sync");
            }
        }
        
        // Update time display every second
        if (currentTime - lastDisplayUpdate >= displayUpdateInterval) {
            lastDisplayUpdate = currentTime;
            
            // Update display functions based on flags (these are already designed to be non-blocking)
            if (displayEthernetStatusEnabled) {
                displayEthernetConnectionStatus(10, 10, WHITE, BLACK);
            }
            if (displayTimeEnabled) {
//                displayCurrentTime(10, SCREEN_HEIGHT - 40, WHITE, BLACK);  // Fixed Y coordinate
                displayCurrentTime(10, SCREEN_WIDTH - 20, WHITE, BLACK);  // Fixed Y coordinate
            }
        }
        
        // Small delay to prevent task from consuming too much CPU
        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms for responsive display updates
    }
}

// Start NTP time management task
bool startNtpTimeTask() {
    if (ntpTaskHandle != NULL) {
        Serial.println("NTP time management task already running");
        return true;
    }
    
    Serial.println("Starting NTP time management task...");
    Serial.printf("Free heap before NTP task creation: %d bytes\n", ESP.getFreeHeap());
    
    BaseType_t ntpTaskResult = xTaskCreatePinnedToCore(
        ntpTimeTask,               // Task function
        "NTPTimeManager",          // Task name
        4096,                      // Stack size (bytes)
        NULL,                      // Task parameter
        1,                         // Task priority (lower than connectivity monitor)
        &ntpTaskHandle,            // Task handle
        0                          // Core to run on (use core 0, connectivity uses core 1)
    );
    
    if (ntpTaskResult == pdPASS && ntpTaskHandle != NULL) {
        Serial.println("*** NTP time management task created successfully ***");
        Serial.printf("Task handle: 0x%x\n", (uint32_t)ntpTaskHandle);
        delay(100); // Give task time to start
        return true;
    } else {
        Serial.println("*** FAILED to create NTP time management task ***");
        Serial.printf("xTaskCreatePinnedToCore returned: %d\n", ntpTaskResult);
        Serial.printf("Free heap after failed creation: %d bytes\n", ESP.getFreeHeap());
        return false;
    }
}

// Stop NTP time management task
void stopNtpTimeTask() {
    if (ntpTaskHandle != NULL) {
        Serial.println("Stopping NTP time management task...");
        vTaskDelete(ntpTaskHandle);
        ntpTaskHandle = NULL;
        Serial.println("NTP time management task stopped");
    }
}

// Get timezone from IP geolocation
String getTimezoneFromLocation() {
    if (!getEthernetStatus()) {
        Serial.println("No internet connection for timezone detection");
        return "UTC0"; // Default fallback
    }
    
    // Alternate between timezone services - switch to other service if last call failed
    static bool useWorldTimeApi = true;
    const char* hosts[] = {"worldtimeapi.org", "timeapi.world"};
    const char* paths[] = {"/api/ip", "/api/ip"};
    
    // Select service based on current flag
    const char* host = hosts[useWorldTimeApi ? 0 : 1];
    const char* path = paths[useWorldTimeApi ? 0 : 1];
    
    EthernetClient client;
    Serial.printf("Trying timezone service: %s\n", host);
    
    if (!client.connect(host, 80)) {
        Serial.printf("Failed to connect to %s\n", host);
        useWorldTimeApi = !useWorldTimeApi; // Switch to other service for next time
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
    
    // Read response
    String response = "";
    bool headersPassed = false;
    unsigned long timeout = millis() + 10000; // 10 second timeout
    
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
        Serial.printf("No response from %s\n", host);
        useWorldTimeApi = !useWorldTimeApi; // Switch to other service for next time
        return "UTC0";
    }
    
    Serial.printf("Successful response from %s:\n", host);
    Serial.println(response);
    
    // Parse JSON response to extract timezone
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
        Serial.printf("JSON parsing failed for %s: %s\n", host, error.c_str());
        useWorldTimeApi = !useWorldTimeApi; // Switch to other service for next time
        return "UTC0";
    }
    
    // Success! Keep using the same service next time (don't flip the flag)
    
    // Extract timezone info from successful response
    String timezone_name = doc["timezone"].as<String>();
    String abbreviation = doc["abbreviation"].as<String>();
    int utc_offset = doc["utc_offset"].as<int>(); // Offset in seconds
    bool dst = doc["dst"].as<bool>();
    
    // Store the detected timezone info globally
    detectedTimezoneName = timezone_name;
    detectedTimezoneAbbreviation = abbreviation;  // Store abbreviation from API response
    
    Serial.printf("Detected location: %s\n", timezone_name.c_str());
    Serial.printf("UTC Offset: %d seconds (%d hours)\n", utc_offset, utc_offset/3600);
    Serial.printf("DST Active: %s\n", dst ? "Yes" : "No");
    Serial.printf("Abbreviation from API: %s\n", abbreviation.c_str());
    
    // Generate simple POSIX timezone string directly from API data
    int offsetHours = utc_offset / 3600;
    int offsetMinutes = abs(utc_offset % 3600) / 60;
    
    String posixTz;
    if (abbreviation.length() > 0) {
        // Use API-provided abbreviation
        posixTz = abbreviation;
    } else {
        // Fallback to generic abbreviation
        posixTz = "UTC";
    }
    
    // Add offset (POSIX format uses opposite sign)
    if (utc_offset == 0) {
        posixTz += "0";
    } else {
        int posixOffsetHours = -offsetHours;  // POSIX uses opposite sign
        int posixOffsetMinutes = offsetMinutes;
        
        posixTz += String(posixOffsetHours);
        if (posixOffsetMinutes > 0) {
            posixTz += ":" + String(posixOffsetMinutes);
        }
    }
    
    Serial.printf("Generated POSIX timezone: %s\n", posixTz.c_str());
    return posixTz;
}



// Configure timezone with automatic detection and DST handling
void configureTimezone() {
    Serial.println("=== Automatic Timezone Configuration ===");
    
    // Try to get timezone from internet
    String detectedTz = getTimezoneFromLocation();
    const char* timezone = detectedTz.c_str();
    
    // Store the detected timezone name for abbreviation lookup
    // This should be called from getTimezoneFromLocation to store the actual timezone name
    // For now, we'll try to extract it from the POSIX string, but ideally this should be set
    // in getTimezoneFromLocation when we get the timezone_name from the API
    
    // Fallback to manual configuration if detection failed
    if (detectedTz == "UTC0") {
        Serial.println("Automatic detection failed, using manual configuration...");
        
        // Manual timezone configuration (uncomment your timezone):
        timezone = "UTC0";  // Default
        detectedTimezoneName = "UTC";  // Set fallback timezone name for abbreviation

    }
    
    Serial.printf("Configuring timezone: %s\n", timezone);
    setenv("TZ", timezone, 1);
    tzset();
    Serial.println("=== Timezone Configuration Complete ===\n");
}


void getNTPTime() {
    static unsigned long lastNTPSync = 0;
    static bool timeInitialized = false;
    static bool timezoneConfigured = false;
    const unsigned long NTP_SYNC_INTERVAL = 30 * 60 * 1000; // 30 minutes in milliseconds
    
    // Configure timezone once on first call
    if (!timezoneConfigured) {
        configureTimezone();
        timezoneConfigured = true;
    }
    
    // If time is already initialized, only sync every 30 minutes
    if (timeInitialized && (millis() - lastNTPSync < NTP_SYNC_INTERVAL)) {
        // Time was already synced recently, no need to sync again
        Serial.println("NTP sync not needed - within 30-minute interval");
        return;
    }
    
    Serial.println("Getting NTP time via Ethernet...");
    
    // Check if Ethernet is connected
    if (Ethernet.linkStatus() != LinkON) {
        Serial.println("Ethernet not connected - cannot get NTP time");
        return;
    }
    
    // Initialize UDP if not already done
    static bool udpInitialized = false;
    if (!udpInitialized) {
        Udp.begin(localPort);
        udpInitialized = true;
        Serial.println("UDP initialized for NTP");
    }
    
    // Try primary NTP server first, then backup
    const char* serversToTry[] = {ntpServer, ntpServerBackup};
    bool ntpSuccess = false;
    
    for (int serverIndex = 0; serverIndex < 2 && !ntpSuccess; serverIndex++) {
        const char* currentServer = serversToTry[serverIndex];
        Serial.printf("Trying NTP server: %s\n", currentServer);
        
        // Send NTP request
        memset(packetBuffer, 0, 48);
        packetBuffer[0] = 0b11100011; // LI, Version, Mode
        packetBuffer[1] = 0;          // Stratum
        packetBuffer[2] = 6;          // Polling Interval
        packetBuffer[3] = 0xEC;       // Peer Clock Precision
        packetBuffer[12] = 49;        // Reference ID
        packetBuffer[13] = 0x4E;
        packetBuffer[14] = 49;
        packetBuffer[15] = 52;

        Udp.beginPacket(currentServer, ntpPort);
        Udp.write(packetBuffer, 48);
        if (Udp.endPacket() == 0) {
            Serial.printf("Error sending NTP packet to %s\n", currentServer);
            continue;  // Try next server
        }
        
        Serial.printf("NTP packet sent to %s, waiting for response...\n", currentServer);
        
        // Wait for response with timeout
        unsigned long startTime = millis();
        int packetSize = 0;
        while ((millis() - startTime < 5000) && (packetSize = Udp.parsePacket()) == 0) {
            delay(10);
        }
        
        if (packetSize >= 48) {
            ntpSuccess = true;
            Serial.printf("NTP response received from %s\n", currentServer);
        Udp.read(packetBuffer, 48);
        
        // Extract timestamp from NTP packet
        unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
        unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
        unsigned long secsSince1900 = (highWord << 16) | lowWord;
        unsigned long epoch = secsSince1900 - 2208988800UL; // Convert NTP to Unix time
        
        // Set the system time (this will update the ESP32's internal RTC)
        struct timeval tv;
        tv.tv_sec = epoch;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        
        // Convert to local time (with DST if configured)
        time_t rawTime = epoch;
        struct tm * timeinfo_utc = gmtime(&rawTime);
        struct tm * timeinfo_local = localtime(&rawTime);
        
        Serial.printf("NTP Time synchronized from %s (UTC): %04d-%02d-%02d %02d:%02d:%02d\n",
            currentServer,
            timeinfo_utc->tm_year + 1900,
            timeinfo_utc->tm_mon + 1,
            timeinfo_utc->tm_mday,
            timeinfo_utc->tm_hour,
            timeinfo_utc->tm_min,
            timeinfo_utc->tm_sec);
            
        Serial.printf("Local Time (with DST): %04d-%02d-%02d %02d:%02d:%02d %s\n",
            timeinfo_local->tm_year + 1900,
            timeinfo_local->tm_mon + 1,
            timeinfo_local->tm_mday,
            timeinfo_local->tm_hour,
            timeinfo_local->tm_min,
            timeinfo_local->tm_sec,
            timeinfo_local->tm_isdst ? "(DST)" : "(Standard)");
            
        Serial.printf("Unix timestamp: %lu\n", epoch);
        
        // Show current system time in both UTC and local
        time_t now = time(nullptr);
        struct tm * current_utc = gmtime(&now);
        struct tm * current_local = localtime(&now);
        
        Serial.printf("Current System Time (UTC): %s", asctime(current_utc));
        Serial.printf("Current System Time (Local): %s", asctime(current_local));
        Serial.printf("DST Status: %s\n", current_local->tm_isdst ? "Active" : "Inactive");
        
            // Mark that time has been successfully initialized and update sync time
            timeInitialized = true;
            lastNTPSync = millis();
            Serial.printf("NTP sync completed from %s - next sync in 30 minutes\n", currentServer);
            
        } else {
            Serial.printf("No NTP response received from %s (packet size: %d)\n", currentServer, packetSize);
        }
    }
    
    if (!ntpSuccess) {
        Serial.println("All NTP servers failed - time synchronization unsuccessful");
    }
}



// Better startup sequence: Screen first, then networking, then fade-in
// Smart delay function - uses appropriate delay based on context
void smartDelay(uint32_t ms, bool ethernetPresent) {
    if (ethernetPresent && xTaskGetCurrentTaskHandle() != NULL) {
        vTaskDelay(pdMS_TO_TICKS(ms));  // Use FreeRTOS delay if in task context
    } else {
        delay(ms);  // Use Arduino delay otherwise
    }
}

void startupSequenceWithEthernet(bool ethernetPresent) {
    Serial.printf("=== Better Startup Sequence (Ethernet: %s) ===\n", ethernetPresent ? "ENABLED" : "DISABLED");
    
    // STEP 1: Ensure backlight is completely off and screen is ready
    Serial.println("STEP 1: Preparing screen and backlight...");
    ledcWrite(1, 0);  // Force PWM to 0 first
    smartDelay(200, ethernetPresent);  // Give hardware more time to respond and settle
    // Force internal tracking to 0 instead of reading potentially unsettled PWM value
    currentBacklightBrightness = 0;  // Force internal tracking to 0
    Serial.println("Backlight forced to 0 and tracking initialized to 0");
    
    // STEP 2: Display splash screen content while screen is dark
    Serial.println("STEP 2: Displaying splash screen content...");
    displaySplashScreenContent();  // Display splash content cleanly while screen is dark
    
    // STEP 2.5: Verify backlight is still at 0 after content display
    ledcWrite(1, 0);  // Ensure PWM is still at 0 
    currentBacklightBrightness = 0;  // Ensure tracking is still at 0
    Serial.printf("Verified backlight still at 0 after content display. PWM: %d, Tracking: %d\n", ledcRead(1), currentBacklightBrightness);
    smartDelay(200, ethernetPresent);  // Ensure content is fully rendered and backlight settled
    
    if (ethernetPresent) {
        // STEP 3: Start networking tasks after screen is ready
        Serial.println("STEP 3: Starting networking tasks...");
        initEthernet();  // Initialize ethernet hardware
        initEthernetInterrupt();  // Setup interrupt monitoring
        startConnectivityTask();  // Start background ethernet monitoring
        startBackgroundEthernetInit();  // Start background ethernet initialization with IP configuration
        
        // Give networking tasks a moment to start
        smartDelay(100, ethernetPresent);
    } else {
        Serial.println("STEP 3: Skipping networking tasks (Ethernet disabled)");
    }
    
    // STEP 4: Now fade in the splash screen
    Serial.println("STEP 4: Fading in splash screen...");
    fadeBacklight(Backlight_on, 2000, ethernetPresent);  // Fade in over 2 seconds
    
    // Give a moment to appreciate the fully faded-in splash screen

    smartDelay(500, ethernetPresent);  // Wait 500ms with full brightness before monitoring

    if (ethernetPresent) {
        // STEP 5: Now show ethernet status monitoring for up to 15 seconds
        Serial.println("STEP 5: Monitoring ethernet connection with status display (max 15 seconds)...");
        uint32_t monitorStartTime = millis();
        uint32_t maxMonitorTime = 15000;  // 15 seconds maximum
        
        while ((millis() - monitorStartTime < maxMonitorTime)) {
            // Check if ethernet is connected (don't wait for internet during startup)
            if (getEthernetStatus()) {
                Serial.printf("*** Ethernet connected after %u ms! ***\n", (uint32_t)(millis() - monitorStartTime));
                break;
            }
            
            // Show ethernet status indicator in corner while waiting
            drawEthernetStatusIndicator();
            
            smartDelay(100, ethernetPresent);  // Check every 100ms for responsive indicator
        }
        
        uint32_t monitorDuration = millis() - monitorStartTime;
        Serial.printf("Ethernet monitoring shown for %u ms\n", monitorDuration);
        
        // Show final ethernet status
        if (getEthernetStatus()) {
            if (getInternetStatus()) {
                Serial.println("Final status: Ethernet and Internet connected");
            } else {
                Serial.println("Final status: Ethernet connected, Internet pending");
            }
        } else {
            Serial.println("Final status: Ethernet connection still in progress");
        }
    } else {
        Serial.println("STEP 5: Skipping ethernet monitoring (Ethernet disabled)");
    }
    
    // STEP 6: Fade out splash screen
    Serial.println("STEP 6: Fading out splash screen...");
    fadeBacklight(Backlight_off, 2000, ethernetPresent);  // Fade out over 2 seconds
    
    // Clear screen
    gfx->fillScreen(BLACK);

    if (ethernetPresent) {
        // STEP 7: Fade in to display time and ethernet connection status
        Serial.println("STEP 7: Fading in to display time and ethernet connection...");
        fadeBacklight(Backlight_on, 1000, ethernetPresent);  // Fade in over 1 second
        
        // STEP 8: Start NTP time management task
        Serial.println("STEP 8: Starting NTP time management task...");
        startNtpTimeTask();
        
        // Give NTP task a moment to display initial content
        smartDelay(500, ethernetPresent);
    } else {
        // STEP 7: Simple screen-only startup complete
        Serial.println("STEP 7: Screen-only startup complete - displaying simple message...");
        
        // Display simple message on screen
        gfx->setTextColor(WHITE, BLACK);
        gfx->setTextSize(2);
        
        // Center the text on screen
        int16_t x = (gfx->width() - (strlen("Screen Started OK") * 12)) / 2;
        int16_t y = gfx->height() / 2 - 10;
        
        gfx->setCursor(x, y);
        gfx->println("Screen Started OK");
        
        // Fade in to show the message
        fadeBacklight(Backlight_on, 1000, ethernetPresent);  // Fade in over 1 second
        
        Serial.println("Screen-only startup completed successfully");
    }
    
    Serial.println("=== Better Startup Sequence Complete ===\n");

}

#endif