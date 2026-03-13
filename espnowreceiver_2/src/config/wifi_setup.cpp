#include "wifi_setup.h"
#include "../common.h"
#include <WiFi.h>

void setupWiFi() {
    Serial.println("[INIT] Configuring WiFi with static IP...");
    if (!WiFi.config(Config::LOCAL_IP, Config::GATEWAY, Config::SUBNET, Config::PRIMARY_DNS, Config::SECONDARY_DNS)) {
        Serial.println("[ERROR] Static IP configuration failed!");
    }
    
    WiFi.mode(WIFI_STA);
    
    if (strlen(Config::WIFI_PASSWORD) > 0) {
        Serial.printf("[INIT] Connecting to WiFi: %s\n", Config::WIFI_SSID);
        WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            smart_delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println();
            ESPNow::wifi_channel = WiFi.channel();
            Serial.print("[INIT] WiFi connected! IP: ");
            Serial.println(WiFi.localIP());
            Serial.print("[INIT] WiFi Channel: ");
            Serial.println(ESPNow::wifi_channel);
            Serial.println("[INIT] WiFi will stay connected for web server");
        } else {
            Serial.println();
            Serial.println("[WARN] WiFi connection failed, continuing without web server");
        }
    } else {
        Serial.println("[WARN] WiFi password not set, skipping WiFi connection");
    }
    
    Serial.print("[INIT] MAC Address: ");
    Serial.println(WiFi.macAddress());
}
