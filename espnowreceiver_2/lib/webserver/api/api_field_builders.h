#ifndef API_FIELD_BUILDERS_H
#define API_FIELD_BUILDERS_H

#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESP.h>
#include <cstdint>
#include <cstring>

namespace ApiFieldBuilders {

/**
 * Field builder helper functions to reduce repetitive JSON document construction
 * and field serialization patterns throughout API handlers.
 * 
 * Usage:
 *   StaticJsonDocument<512> doc;
 *   ApiFieldBuilders::addWiFiFields(doc);
 *   ApiFieldBuilders::addChipInfo(doc);
 *   serializeJson(doc, json);
 */

// Add receiver WiFi information (SSID, IP, MAC, channel)
inline void addWiFiFields(JsonDocument& doc) {
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["channel"] = WiFi.channel();
}

// Add ESP32 chip information (model, revision, efuse MAC)
inline void addChipInfo(JsonDocument& doc) {
    char efuseMacStr[18];
    uint64_t efuseMac = ESP.getEfuseMac();
    snprintf(efuseMacStr, sizeof(efuseMacStr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             (uint8_t)(efuseMac >> 40), (uint8_t)(efuseMac >> 32),
             (uint8_t)(efuseMac >> 24), (uint8_t)(efuseMac >> 16),
             (uint8_t)(efuseMac >> 8), (uint8_t)(efuseMac));

    doc["chipModel"] = ESP.getChipModel();
    doc["chipRevision"] = ESP.getChipRevision();
    doc["efuseMac"] = efuseMacStr;
}

// Add nested transmitter object with connection info
inline JsonObject addTransmitterObject(JsonDocument& doc) {
    return doc.createNestedObject("transmitter");
}

// Add nested receiver object
inline JsonObject addReceiverObject(JsonDocument& doc) {
    return doc.createNestedObject("receiver");
}

// Helper: format version from three-part components
inline void formatVersionString(char* buf, size_t buflen, 
                                 uint8_t major, uint8_t minor, uint8_t patch) {
    snprintf(buf, buflen, "%d.%d.%d", major, minor, patch);
}

// Helper: convert version number (MMmmpp format) to string
inline void formatVersionFromNumber(char* buf, size_t buflen, uint32_t version_number) {
    uint8_t major = version_number / 10000;
    uint8_t minor = (version_number % 10000) / 100;
    uint8_t patch = version_number % 100;
    formatVersionString(buf, buflen, major, minor, patch);
}

} // namespace ApiFieldBuilders

#endif // API_FIELD_BUILDERS_H
