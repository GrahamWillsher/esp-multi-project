#ifndef RECEIVER_CONFIG_MANAGER_H
#define RECEIVER_CONFIG_MANAGER_H

#include <Arduino.h>

class ReceiverConfigManager {
public:
    static void init();
    static void updateNetworkInfo();
    static String getReceiverInfoJson();

    static const char* getIpAddress();
    static const uint8_t* getMacAddress();
    static String getMacString();
    static const char* getFirmwareVersion();
    static const char* getDeviceName();
    static const char* getBuildDate();
    static const char* getEnvironment();

    static void setDeviceName(const char* name);

private:
    static void loadFromNVS();
    static void saveToNVS();
    static void updateFromRuntime();

    static bool initialized_;

    static char rx_ip_address_[16];
    static uint8_t rx_mac_address_[6];
    static char rx_firmware_version_[16];
    static char rx_device_name_[32];
    static char rx_build_datetime_[32];
    static char rx_environment_[32];
};

#endif
