#ifndef TRANSMITTER_MANAGER_H
#define TRANSMITTER_MANAGER_H

#include <Arduino.h>

class TransmitterManager {
private:
    static uint8_t mac[6];
    static bool mac_known;
    
    static uint8_t ip[4];
    static uint8_t gateway[4];
    static uint8_t subnet[4];
    static bool ip_known;
    
    static uint32_t firmware_version;
    static bool version_known;
    static char build_date[12];
    static char build_time[9];
    
public:
    // MAC management
    static void registerMAC(const uint8_t* transmitter_mac);
    static const uint8_t* getMAC();
    static bool isMACKnown();
    static String getMACString();
    
    // IP management
    static void storeIPData(const uint8_t* transmitter_ip, 
                           const uint8_t* transmitter_gateway,
                           const uint8_t* transmitter_subnet);
    static const uint8_t* getIP();
    static const uint8_t* getGateway();
    static const uint8_t* getSubnet();
    static bool isIPKnown();
    static String getIPString();
    static String getURL();  // Returns http://x.x.x.x
    
    // Version management
    static void storeFirmwareVersion(uint32_t version, const char* date = nullptr, const char* time = nullptr);
    static uint32_t getFirmwareVersion();
    static bool hasVersionInfo();
    static const char* getBuildDate();
    static const char* getBuildTime();
};

#endif
