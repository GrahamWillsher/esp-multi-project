#include "transmitter_manager.h"
#include <esp_now.h>
#include <string.h>

// Static member initialization
uint8_t TransmitterManager::mac[6] = {0};
bool TransmitterManager::mac_known = false;
uint8_t TransmitterManager::ip[4] = {0};
uint8_t TransmitterManager::gateway[4] = {0};
uint8_t TransmitterManager::subnet[4] = {0};
bool TransmitterManager::ip_known = false;
uint32_t TransmitterManager::firmware_version = 0;
bool TransmitterManager::version_known = false;
char TransmitterManager::build_date[12] = {0};
char TransmitterManager::build_time[9] = {0};
bool TransmitterManager::metadata_received = false;
bool TransmitterManager::metadata_valid = false;
char TransmitterManager::metadata_env[32] = {0};
char TransmitterManager::metadata_device[16] = {0};
uint8_t TransmitterManager::metadata_major = 0;
uint8_t TransmitterManager::metadata_minor = 0;
uint8_t TransmitterManager::metadata_patch = 0;
char TransmitterManager::metadata_build_date[48] = {0};

void TransmitterManager::registerMAC(const uint8_t* transmitter_mac) {
    if (transmitter_mac == nullptr) return;
    
    memcpy(mac, transmitter_mac, 6);
    mac_known = true;
    
    Serial.printf("[TX_MGR] MAC registered: %s\n", getMACString().c_str());
    
    // Add as ESP-NOW peer
    if (!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = 0;
        peer.encrypt = false;
        peer.ifidx = WIFI_IF_STA;
        
        if (esp_now_add_peer(&peer) == ESP_OK) {
            Serial.println("[TX_MGR] Added as ESP-NOW peer");
        } else {
            Serial.println("[TX_MGR] ERROR: Failed to add as ESP-NOW peer");
        }
    }
}

const uint8_t* TransmitterManager::getMAC() {
    return mac_known ? mac : nullptr;
}

bool TransmitterManager::isMACKnown() {
    return mac_known;
}

String TransmitterManager::getMACString() {
    if (!mac_known) return "Unknown";
    char str[18];
    snprintf(str, sizeof(str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(str);
}

void TransmitterManager::storeIPData(const uint8_t* transmitter_ip,
                                     const uint8_t* transmitter_gateway,
                                     const uint8_t* transmitter_subnet) {
    if (!transmitter_ip || !transmitter_gateway || !transmitter_subnet) return;
    
    memcpy(ip, transmitter_ip, 4);
    memcpy(gateway, transmitter_gateway, 4);
    memcpy(subnet, transmitter_subnet, 4);
    ip_known = true;
    
    Serial.printf("[TX_MGR] IP data: %s, Gateway: %d.%d.%d.%d, Subnet: %d.%d.%d.%d\n",
                 getIPString().c_str(),
                 gateway[0], gateway[1], gateway[2], gateway[3],
                 subnet[0], subnet[1], subnet[2], subnet[3]);
}

const uint8_t* TransmitterManager::getIP() {
    return ip_known ? ip : nullptr;
}

const uint8_t* TransmitterManager::getGateway() {
    return ip_known ? gateway : nullptr;
}

const uint8_t* TransmitterManager::getSubnet() {
    return ip_known ? subnet : nullptr;
}

bool TransmitterManager::isIPKnown() {
    return ip_known;
}

String TransmitterManager::getIPString() {
    if (!ip_known) return "0.0.0.0";
    char str[16];
    snprintf(str, sizeof(str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return String(str);
}

String TransmitterManager::getURL() {
    if (!ip_known) return "";
    return "http://" + getIPString();
}

void TransmitterManager::storeFirmwareVersion(uint32_t version, const char* date, const char* time) {
    firmware_version = version;
    version_known = true;
    
    if (date != nullptr) {
        strncpy(build_date, date, sizeof(build_date) - 1);
        build_date[sizeof(build_date) - 1] = '\0';
    } else {
        build_date[0] = '\0';
    }
    
    if (time != nullptr) {
        strncpy(build_time, time, sizeof(build_time) - 1);
        build_time[sizeof(build_time) - 1] = '\0';
    } else {
        build_time[0] = '\0';
    }
    
    uint8_t major = (version / 10000);
    uint8_t minor = (version / 100) % 100;
    uint8_t patch = version % 100;
    
    if (date != nullptr && time != nullptr) {
        Serial.printf("[TX_MGR] Firmware version: v%d.%d.%d (%u), built %s %s\n", 
                     major, minor, patch, version, date, time);
    } else {
        Serial.printf("[TX_MGR] Firmware version: v%d.%d.%d (%u)\n", major, minor, patch, version);
    }
}

uint32_t TransmitterManager::getFirmwareVersion() {
    return firmware_version;
}

bool TransmitterManager::hasVersionInfo() {
    return version_known;
}

const char* TransmitterManager::getBuildDate() {
    return build_date;
}

const char* TransmitterManager::getBuildTime() {
    return build_time;
}

// Metadata management
void TransmitterManager::storeMetadata(bool valid, const char* env, const char* device,
                                       uint8_t major, uint8_t minor, uint8_t patch,
                                       const char* build_date_str) {
    metadata_received = true;
    metadata_valid = valid;
    
    if (env != nullptr) {
        strncpy(metadata_env, env, sizeof(metadata_env) - 1);
        metadata_env[sizeof(metadata_env) - 1] = '\0';
    }
    
    if (device != nullptr) {
        strncpy(metadata_device, device, sizeof(metadata_device) - 1);
        metadata_device[sizeof(metadata_device) - 1] = '\0';
    }
    
    metadata_major = major;
    metadata_minor = minor;
    metadata_patch = patch;
    
    if (build_date_str != nullptr) {
        strncpy(metadata_build_date, build_date_str, sizeof(metadata_build_date) - 1);
        metadata_build_date[sizeof(metadata_build_date) - 1] = '\0';
    }
    
    char indicator = valid ? '@' : '*';
    Serial.printf("[TX_MGR] Metadata: %s %s v%d.%d.%d %c\n", 
                 metadata_device, metadata_env, major, minor, patch, indicator);
    if (build_date_str != nullptr && strlen(build_date_str) > 0) {
        Serial.printf("[TX_MGR]   Built: %s\n", metadata_build_date);
    }
}

bool TransmitterManager::hasMetadata() {
    return metadata_received;
}

bool TransmitterManager::isMetadataValid() {
    return metadata_valid;
}

const char* TransmitterManager::getMetadataEnv() {
    return metadata_env;
}

const char* TransmitterManager::getMetadataDevice() {
    return metadata_device;
}

void TransmitterManager::getMetadataVersion(uint8_t& major, uint8_t& minor, uint8_t& patch) {
    major = metadata_major;
    minor = metadata_minor;
    patch = metadata_patch;
}

const char* TransmitterManager::getMetadataBuildDate() {
    return metadata_build_date;
}
