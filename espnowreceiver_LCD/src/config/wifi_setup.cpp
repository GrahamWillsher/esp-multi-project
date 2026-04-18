#include "config/wifi_setup.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>

#include "helpers.h"
#include "logging_config.h"
#include <receiver_config_manager.h>

namespace WiFiSetup {
namespace {
constexpr uint32_t kConnectTimeoutMs = 30000;
constexpr uint32_t kPollStepMs = 500;
constexpr const char* kApSsid = "ESP32-LCD-Setup";
constexpr const char* kApIp   = "192.168.4.1";
constexpr uint8_t kApChannel = 1;

void start_mdns(const char* hostname) {
    const char* h = (hostname && hostname[0] != '\0') ? hostname : "lcd-receiver";
    if (!MDNS.begin(h)) {
        LOG_WARN("WIFI", "mDNS.begin('%s') failed", h);
        return;
    }
    MDNS.addService("http", "tcp", 80);
    LOG_INFO("WIFI", "mDNS started: http://%s.local/", h);
}
}  // namespace

bool setup_from_loaded_config() {
    LOG_INFO("WIFI", "Configuring WiFi from NVS receiver config");

    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);

    if (ReceiverNetworkConfig::getHostname()[0] != '\0') {
        WiFi.setHostname(ReceiverNetworkConfig::getHostname());
    }

    if (ReceiverNetworkConfig::useStaticIP()) {
        const uint8_t* ip = ReceiverNetworkConfig::getStaticIP();
        const uint8_t* gw = ReceiverNetworkConfig::getGateway();
        const uint8_t* sn = ReceiverNetworkConfig::getSubnet();
        const uint8_t* dns1 = ReceiverNetworkConfig::getDNSPrimary();
        const uint8_t* dns2 = ReceiverNetworkConfig::getDNSSecondary();

        IPAddress static_ip(ip[0], ip[1], ip[2], ip[3]);
        IPAddress gateway(gw[0], gw[1], gw[2], gw[3]);
        IPAddress subnet(sn[0], sn[1], sn[2], sn[3]);
        IPAddress primary_dns(dns1[0], dns1[1], dns1[2], dns1[3]);
        IPAddress secondary_dns(dns2[0], dns2[1], dns2[2], dns2[3]);

        if (!WiFi.config(static_ip, gateway, subnet, primary_dns, secondary_dns)) {
            LOG_WARN("WIFI", "Static IP config failed, continuing with DHCP fallback");
        } else {
            LOG_INFO("WIFI", "Static IP configured: %s", static_ip.toString().c_str());
        }
    }

    LOG_INFO("WIFI", "Connecting to SSID: %s", ReceiverNetworkConfig::getSSID());
    WiFi.begin(ReceiverNetworkConfig::getSSID(), ReceiverNetworkConfig::getPassword());

    uint32_t waited_ms = 0;
    while (WiFi.status() != WL_CONNECTED && waited_ms < kConnectTimeoutMs) {
        smart_delay(kPollStepMs);
        waited_ms += kPollStepMs;
    }

    if (WiFi.status() != WL_CONNECTED) {
        LOG_WARN("WIFI", "WiFi connect timeout after %lu ms", static_cast<unsigned long>(waited_ms));
        return false;
    }

    const esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_err != ESP_OK) {
        LOG_WARN("WIFI", "esp_wifi_set_ps(WIFI_PS_NONE) failed: %d", static_cast<int>(ps_err));
    }

    LOG_INFO("WIFI", "Connected: IP=%s RSSI=%d dBm CH=%d",
             WiFi.localIP().toString().c_str(),
             static_cast<int>(WiFi.RSSI()),
             static_cast<int>(WiFi.channel()));
    LOG_INFO("WIFI", "MAC: %s", WiFi.macAddress().c_str());

    start_mdns(ReceiverNetworkConfig::getHostname());

    return true;
}

bool is_sta_connected() {
    const wifi_mode_t mode = WiFi.getMode();
    const bool sta_active = (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA);
    return sta_active && (WiFi.status() == WL_CONNECTED);
}

void start_ap_fallback() {
    LOG_INFO("WIFI", "Starting AP fallback: SSID=%s CH=%u", kApSsid, static_cast<unsigned>(kApChannel));
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, nullptr, kApChannel);  // open — no password
    LOG_INFO("WIFI", "AP fallback started in AP-only mode: SSID=%s IP=%s CH=%d",
             kApSsid,
             kApIp,
             static_cast<int>(WiFi.channel()));

    start_mdns("lcd-receiver");  // accessible at lcd-receiver.local even in AP mode
}

bool is_ap_mode() {
    const wifi_mode_t m = WiFi.getMode();
    return m == WIFI_MODE_AP || m == WIFI_MODE_APSTA;
}

void get_ip_string(char* buf, size_t len) {
    if (is_ap_mode()) {
        snprintf(buf, len, "%s", kApIp);
    } else {
        snprintf(buf, len, "%s", WiFi.localIP().toString().c_str());
    }
}

}  // namespace WiFiSetup
