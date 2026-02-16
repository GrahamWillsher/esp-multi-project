#include "receiver_config_manager.h"
#include <Preferences.h>
#include <WiFi.h>
#include <firmware_version.h>
#include <firmware_metadata.h>

namespace {
    constexpr const char* kNvsNamespace = "rx_config";
    constexpr const char* kKeyIp = "ip";
    constexpr const char* kKeyMac = "mac";
    constexpr const char* kKeyFirmware = "fw";
    constexpr const char* kKeyDeviceName = "dev";
    constexpr const char* kKeyBuild = "build";
    constexpr const char* kKeyEnv = "env";
}

bool ReceiverConfigManager::initialized_ = false;

char ReceiverConfigManager::rx_ip_address_[16] = {0};
uint8_t ReceiverConfigManager::rx_mac_address_[6] = {0};
char ReceiverConfigManager::rx_firmware_version_[16] = {0};
char ReceiverConfigManager::rx_device_name_[32] = {0};
char ReceiverConfigManager::rx_build_datetime_[32] = {0};
char ReceiverConfigManager::rx_environment_[32] = {0};

void ReceiverConfigManager::init() {
    loadFromNVS();
    updateFromRuntime();
    saveToNVS();
    initialized_ = true;
}

void ReceiverConfigManager::updateNetworkInfo() {
    updateFromRuntime();
    saveToNVS();
}

void ReceiverConfigManager::loadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) {
        return;
    }

    String ip = prefs.getString(kKeyIp, "0.0.0.0");
    strncpy(rx_ip_address_, ip.c_str(), sizeof(rx_ip_address_) - 1);
    rx_ip_address_[sizeof(rx_ip_address_) - 1] = '\0';

    size_t mac_len = prefs.getBytes(kKeyMac, rx_mac_address_, sizeof(rx_mac_address_));
    if (mac_len != sizeof(rx_mac_address_)) {
        memset(rx_mac_address_, 0, sizeof(rx_mac_address_));
    }

    String fw = prefs.getString(kKeyFirmware, FW_VERSION_STRING);
    strncpy(rx_firmware_version_, fw.c_str(), sizeof(rx_firmware_version_) - 1);
    rx_firmware_version_[sizeof(rx_firmware_version_) - 1] = '\0';

    String dev = prefs.getString(kKeyDeviceName, DEVICE_NAME);
    strncpy(rx_device_name_, dev.c_str(), sizeof(rx_device_name_) - 1);
    rx_device_name_[sizeof(rx_device_name_) - 1] = '\0';

    String build = prefs.getString(kKeyBuild, String(__DATE__) + " " + String(__TIME__));
    strncpy(rx_build_datetime_, build.c_str(), sizeof(rx_build_datetime_) - 1);
    rx_build_datetime_[sizeof(rx_build_datetime_) - 1] = '\0';

    String env = prefs.getString(kKeyEnv, "");
    strncpy(rx_environment_, env.c_str(), sizeof(rx_environment_) - 1);
    rx_environment_[sizeof(rx_environment_) - 1] = '\0';

    prefs.end();
}

void ReceiverConfigManager::saveToNVS() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        return;
    }

    prefs.putString(kKeyIp, rx_ip_address_);
    prefs.putBytes(kKeyMac, rx_mac_address_, sizeof(rx_mac_address_));
    prefs.putString(kKeyFirmware, rx_firmware_version_);
    prefs.putString(kKeyDeviceName, rx_device_name_);
    prefs.putString(kKeyBuild, rx_build_datetime_);
    prefs.putString(kKeyEnv, rx_environment_);
    prefs.end();
}

void ReceiverConfigManager::updateFromRuntime() {
    if (WiFi.status() == WL_CONNECTED) {
        String ip = WiFi.localIP().toString();
        strncpy(rx_ip_address_, ip.c_str(), sizeof(rx_ip_address_) - 1);
        rx_ip_address_[sizeof(rx_ip_address_) - 1] = '\0';
    }

    String mac_str = WiFi.macAddress();
    if (!mac_str.isEmpty()) {
        unsigned int mac_bytes[6];
        if (sscanf(mac_str.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
                   &mac_bytes[0], &mac_bytes[1], &mac_bytes[2],
                   &mac_bytes[3], &mac_bytes[4], &mac_bytes[5]) == 6) {
            for (int i = 0; i < 6; ++i) {
                rx_mac_address_[i] = static_cast<uint8_t>(mac_bytes[i]);
            }
        }
    }

    strncpy(rx_firmware_version_, FW_VERSION_STRING, sizeof(rx_firmware_version_) - 1);
    rx_firmware_version_[sizeof(rx_firmware_version_) - 1] = '\0';

    if (rx_device_name_[0] == '\0') {
        strncpy(rx_device_name_, DEVICE_NAME, sizeof(rx_device_name_) - 1);
        rx_device_name_[sizeof(rx_device_name_) - 1] = '\0';
    }

    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        strncpy(rx_environment_, FirmwareMetadata::metadata.env_name, sizeof(rx_environment_) - 1);
        rx_environment_[sizeof(rx_environment_) - 1] = '\0';

        strncpy(rx_build_datetime_, FirmwareMetadata::metadata.build_date, sizeof(rx_build_datetime_) - 1);
        rx_build_datetime_[sizeof(rx_build_datetime_) - 1] = '\0';
    } else {
        String build = String(__DATE__) + " " + String(__TIME__);
        strncpy(rx_build_datetime_, build.c_str(), sizeof(rx_build_datetime_) - 1);
        rx_build_datetime_[sizeof(rx_build_datetime_) - 1] = '\0';
    }
}

String ReceiverConfigManager::getReceiverInfoJson() {
    char json[512];
    snprintf(json, sizeof(json),
             "{"
             "\"success\":true,"
             "\"ip\":\"%s\","
             "\"mac\":\"%s\","
             "\"firmware\":\"%s\","
             "\"device_name\":\"%s\","
             "\"build_date\":\"%s\","
             "\"environment\":\"%s\""
             "}",
             rx_ip_address_,
             getMacString().c_str(),
             rx_firmware_version_,
             rx_device_name_,
             rx_build_datetime_,
             rx_environment_);
    return String(json);
}

const char* ReceiverConfigManager::getIpAddress() {
    return rx_ip_address_;
}

const uint8_t* ReceiverConfigManager::getMacAddress() {
    return rx_mac_address_;
}

String ReceiverConfigManager::getMacString() {
    char str[18];
    snprintf(str, sizeof(str), "%02X:%02X:%02X:%02X:%02X:%02X",
             rx_mac_address_[0], rx_mac_address_[1], rx_mac_address_[2],
             rx_mac_address_[3], rx_mac_address_[4], rx_mac_address_[5]);
    return String(str);
}

const char* ReceiverConfigManager::getFirmwareVersion() {
    return rx_firmware_version_;
}

const char* ReceiverConfigManager::getDeviceName() {
    return rx_device_name_;
}

const char* ReceiverConfigManager::getBuildDate() {
    return rx_build_datetime_;
}

const char* ReceiverConfigManager::getEnvironment() {
    return rx_environment_;
}

void ReceiverConfigManager::setDeviceName(const char* name) {
    if (!name) return;
    strncpy(rx_device_name_, name, sizeof(rx_device_name_) - 1);
    rx_device_name_[sizeof(rx_device_name_) - 1] = '\0';
    if (initialized_) {
        saveToNVS();
    }
}
