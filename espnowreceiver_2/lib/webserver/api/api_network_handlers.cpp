#include "api_network_handlers.h"

#include "api_request_utils.h"
#include "api_response_utils.h"
#include "../utils/transmitter_manager.h"
#include "../logging.h"
#include "../../receiver_config/receiver_config_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp32common/espnow/common.h>
#include <cstring>

esp_err_t api_get_receiver_network_handler(httpd_req_t *req) {
    String wifi_mac = WiFi.macAddress();
    String ssid = WiFi.SSID();
    int channel = WiFi.channel();
    bool is_ap_mode = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);

    String chip_model = ESP.getChipModel();
    uint8_t chip_revision = ESP.getChipRevision();

    const char* hostname = ReceiverNetworkConfig::getHostname();
    const char* configured_ssid = ReceiverNetworkConfig::getSSID();
    const char* configured_password = ReceiverNetworkConfig::getPassword();
    bool use_static_ip = ReceiverNetworkConfig::useStaticIP();
    const uint8_t* static_ip = ReceiverNetworkConfig::getStaticIP();
    const uint8_t* gateway = ReceiverNetworkConfig::getGateway();
    const uint8_t* subnet = ReceiverNetworkConfig::getSubnet();
    const uint8_t* dns_primary = ReceiverNetworkConfig::getDNSPrimary();
    const uint8_t* dns_secondary = ReceiverNetworkConfig::getDNSSecondary();

    bool mqtt_enabled = ReceiverNetworkConfig::isMqttEnabled();
    const uint8_t* mqtt_server = ReceiverNetworkConfig::getMqttServer();
    uint16_t mqtt_port = ReceiverNetworkConfig::getMqttPort();
    const char* mqtt_username = ReceiverNetworkConfig::getMqttUsername();

    char ip_str[16], gw_str[16], sn_str[16], dns1_str[16], dns2_str[16], mqtt_str[16];
    ApiResponseUtils::format_ipv4(ip_str, static_ip);
    ApiResponseUtils::format_ipv4(gw_str, gateway);
    ApiResponseUtils::format_ipv4(sn_str, subnet);
    ApiResponseUtils::format_ipv4(dns1_str, dns_primary);
    ApiResponseUtils::format_ipv4(dns2_str, dns_secondary);
    ApiResponseUtils::format_ipv4(mqtt_str, mqtt_server);

    StaticJsonDocument<512> doc;
    doc["success"]        = true;
    doc["is_ap_mode"]     = is_ap_mode;
    doc["wifi_mac"]       = wifi_mac.c_str();
    doc["chip_model"]     = chip_model.c_str();
    doc["chip_revision"]  = chip_revision;
    doc["hostname"]       = hostname;
    doc["ssid"]           = configured_ssid[0] ? configured_ssid : ssid.c_str();
    doc["password"]       = configured_password;
    doc["channel"]        = channel;
    doc["use_static_ip"]  = use_static_ip;
    doc["static_ip"]      = ip_str;
    doc["gateway"]        = gw_str;
    doc["subnet"]         = sn_str;
    doc["dns_primary"]    = dns1_str;
    doc["dns_secondary"]  = dns2_str;
    doc["mqtt_enabled"]   = mqtt_enabled;
    doc["mqtt_server"]    = mqtt_str;
    doc["mqtt_port"]      = mqtt_port;
    doc["mqtt_username"]  = mqtt_username;
    doc["mqtt_password"]  = "********";

    return ApiResponseUtils::send_json_doc(req, doc);
}

esp_err_t api_save_receiver_network_handler(httpd_req_t *req) {
    char buf[512];
    static const uint8_t kDefaultDnsPrimary[4] = {8, 8, 8, 8};
    static const uint8_t kDefaultDnsSecondary[4] = {8, 8, 4, 4};

    LOG_INFO("API", "save_receiver_network called, content_len=%d", req->content_len);

    StaticJsonDocument<512> doc;
    esp_err_t response_error = ESP_OK;
    if (!ApiRequestUtils::read_json_body_or_respond(req, buf, sizeof(buf), doc, &response_error)) {
        return response_error;
    }

    const char* hostname = doc["hostname"] | "";
    const char* ssid = doc["ssid"] | "";
    const char* password = doc["password"] | "";
    bool use_static_ip = doc["use_static_ip"].as<bool>();

    bool mqtt_enabled = doc["mqtt_enabled"].as<bool>();
    const char* mqtt_server_str = doc["mqtt_server"] | "";
    uint16_t mqtt_port = doc["mqtt_port"] | 1883;
    const char* mqtt_username = doc["mqtt_username"] | "";
    const char* mqtt_password = doc["mqtt_password"] | "";

    if (!ssid || ssid[0] == '\0') {
        return ApiResponseUtils::send_error_message(req, "SSID is required");
    }

    const char* password_to_save = password;
    if (!password || password[0] == '\0') {
        password_to_save = ReceiverNetworkConfig::getPassword();
    }

    uint8_t ip[4] = {0};
    uint8_t gateway[4] = {0};
    uint8_t subnet[4] = {0};
    uint8_t dns_primary[4] = {8, 8, 8, 8};
    uint8_t dns_secondary[4] = {8, 8, 4, 4};
    uint8_t mqtt_server[4] = {0};

    if (use_static_ip) {
        const char* ip_str = doc["ip"] | "";
        const char* gateway_str = doc["gateway"] | "";
        const char* subnet_str = doc["subnet"] | "";
        const char* dns1_str = doc["dns_primary"] | "";
        const char* dns2_str = doc["dns_secondary"] | "";

        if (!ApiRequestUtils::parse_ipv4(ip_str, ip) ||
            !ApiRequestUtils::parse_ipv4(gateway_str, gateway) ||
            !ApiRequestUtils::parse_ipv4(subnet_str, subnet)) {
            return ApiResponseUtils::send_error_message(req, "Invalid static IP configuration");
        }

        ApiRequestUtils::parse_ipv4_or_default(dns1_str, dns_primary, kDefaultDnsPrimary);
        ApiRequestUtils::parse_ipv4_or_default(dns2_str, dns_secondary, kDefaultDnsSecondary);
    }

    if (mqtt_enabled && mqtt_server_str && mqtt_server_str[0] != '\0') {
        if (!ApiRequestUtils::parse_ipv4(mqtt_server_str, mqtt_server)) {
            return ApiResponseUtils::send_error_message(req, "Invalid MQTT server IP");
        }
    }

    bool saved = ReceiverNetworkConfig::saveConfig(
        hostname,
        ssid,
        password_to_save,
        use_static_ip,
        ip,
        gateway,
        subnet,
        dns_primary,
        dns_secondary,
        mqtt_enabled,
        mqtt_server,
        mqtt_port,
        mqtt_username,
        mqtt_password
    );

    if (saved) {
        return ApiResponseUtils::send_success_message(req, "Receiver network config saved");
    } else {
        return ApiResponseUtils::send_error_message(req, "Failed to save receiver config");
    }
}

esp_err_t api_get_network_config_handler(httpd_req_t *req) {
    if (!TransmitterManager::isIPKnown()) {
        return ApiResponseUtils::send_error_message(req, "No network config cached yet");
    }

    bool is_static = TransmitterManager::isStaticIP();
    uint32_t version = TransmitterManager::getNetworkConfigVersion();

    const uint8_t* current_ip = TransmitterManager::getIP();
    const uint8_t* current_gateway = TransmitterManager::getGateway();
    const uint8_t* current_subnet = TransmitterManager::getSubnet();

    const uint8_t* static_ip = TransmitterManager::getStaticIP();
    const uint8_t* static_gateway = TransmitterManager::getStaticGateway();
    const uint8_t* static_subnet = TransmitterManager::getStaticSubnet();
    const uint8_t* static_dns1 = TransmitterManager::getStaticDNSPrimary();
    const uint8_t* static_dns2 = TransmitterManager::getStaticDNSSecondary();

    if (!(current_ip && current_gateway && current_subnet &&
          static_ip && static_gateway && static_subnet && static_dns1 && static_dns2)) {
        return ApiResponseUtils::send_error_message(req, "No network data available");
    }

    char cur_ip[16], cur_gw[16], cur_sn[16];
    char st_ip[16], st_gw[16], st_sn[16], st_dns1[16], st_dns2[16];
    ApiResponseUtils::format_ipv4(cur_ip, current_ip);
    ApiResponseUtils::format_ipv4(cur_gw, current_gateway);
    ApiResponseUtils::format_ipv4(cur_sn, current_subnet);
    ApiResponseUtils::format_ipv4(st_ip, static_ip);
    ApiResponseUtils::format_ipv4(st_gw, static_gateway);
    ApiResponseUtils::format_ipv4(st_sn, static_subnet);
    ApiResponseUtils::format_ipv4(st_dns1, static_dns1);
    ApiResponseUtils::format_ipv4(st_dns2, static_dns2);

    StaticJsonDocument<384> doc;
    doc["success"]       = true;
    doc["use_static_ip"] = is_static;
    doc["config_version"] = version;

    JsonObject current = doc.createNestedObject("current");
    current["ip"]      = cur_ip;
    current["gateway"] = cur_gw;
    current["subnet"]  = cur_sn;

    JsonObject sc = doc.createNestedObject("static_config");
    sc["ip"]          = st_ip;
    sc["gateway"]     = st_gw;
    sc["subnet"]      = st_sn;
    sc["dns_primary"]   = st_dns1;
    sc["dns_secondary"] = st_dns2;

    return ApiResponseUtils::send_json_doc(req, doc);
}

esp_err_t api_save_network_config_handler(httpd_req_t *req) {
    char buf[512];
    static const uint8_t kDefaultDnsPrimary[4] = {8, 8, 8, 8};
    static const uint8_t kDefaultDnsSecondary[4] = {8, 8, 4, 4};

    LOG_INFO("API", "save_network_config called, content_len=%d", req->content_len);

    StaticJsonDocument<512> doc;
    esp_err_t response_error = ESP_OK;
    if (!ApiRequestUtils::read_json_body_or_respond(req, buf, sizeof(buf), doc, &response_error)) {
        return response_error;
    }

    LOG_INFO("API", "Received network config JSON: %s", buf);

    if (!TransmitterManager::isMACKnown()) {
        return ApiResponseUtils::send_transmitter_mac_unknown(req);
    }

    network_config_update_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = msg_network_config_update;

    msg.use_static_ip = doc["use_static_ip"].as<bool>() ? 1 : 0;

    if (msg.use_static_ip) {
          const char* ip_str = doc["ip"] | "";
          const char* gateway_str = doc["gateway"] | "";
          const char* subnet_str = doc["subnet"] | "";
          const char* dns1_str = doc["dns_primary"] | "8.8.8.8";
          const char* dns2_str = doc["dns_secondary"] | "8.8.4.4";

          if (!ApiRequestUtils::parse_ipv4(ip_str, msg.ip) ||
            !ApiRequestUtils::parse_ipv4(gateway_str, msg.gateway) ||
            !ApiRequestUtils::parse_ipv4(subnet_str, msg.subnet)) {
            return ApiResponseUtils::send_error_message(req, "Invalid static IP configuration");
          }
          ApiRequestUtils::parse_ipv4_or_default(dns1_str, msg.dns_primary, kDefaultDnsPrimary);
          ApiRequestUtils::parse_ipv4_or_default(dns2_str, msg.dns_secondary, kDefaultDnsSecondary);

        LOG_INFO("API", "Sending static IP config: %d.%d.%d.%d",
                 msg.ip[0], msg.ip[1], msg.ip[2], msg.ip[3]);
    } else {
        LOG_INFO("API", "Sending DHCP mode config");
    }

    msg.config_version = 0;
    msg.checksum = 0;

    esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&msg, sizeof(msg));
    if (result == ESP_OK) {
        LOG_INFO("API", "✓ Network config sent to transmitter");
    } else {
        LOG_ERROR("API", "✗ ESP-NOW send FAILED: %s", esp_err_to_name(result));
    }
    return ApiResponseUtils::send_espnow_send_result(req, result, "Network config sent - awaiting transmitter response");
}

esp_err_t api_get_mqtt_config_handler(httpd_req_t *req) {
    if (!TransmitterManager::isMqttConfigKnown()) {
        LOG_INFO("API", "MQTT config not cached");
        return ApiResponseUtils::send_error_message(req, "MQTT config not cached");
    }

    const uint8_t* server = TransmitterManager::getMqttServer();
    char server_str[16];
    ApiResponseUtils::format_ipv4(server_str, server);

    StaticJsonDocument<256> doc;
    doc["success"]    = true;
    doc["enabled"]    = TransmitterManager::isMqttEnabled();
    doc["server"]     = server_str;
    doc["port"]       = TransmitterManager::getMqttPort();
    doc["username"]   = TransmitterManager::getMqttUsername();
    doc["password"]   = "********";
    doc["client_id"]  = TransmitterManager::getMqttClientId();
    doc["connected"]  = TransmitterManager::isMqttConnected();

    LOG_INFO("API", "✓ Returning cached MQTT config (enabled=%d, connected=%d)",
             TransmitterManager::isMqttEnabled(), TransmitterManager::isMqttConnected());

    return ApiResponseUtils::send_json_doc(req, doc);
}

esp_err_t api_save_mqtt_config_handler(httpd_req_t *req) {
    char buf[512];

    LOG_INFO("API", "save_mqtt_config called, content_len=%d", req->content_len);

    StaticJsonDocument<512> doc;
    esp_err_t response_error = ESP_OK;
    if (!ApiRequestUtils::read_json_body_or_respond(req, buf, sizeof(buf), doc, &response_error)) {
        return response_error;
    }

    LOG_INFO("API", "Received MQTT config JSON: %s", buf);

    if (!TransmitterManager::isMACKnown()) {
        return ApiResponseUtils::send_transmitter_mac_unknown(req);
    }

    mqtt_config_update_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = msg_mqtt_config_update;

    msg.enabled = doc["enabled"].as<bool>() ? 1 : 0;

    const char* server_str = doc["server"] | "";
    if (server_str[0] == '\0') {
        if (msg.enabled) {
            return ApiResponseUtils::send_error_message(req, "MQTT server IP is required");
        }
    } else if (!ApiRequestUtils::parse_ipv4(server_str, msg.server)) {
        return ApiResponseUtils::send_error_message(req, "Invalid MQTT server IP");
    }

    msg.port = doc["port"].as<uint16_t>();

    const char* username = doc["username"] | "";
    const char* password = doc["password"] | "";
    const char* client_id = doc["client_id"] | "espnow_transmitter";

    strncpy(msg.username, username, sizeof(msg.username) - 1);
    strncpy(msg.password, password, sizeof(msg.password) - 1);
    strncpy(msg.client_id, client_id, sizeof(msg.client_id) - 1);

    msg.config_version = 0;
    msg.checksum = 0;

    LOG_INFO("API", "Sending MQTT config: %s, %d.%d.%d.%d:%d",
             msg.enabled ? "ENABLED" : "DISABLED",
             msg.server[0], msg.server[1], msg.server[2], msg.server[3],
             msg.port);

    esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&msg, sizeof(msg));
    if (result == ESP_OK) {
        LOG_INFO("API", "✓ MQTT config sent to transmitter");
    } else {
        LOG_ERROR("API", "✗ ESP-NOW send FAILED: %s", esp_err_to_name(result));
    }
    return ApiResponseUtils::send_espnow_send_result(req, result, "MQTT config sent - awaiting transmitter response");
}
