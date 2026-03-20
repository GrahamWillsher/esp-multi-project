#include "api_network_handlers.h"

#include "api_request_utils.h"
#include "api_response_utils.h"
#include "../utils/transmitter_manager.h"
#include <webserver_common_utils/http_json_utils.h>
#include "../logging.h"
#include "../../receiver_config/receiver_config_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp32common/espnow/common.h>
#include <cstring>

esp_err_t api_get_receiver_network_handler(httpd_req_t *req) {
    char json[1024];

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

    snprintf(json, sizeof(json),
        "{"
        "\"success\":true,"
        "\"is_ap_mode\":%s,"
        "\"wifi_mac\":\"%s\","
        "\"chip_model\":\"%s\","
        "\"chip_revision\":%d,"
        "\"hostname\":\"%s\","
        "\"ssid\":\"%s\","
        "\"password\":\"%s\","
        "\"channel\":%d,"
        "\"use_static_ip\":%s,"
        "\"static_ip\":\"%d.%d.%d.%d\","
        "\"gateway\":\"%d.%d.%d.%d\","
        "\"subnet\":\"%d.%d.%d.%d\","
        "\"dns_primary\":\"%d.%d.%d.%d\","
        "\"dns_secondary\":\"%d.%d.%d.%d\","
        "\"mqtt_enabled\":%s,"
        "\"mqtt_server\":\"%d.%d.%d.%d\","
        "\"mqtt_port\":%d,"
        "\"mqtt_username\":\"%s\","
        "\"mqtt_password\":\"%s\""
        "}",
        is_ap_mode ? "true" : "false",
        wifi_mac.c_str(),
        chip_model.c_str(),
        chip_revision,
        hostname,
        configured_ssid[0] ? configured_ssid : ssid.c_str(),
        configured_password,
        channel,
        use_static_ip ? "true" : "false",
        static_ip[0], static_ip[1], static_ip[2], static_ip[3],
        gateway[0], gateway[1], gateway[2], gateway[3],
        subnet[0], subnet[1], subnet[2], subnet[3],
        dns_primary[0], dns_primary[1], dns_primary[2], dns_primary[3],
        dns_secondary[0], dns_secondary[1], dns_secondary[2], dns_secondary[3],
        mqtt_enabled ? "true" : "false",
        mqtt_server[0], mqtt_server[1], mqtt_server[2], mqtt_server[3],
        mqtt_port,
        mqtt_username,
        "********"
    );

    return HttpJsonUtils::send_json(req, json);
}

esp_err_t api_save_receiver_network_handler(httpd_req_t *req) {
    char buf[512];
    static const uint8_t kDefaultDnsPrimary[4] = {8, 8, 8, 8};
    static const uint8_t kDefaultDnsSecondary[4] = {8, 8, 4, 4};

    LOG_INFO("API: save_receiver_network called, content_len=%d", req->content_len);

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
    char json[1024];

    if (!TransmitterManager::isIPKnown()) {
        snprintf(json, sizeof(json),
            "{"
            "\"success\":false,"
            "\"message\":\"No network config cached yet\""
            "}"
        );
        return HttpJsonUtils::send_json(req, json);
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

    if (current_ip && current_gateway && current_subnet &&
        static_ip && static_gateway && static_subnet && static_dns1 && static_dns2) {
        snprintf(json, sizeof(json),
            "{"
            "\"success\":true,"
            "\"use_static_ip\":%s,"
            "\"current\":{"
                "\"ip\":\"%d.%d.%d.%d\","
                "\"gateway\":\"%d.%d.%d.%d\","
                "\"subnet\":\"%d.%d.%d.%d\""
            "},"
            "\"static_config\":{"
                "\"ip\":\"%d.%d.%d.%d\","
                "\"gateway\":\"%d.%d.%d.%d\","
                "\"subnet\":\"%d.%d.%d.%d\","
                "\"dns_primary\":\"%d.%d.%d.%d\","
                "\"dns_secondary\":\"%d.%d.%d.%d\""
            "},"
            "\"config_version\":%u"
            "}",
            is_static ? "true" : "false",
            current_ip[0], current_ip[1], current_ip[2], current_ip[3],
            current_gateway[0], current_gateway[1], current_gateway[2], current_gateway[3],
            current_subnet[0], current_subnet[1], current_subnet[2], current_subnet[3],
            static_ip[0], static_ip[1], static_ip[2], static_ip[3],
            static_gateway[0], static_gateway[1], static_gateway[2], static_gateway[3],
            static_subnet[0], static_subnet[1], static_subnet[2], static_subnet[3],
            static_dns1[0], static_dns1[1], static_dns1[2], static_dns1[3],
            static_dns2[0], static_dns2[1], static_dns2[2], static_dns2[3],
            version
        );
    } else {
        snprintf(json, sizeof(json),
            "{"
            "\"success\":false,"
            "\"message\":\"No network data available\""
            "}"
        );
    }

    return HttpJsonUtils::send_json(req, json);
}

esp_err_t api_save_network_config_handler(httpd_req_t *req) {
    char buf[512];
    static const uint8_t kDefaultDnsPrimary[4] = {8, 8, 8, 8};
    static const uint8_t kDefaultDnsSecondary[4] = {8, 8, 4, 4};

    LOG_INFO("API: save_network_config called, content_len=%d", req->content_len);

    StaticJsonDocument<512> doc;
    esp_err_t response_error = ESP_OK;
    if (!ApiRequestUtils::read_json_body_or_respond(req, buf, sizeof(buf), doc, &response_error)) {
        return response_error;
    }

    LOG_INFO("API: Received network config JSON: %s", buf);

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

        LOG_INFO("API: Sending static IP config: %d.%d.%d.%d",
                 msg.ip[0], msg.ip[1], msg.ip[2], msg.ip[3]);
    } else {
        LOG_INFO("API: Sending DHCP mode config");
    }

    msg.config_version = 0;
    msg.checksum = 0;

    esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&msg, sizeof(msg));
    if (result == ESP_OK) {
        LOG_INFO("API: ✓ Network config sent to transmitter");
        return ApiResponseUtils::send_success_message(req, "Network config sent - awaiting transmitter response");
    } else {
        LOG_ERROR("API: ✗ ESP-NOW send FAILED: %s", esp_err_to_name(result));
        return ApiResponseUtils::send_jsonf(req,
                                            "{\"success\":false,\"message\":\"ESP-NOW send failed: %s\"}",
                                            esp_err_to_name(result));
    }
}

esp_err_t api_get_mqtt_config_handler(httpd_req_t *req) {
    char json[512];

    if (!TransmitterManager::isMqttConfigKnown()) {
        LOG_INFO("API: MQTT config not cached");
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"MQTT config not cached\"}");
        return HttpJsonUtils::send_json(req, json);
    }

    const uint8_t* server = TransmitterManager::getMqttServer();
    snprintf(json, sizeof(json),
        "{\"success\":true,"
        "\"enabled\":%s,"
        "\"server\":\"%d.%d.%d.%d\","
        "\"port\":%d,"
        "\"username\":\"%s\","
        "\"password\":\"********\","
        "\"client_id\":\"%s\","
        "\"connected\":%s}",
        TransmitterManager::isMqttEnabled() ? "true" : "false",
        server[0], server[1], server[2], server[3],
        TransmitterManager::getMqttPort(),
        TransmitterManager::getMqttUsername(),
        TransmitterManager::getMqttClientId(),
        TransmitterManager::isMqttConnected() ? "true" : "false"
    );

    LOG_INFO("API: ✓ Returning cached MQTT config (enabled=%d, connected=%d)",
             TransmitterManager::isMqttEnabled(), TransmitterManager::isMqttConnected());

    return HttpJsonUtils::send_json(req, json);
}

esp_err_t api_save_mqtt_config_handler(httpd_req_t *req) {
    char buf[512];

    LOG_INFO("API: save_mqtt_config called, content_len=%d", req->content_len);

    StaticJsonDocument<512> doc;
    esp_err_t response_error = ESP_OK;
    if (!ApiRequestUtils::read_json_body_or_respond(req, buf, sizeof(buf), doc, &response_error)) {
        return response_error;
    }

    LOG_INFO("API: Received MQTT config JSON: %s", buf);

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

    LOG_INFO("API: Sending MQTT config: %s, %d.%d.%d.%d:%d",
             msg.enabled ? "ENABLED" : "DISABLED",
             msg.server[0], msg.server[1], msg.server[2], msg.server[3],
             msg.port);

    esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&msg, sizeof(msg));
    if (result == ESP_OK) {
        LOG_INFO("API: ✓ MQTT config sent to transmitter");
        return ApiResponseUtils::send_success_message(req, "MQTT config sent - awaiting transmitter response");
    } else {
        LOG_ERROR("API: ✗ ESP-NOW send FAILED: %s", esp_err_to_name(result));
        return ApiResponseUtils::send_jsonf(req,
                                            "{\"success\":false,\"message\":\"ESP-NOW send failed: %s\"}",
                                            esp_err_to_name(result));
    }
}
