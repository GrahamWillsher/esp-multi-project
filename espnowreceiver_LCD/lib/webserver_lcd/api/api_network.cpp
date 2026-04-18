#include "api_network.h"
#include "api_utils.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <receiver_config_manager.h>
#include <logging_config.h>

namespace ApiNetwork {

esp_err_t handle_get(httpd_req_t* req) {
    char ip_str[16], gw_str[16], sn_str[16], dns1_str[16], dns2_str[16], mqtt_str[16];
    ApiUtils::format_ipv4(ip_str,  ReceiverNetworkConfig::getStaticIP());
    ApiUtils::format_ipv4(gw_str,  ReceiverNetworkConfig::getGateway());
    ApiUtils::format_ipv4(sn_str,  ReceiverNetworkConfig::getSubnet());
    ApiUtils::format_ipv4(dns1_str, ReceiverNetworkConfig::getDNSPrimary());
    ApiUtils::format_ipv4(dns2_str, ReceiverNetworkConfig::getDNSSecondary());
    ApiUtils::format_ipv4(mqtt_str, ReceiverNetworkConfig::getMqttServer());

    StaticJsonDocument<640> doc;
    doc["success"]       = true;
    doc["hostname"]      = ReceiverNetworkConfig::getHostname();
    doc["ssid"]          = ReceiverNetworkConfig::getSSID();
    doc["use_static_ip"] = ReceiverNetworkConfig::useStaticIP();
    doc["static_ip"]     = ip_str;
    doc["gateway"]       = gw_str;
    doc["subnet"]        = sn_str;
    doc["dns_primary"]   = dns1_str;
    doc["dns_secondary"] = dns2_str;
    doc["mqtt_enabled"]  = ReceiverNetworkConfig::isMqttEnabled();
    doc["mqtt_server"]   = mqtt_str;
    doc["mqtt_port"]     = ReceiverNetworkConfig::getMqttPort();
    doc["mqtt_username"] = ReceiverNetworkConfig::getMqttUsername();
    doc["wifi_mac"]      = WiFi.macAddress().c_str();
    doc["wifi_ip"]       = WiFi.localIP().toString().c_str();

    return ApiUtils::send_json_doc(req, doc);
}

// Reboot helper — called after response is sent.
static void IRAM_ATTR deferred_reboot(void* /*arg*/) {
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

esp_err_t handle_post(httpd_req_t* req) {
    char buf[640];
    esp_err_t err_resp = ESP_OK;
    StaticJsonDocument<640> doc;
    if (!ApiUtils::read_json(req, buf, sizeof(buf), doc, &err_resp)) {
        return err_resp;
    }

    // --- WiFi credentials ---
    const char* ssid = doc["ssid"] | "";
    if (!ssid || ssid[0] == '\0') {
        return ApiUtils::send_err(req, "SSID is required");
    }

    // Password: if blank in request, keep the current NVS value.
    const char* password_in = doc["password"] | "";
    const char* password = (password_in[0] != '\0') ? password_in : ReceiverNetworkConfig::getPassword();

    const char* hostname = doc["hostname"] | ReceiverNetworkConfig::getHostname();

    // --- Static IP ---
    const bool use_static = doc["use_static_ip"].as<bool>();
    uint8_t ip[4] = {}, gw[4] = {}, sn[4] = {}, dns1[4] = {}, dns2[4] = {};
    if (use_static) {
        ApiUtils::parse_ipv4(doc["static_ip"]    | "0.0.0.0",      ip);
        ApiUtils::parse_ipv4(doc["gateway"]       | "0.0.0.0",      gw);
        ApiUtils::parse_ipv4(doc["subnet"]        | "255.255.255.0", sn);
        ApiUtils::parse_ipv4(doc["dns_primary"]   | "8.8.8.8",      dns1);
        ApiUtils::parse_ipv4(doc["dns_secondary"] | "8.8.4.4",      dns2);
    }

    // --- MQTT ---
    const bool mqtt_en = doc["mqtt_enabled"].as<bool>();
    uint8_t mqtt_ip[4] = {};
    const uint16_t mqtt_port = static_cast<uint16_t>(doc["mqtt_port"] | 1883);
    const char* mqtt_user = doc["mqtt_username"] | "";
    const char* mqtt_pass_in = doc["mqtt_password"] | "";
    const char* mqtt_pass = (mqtt_pass_in[0] != '\0') ? mqtt_pass_in : ReceiverNetworkConfig::getMqttPassword();
    if (mqtt_en) {
        ApiUtils::parse_ipv4(doc["mqtt_server"] | "0.0.0.0", mqtt_ip);
    }

    // saveConfig() writes everything to NVS in one operation.
    const bool ok = ReceiverNetworkConfig::saveConfig(
        hostname, ssid, password,
        use_static, ip, gw, sn, dns1, dns2,
        mqtt_en, mqtt_ip, mqtt_port, mqtt_user, mqtt_pass
    );

    if (!ok) {
        return ApiUtils::send_err(req, "Failed to save config (validation error)");
    }

    LOG_INFO("WEBSERVER", "Network config saved, rebooting in 500ms");
    ApiUtils::send_ok(req);

    // Fire-and-forget task to reboot after response flush.
    xTaskCreate(deferred_reboot, "reboot", 1024, nullptr, 1, nullptr);
    return ESP_OK;
}

esp_err_t register_handlers(httpd_handle_t server) {
    httpd_uri_t get_uri = {
        .uri      = "/api/v1/network",
        .method   = HTTP_GET,
        .handler  = handle_get,
        .user_ctx = nullptr,
    };
    httpd_uri_t post_uri = {
        .uri      = "/api/v1/network",
        .method   = HTTP_POST,
        .handler  = handle_post,
        .user_ctx = nullptr,
    };
    const esp_err_t g = httpd_register_uri_handler(server, &get_uri);
    const esp_err_t p = httpd_register_uri_handler(server, &post_uri);
    return (g == ESP_OK && p == ESP_OK) ? ESP_OK : ESP_FAIL;
}

}  // namespace ApiNetwork
