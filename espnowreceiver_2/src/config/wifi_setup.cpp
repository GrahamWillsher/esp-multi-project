#include "wifi_setup.h"
#include "../common.h"
#include <WiFi.h>

void setupWiFi() {
    LOG_INFO("INIT", "Configuring WiFi with static IP...");
    if (!WiFi.config(Config::LOCAL_IP, Config::GATEWAY, Config::SUBNET, Config::PRIMARY_DNS, Config::SECONDARY_DNS)) {
        LOG_ERROR("INIT", "Static IP configuration failed");
    }
    
    WiFi.mode(WIFI_STA);
    
    if (strlen(Config::WIFI_PASSWORD) > 0) {
        LOG_INFO("INIT", "Connecting to WiFi: %s", Config::WIFI_SSID);
        WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            smart_delay(500);
            LOG_DEBUG("INIT", ".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            ESPNow::wifi_channel = WiFi.channel();
            LOG_INFO("INIT", "WiFi connected! IP: %s", WiFi.localIP().toString().c_str());
            LOG_INFO("INIT", "WiFi Channel: %d", ESPNow::wifi_channel);
            LOG_INFO("INIT", "WiFi will stay connected for web server");
        } else {
            LOG_WARN("INIT", "WiFi connection failed, continuing without web server");
        }
    } else {
        LOG_WARN("INIT", "WiFi password not set, skipping WiFi connection");
    }
    
    LOG_INFO("INIT", "MAC Address: %s", WiFi.macAddress().c_str());
}
