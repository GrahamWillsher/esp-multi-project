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
constexpr uint32_t kRecoveryRetryInitialMs = 5000;
constexpr uint32_t kRecoveryRetryStepMs = 5000;
constexpr uint32_t kRecoveryRetryMaxMs = 60000;
constexpr uint32_t kRecoveryStableConnectMs = 10000;
constexpr const char* kApSsid = "ESP32-LCD-Setup";
constexpr const char* kApIp   = "192.168.4.1";
constexpr uint8_t kApChannel = 1;

bool g_recovery_active = false;
uint32_t g_recovery_next_retry_ms = 0;
uint32_t g_recovery_retry_interval_ms = kRecoveryRetryInitialMs;
uint32_t g_recovery_connected_since_ms = 0;
bool g_mdns_started = false;

void start_mdns(const char* hostname) {
    const char* h = (hostname && hostname[0] != '\0') ? hostname : "lcd-receiver";

    if (g_mdns_started) {
        MDNS.end();
        g_mdns_started = false;
    }

    if (!MDNS.begin(h)) {
        LOG_WARN("WIFI", "mDNS.begin('%s') failed", h);
        return;
    }

    g_mdns_started = true;
    if (!MDNS.addService("http", "tcp", 80)) {
        LOG_WARN("WIFI", "mDNS service registration failed for http.tcp on host '%s'", h);
    }

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

void start_ap_fallback(const bool has_credentials) {
    LOG_INFO("WIFI", "Starting AP fallback: SSID=%s CH=%u mode=%s",
             kApSsid,
             static_cast<unsigned>(kApChannel),
             has_credentials ? "AP+STA recovery" : "AP-only provisioning");
    WiFi.disconnect(false, false);
    WiFi.mode(has_credentials ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(kApSsid, nullptr, kApChannel);  // open — no password
    LOG_INFO("WIFI", "AP fallback started: SSID=%s IP=%s CH=%d mode=%s",
             kApSsid,
             kApIp,
             static_cast<int>(WiFi.channel()),
             has_credentials ? "AP+STA" : "AP");

    if (has_credentials) {
        WiFi.begin(ReceiverNetworkConfig::getSSID(), ReceiverNetworkConfig::getPassword());
        g_recovery_active = true;
        g_recovery_next_retry_ms = millis() + kRecoveryRetryInitialMs;
        g_recovery_retry_interval_ms = kRecoveryRetryInitialMs;
        g_recovery_connected_since_ms = 0;
        LOG_INFO("WIFI", "AP+STA recovery armed: retry=%lu ms step=%lu ms max=%lu ms",
                 static_cast<unsigned long>(kRecoveryRetryInitialMs),
                 static_cast<unsigned long>(kRecoveryRetryStepMs),
                 static_cast<unsigned long>(kRecoveryRetryMaxMs));
    } else {
        g_recovery_active = false;
        g_recovery_next_retry_ms = 0;
        g_recovery_retry_interval_ms = kRecoveryRetryInitialMs;
        g_recovery_connected_since_ms = 0;
    }

    start_mdns("lcd-receiver");  // accessible at lcd-receiver.local even in AP mode
}

bool service_recovery() {
    if (!g_recovery_active) {
        return false;
    }

    const uint32_t now = millis();

    if (is_sta_connected()) {
        if (g_recovery_connected_since_ms == 0) {
            g_recovery_connected_since_ms = now;
            LOG_INFO("WIFI", "Recovery: STA connected, waiting stability window (%lu ms)",
                     static_cast<unsigned long>(kRecoveryStableConnectMs));
        }

        if (now - g_recovery_connected_since_ms >= kRecoveryStableConnectMs) {
            g_recovery_active = false;
            LOG_INFO("WIFI", "Recovery: STA stable (IP=%s), requesting reboot for full stack bring-up",
                     WiFi.localIP().toString().c_str());
            return true;
        }

        return false;
    }

    g_recovery_connected_since_ms = 0;

    if (static_cast<int32_t>(now - g_recovery_next_retry_ms) < 0) {
        return false;
    }

    LOG_WARN("WIFI", "Recovery: retrying STA connect to SSID=%s (interval=%lu ms)",
             ReceiverNetworkConfig::getSSID(),
             static_cast<unsigned long>(g_recovery_retry_interval_ms));

    WiFi.disconnect(false, false);
    WiFi.begin(ReceiverNetworkConfig::getSSID(), ReceiverNetworkConfig::getPassword());

    g_recovery_retry_interval_ms =
        (g_recovery_retry_interval_ms + kRecoveryRetryStepMs <= kRecoveryRetryMaxMs)
            ? (g_recovery_retry_interval_ms + kRecoveryRetryStepMs)
            : kRecoveryRetryMaxMs;
    g_recovery_next_retry_ms = now + g_recovery_retry_interval_ms;

    return false;
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
