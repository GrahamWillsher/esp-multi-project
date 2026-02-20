#ifndef __HW_LILYGO_T_CONNECT_PRO_H__
#define __HW_LILYGO_T_CONNECT_PRO_H__

#include "hal.h"
// Don't include Arduino_GFX_Library.h here - moved to .cpp file
//#include <ArduinoJson.h>

// Forward declarations instead of including the full library
class Arduino_DataBus;
class Arduino_GFX;

class LilyGoTConnectProHal : public Esp32Hal {
 public:
  const char* name() { return "LilyGo T-Connect Pro"; }

  // Power control
  virtual gpio_num_t PIN_5V_EN() { return GPIO_NUM_10; }
  
  // RS485 interface
  // virtual gpio_num_t RS485_EN_PIN() { return GPIO_NUM_17; }
  // virtual gpio_num_t RS485_TX_PIN() { return GPIO_NUM_16; }
  // virtual gpio_num_t RS485_RX_PIN() { return GPIO_NUM_15; }
  // virtual gpio_num_t RS485_SE_PIN() { return GPIO_NUM_NC; }

//added by GJW 22/9/2025
  virtual gpio_num_t RS485_TX_PIN() { return GPIO_NUM_17; }
  virtual gpio_num_t RS485_RX_PIN() { return GPIO_NUM_18; }

  // CAN interface - using internal TWAI controller
  // virtual gpio_num_t CAN_TX_PIN() { return GPIO_NUM_12; }
  // virtual gpio_num_t CAN_RX_PIN() { return GPIO_NUM_13; }
  // virtual gpio_num_t CAN_SE_PIN() { return GPIO_NUM_NC; }

//added by GJW 22/9/2025
  // CAN interface - using internal TWAI controller
  virtual gpio_num_t CAN_TX_PIN() { return GPIO_NUM_6; }
  virtual gpio_num_t CAN_RX_PIN() { return GPIO_NUM_7; }

  // External MCP2515 CAN controller (if used)

  // virtual gpio_num_t MCP2515_SCK() { return GPIO_NUM_6; }
  // virtual gpio_num_t MCP2515_MOSI() { return GPIO_NUM_7; }
  // virtual gpio_num_t MCP2515_MISO() { return GPIO_NUM_5; }
  // virtual gpio_num_t MCP2515_CS() { return GPIO_NUM_4; }
  // virtual gpio_num_t MCP2515_INT() { return GPIO_NUM_3; }
  // virtual gpio_num_t MCP2515_RST() { return GPIO_NUM_2; }

  // MCP2517 CANFD controller (if used)
  // virtual gpio_num_t MCP2517_SCK() { return GPIO_NUM_6; }
  // virtual gpio_num_t MCP2517_SDI() { return GPIO_NUM_7; }
  // virtual gpio_num_t MCP2517_SDO() { return GPIO_NUM_5; }
  // virtual gpio_num_t MCP2517_CS() { return GPIO_NUM_4; }
  // virtual gpio_num_t MCP2517_INT() { return GPIO_NUM_3; }

  // Contactor control pins
  virtual gpio_num_t POSITIVE_CONTACTOR_PIN() { return GPIO_NUM_15; }
  virtual gpio_num_t NEGATIVE_CONTACTOR_PIN() { return GPIO_NUM_16; }
  virtual gpio_num_t PRECHARGE_PIN() { return GPIO_NUM_2; }
  virtual gpio_num_t BMS_POWER() { return GPIO_NUM_1; }

  // SD card interface
  virtual gpio_num_t SD_MISO_PIN() { return GPIO_NUM_37; }
  virtual gpio_num_t SD_MOSI_PIN() { return GPIO_NUM_35; }
  virtual gpio_num_t SD_SCLK_PIN() { return GPIO_NUM_36; }
  virtual gpio_num_t SD_CS_PIN() { return GPIO_NUM_34; }

  // LED control (using built-in RGB LED)
  // virtual gpio_num_t LED_PIN() { return GPIO_NUM_8; }
  virtual gpio_num_t LED_PIN() { return GPIO_NUM_5; } // set as dummy GPIO to use the RS232 pin
  virtual uint8_t LED_MAX_BRIGHTNESS() { return 255; } // Increased brightness for T-Connect Pro

  // Equipment stop button
  virtual gpio_num_t EQUIPMENT_STOP_PIN() { return GPIO_NUM_0; }

  // Battery wake up pins
  // virtual gpio_num_t WUP_PIN1() { return GPIO_NUM_1; }
  // virtual gpio_num_t WUP_PIN2() { return GPIO_NUM_14; }

  // CHAdeMO support pins (if needed)
  virtual gpio_num_t CHADEMO_PIN_2() { return GPIO_NUM_NC; }
  virtual gpio_num_t CHADEMO_PIN_10() { return GPIO_NUM_NC; }
  virtual gpio_num_t CHADEMO_PIN_7() { return GPIO_NUM_NC; }
  virtual gpio_num_t CHADEMO_PIN_4() { return GPIO_NUM_NC; }
  virtual gpio_num_t CHADEMO_LOCK() { return GPIO_NUM_NC; }

  // Additional pins for advanced features
  virtual gpio_num_t SECOND_BATTERY_CONTACTORS_PIN() { return GPIO_NUM_NC; }
  virtual gpio_num_t HIA4V1_PIN() { return GPIO_NUM_NC; }
  virtual gpio_num_t INVERTER_DISCONNECT_CONTACTOR_PIN() { return GPIO_NUM_NC; }
  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_PIN() { return GPIO_NUM_NC; }
  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_LED_PIN() { return GPIO_NUM_NC; }

  virtual std::vector<comm_interface> available_interfaces() {
    return {comm_interface::Modbus,
            comm_interface::RS485,
            comm_interface::CanNative
            // comm_interface::LCDDisplay,
            // comm_interface::Ethernet
//            comm_interface::Wifi
//            comm_interface::CanAddonMcp2515,
//            comm_interface::CanFdAddonMcp2518
};
  }
};

//  added by GJW 22/9/2025

// struct KeySearchResult {
//     bool found;
//     String value;
//     String name;
//     KeySearchResult() : found(false), value(""), name("") {} // Default constructor
// };


#define HalClass LilyGoTConnectProHal


struct LCDColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

const LCDColor RGB_WHITE = {255, 255, 255};
const LCDColor RGB_BLACK = {0, 0, 0};
const LCDColor RGB_BRIGHT_GREEN = {0, 255, 0};
const LCDColor RGB_DARK_GREEN = {0, 64, 0};
const LCDColor RGB_BRIGHT_RED = {255, 0, 0};
const LCDColor RGB_ORANGE = {255, 165, 0};
const LCDColor RGB_BLUE = {0, 100, 255};
const LCDColor RGB_DIM_GREEN = {0, 128, 0};
const LCDColor LCD_ERROR = RGB_BRIGHT_RED;
const LCDColor LCD_WARNING = RGB_ORANGE;
const LCDColor LCD_NORMAL = RGB_BRIGHT_GREEN;

// Display rotation constants
const uint8_t ROTATION_PORTRAIT = 0;          // 0° - Portrait mode
const uint8_t ROTATION_LANDSCAPE = 1;         // 90° - Landscape mode (default)
const uint8_t ROTATION_PORTRAIT_FLIPPED = 2;  // 180° - Upside-down portrait
const uint8_t ROTATION_LANDSCAPE_FLIPPED = 3; // 270° - Landscape flipped


#define IIC_SDA 39
#define IIC_SCL 40

// ST7796
#define SCREEN_WIDTH 222
#define SCREEN_HEIGHT 480
#define SCREEN_BL 46
#define SCREEN_MOSI 11
#define SCREEN_MISO 13
#define SCREEN_SCLK 12
#define SCREEN_CS 21
#define SCREEN_DC 41
#define SCREEN_RST -1


// W5500 Ethernet pin definitions for LilyGO T-Connect Pro
#define W5500_CS_PIN    10    // Chip Select pin for W5500 (CHANGED from 10 to avoid conflict with 5V_EN)
#define W5500_RST_PIN   48   // Reset pin for W5500 (optional)
#define W5500_INT_PIN   9    // Interrupt pin from W5500 (optional)
// PIN_5V_EN is defined as virtual function PIN_5V_EN() in HAL class

// Additional W5500 SPI pins (using default ESP32 SPI pins)
#define W5500_SCLK_PIN 12   // SPI Clock
#define W5500_MISO_PIN 13   // SPI MISO
#define W5500_MOSI_PIN 11   // SPI MOSI


// LCD hardware objects - declared extern for use in other files
extern Arduino_DataBus *bus;
extern Arduino_GFX *gfx;


// NTP server configuration
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"

// Backlight brightness constants
const int Backlight_off = 0;
const int Backlight_on = 255;


// LCD hardware initialization functions
//void init_lcd_display(void);
void cleanup_lcd_display(void);
void Update_lcd_battery_display(void);
void init_LCDDisplay_template();

// LED simulation function for LCD display
void Display_LED_Status(uint32_t pixel_color);

// CPU usage monitoring function
float Calculate_cpu_usage(void);


// CPU temperature monitoring function
float get_cpu_temperature(void);

// Network connectivity check functions
// bool init_Ethernet(void);
void init_ethernet_if_available(void);  // Initialize Ethernet in main setup
bool initEthernet(void);
bool checkEthernetConnection(void);
bool test_static_ip_connectivity(void);
// bool check_ethernet_connectivity(void);


bool checkInternetConnectivity(const char* testHost = "8.8.8.8", uint16_t testPort = 53, uint32_t timeoutMs = 5000);


// W5500 Ethernet functions
bool validate_static_ip_config(void);
bool init_w5500_ethernet(void);
void check_ethernet_status(void);
bool is_ethernet_connected(void);
std::string get_ethernet_ip(void);
void print_network_config(void);


// Setting up the hardware for the LCd display and Ethernet adapter

void setupLCDDisplay();
void setupEthernetAdapter();


// Helper function to convert LCDColor to 16-bit color565 format
uint16_t LCDtoColor565(const LCDColor& color);

// Status indicator functions
void drawStatusIndicator(int x, int y, int radius, uint16_t color, bool isOn);
//void showEthernetStatusIndicator(int durationMs = 2000);

// Enhanced connectivity monitoring functions
void initEthernetInterrupt();
void startConnectivityTask();
void stopConnectivityTask();
bool getEthernetStatus(void);
bool getInternetStatus(void);

// NTP Time Management Task Functions
String getTimezoneFromLocation(void);
void getNTPTime();
void configureTimezone();
void ntpTimeTask(void* parameter);
bool startNtpTimeTask(void);
void stopNtpTimeTask();

// Splash screen functions
void displaySplashJpeg(const char* filename);
void debugLittleFSFiles();
//void displaySplashScreen();

// New optimized startup sequence helper functions
void displaySplashScreenContent();
void drawEthernetStatusIndicator();

// Ethernet configuration
extern bool ethernetPresent;  // Global flag to control ethernet functionality

// Smart delay function - context-aware delay selection
void smartDelay(uint32_t ms, bool ethernetPresent = true);

// Backlight control functions
void initBacklightBrightness();  // Initialize/sync brightness with PWM state
void setBacklightBrightness(uint8_t brightness);
uint8_t getBacklightBrightness(void);
void fadeBacklight(uint8_t targetBrightness, uint32_t durationMs, uint16_t steps = 85, bool ethernetPresent = true);
// Overload to allow setting ethernetPresent with default steps
void fadeBacklight(uint8_t targetBrightness, uint32_t durationMs, bool ethernetPresent);

// Optimized startup sequence
void startupSequenceWithEthernet(bool ethernetPresent = true);


#define IIC_SDA 39
#define IIC_SCL 40

// ST7796
#define SCREEN_WIDTH 222
#define SCREEN_HEIGHT 480
#define SCREEN_BL 46
#define SCREEN_MOSI 11
#define SCREEN_MISO 13
#define SCREEN_SCLK 12
#define SCREEN_CS 21
#define SCREEN_DC 41
#define SCREEN_RST -1

// W5500 Ethernet pin definitions for LilyGO T-Connect Pro
#define W5500_CS_PIN    10    // Chip Select pin for W5500 (CHANGED from 10 to avoid conflict with 5V_EN)
#define W5500_RST_PIN   48   // Reset pin for W5500 (optional)
#define W5500_INT_PIN   9    // Interrupt pin from W5500 (optional)
// PIN_5V_EN is defined as virtual function PIN_5V_EN() in HAL class

// Additional W5500 SPI pins (using default ESP32 SPI pins)
#define W5500_SCLK_PIN 12   // SPI Clock
#define W5500_MISO_PIN 13   // SPI MISO
#define W5500_MOSI_PIN 11   // SPI MOSI


// void Simulated_Flashing_LED(int x, int y, int radius, LCDColor from, LCDColor to, int periodMs = 2000, int steps = 50);

// void Simulated_LED(LCDColor color);

// Fully automated LCD label system - ONLY change the macro list below!
// Uses macro expansion to generate all code automatically

struct LCDLabel {
    const char* text;
    int char_count;
    int x;
    int y;
    int index;
};

// **SINGLE POINT OF CHANGE** - Only modify this list to add/remove/change labels!
// Format: LABEL_ENTRY(NAME, "Display Text", x_pos, y_pos)
#define LCD_LABEL_LIST \
    LABEL_ENTRY(VOLTAGE, "Voltage: ", 5, 70) \
    LABEL_ENTRY(CURRENT, "Current: ", 5, 100) \
    LABEL_ENTRY(SOC, "SoC: ", 5, 130) \
    LABEL_ENTRY(TEMP, "Temp: ", 5, 160) \
    LABEL_ENTRY(CPU_TEMP, "CPU Temp: ", 5, 190)

// Macro to generate extern declarations
#define LABEL_ENTRY(name, text, x, y) extern const LCDLabel LCD_##name;
LCD_LABEL_LIST
#undef LABEL_ENTRY

// Array of all labels for iteration
extern const LCDLabel* const LCD_LABELS[];
extern const int LCD_LABELS_COUNT;

/* ----- Error checks below, don't change (can't be moved to separate file) ----- */
#ifndef HW_CONFIGURED
#define HW_CONFIGURED
#else
#error Multiple HW defined! Please select a single HW
#endif

#endif