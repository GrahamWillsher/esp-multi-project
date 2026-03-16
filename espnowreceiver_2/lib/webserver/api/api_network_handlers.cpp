#include "api_network_handlers.h"

#include "../utils/transmitter_manager.h"
#include "../utils/http_json_utils.h"
#include "../logging.h"
#include "../../receiver_config/receiver_config_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp32common/espnow/common.h>
#include <cstring>

static bool parse_ip_string(const char* ip_str, uint8_t out[4]) {
    if (!ip_str || ip_str[0] == '\0') {
        return false;
    }

    int a = 0, b = 0, c = 0, d = 0;
    if (sscanf(ip_str, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        return false;
    }

    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) {
        return false;
    }

    out[0] = static_cast<uint8_t>(a);
    out[1] = static_cast<uint8_t>(b);
    out[2] = static_cast<uint8_t>(c);
    out[3] = static_cast<uint8_t>(d);
    return true;
}

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
    char json[256];
    char buf[512];

    LOG_INFO("API: save_receiver_network called, content_len=%d", req->content_len);

    const char* read_error = nullptr;
    if (!HttpJsonUtils::read_request_body(req, buf, sizeof(buf), nullptr, &read_error)) {
        return HttpJsonUtils::send_json_error(req, read_error);
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"JSON parse error\"}");
        return HttpJsonUtils::send_json(req, json);
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
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"SSID is required\"}");
        return HttpJsonUtils::send_json(req, json);
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

        if (!parse_ip_string(ip_str, ip) || !parse_ip_string(gateway_str, gateway) || !parse_ip_string(subnet_str, subnet)) {
            snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Invalid static IP configuration\"}");
            return HttpJsonUtils::send_json(req, json);
        }

        if (!parse_ip_string(dns1_str, dns_primary)) {
            dns_primary[0] = 8; dns_primary[1] = 8; dns_primary[2] = 8; dns_primary[3] = 8;
        }

        if (!parse_ip_string(dns2_str, dns_secondary)) {
            dns_secondary[0] = 8; dns_secondary[1] = 8; dns_secondary[2] = 4; dns_secondary[3] = 4;
        }
    }

    if (mqtt_enabled && mqtt_server_str && mqtt_server_str[0] != '\0') {
        if (!parse_ip_string(mqtt_server_str, mqtt_server)) {
            snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Invalid MQTT server IP\"}");
            return HttpJsonUtils::send_json(req, json);
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
        snprintf(json, sizeof(json), "{\"success\":true,\"message\":\"Receiver network config saved\"}");
    } else {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Failed to save receiver config\"}");
    }

    return HttpJsonUtils::send_json(req, json);
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
    Serial.println("\n===== API SAVE NETWORK CONFIG CALLED =====");

    char json[256];
    char buf[512];

    LOG_INFO("API: save_network_config called, content_len=%d", req->content_len);

    const char* read_error = nullptr;
    if (!HttpJsonUtils::read_request_body(req, buf, sizeof(buf), nullptr, &read_error)) {
        return HttpJsonUtils::send_json_error(req, read_error);
    }

    Serial.printf("Received JSON: %s\n", buf);
    LOG_INFO("API: Received network config JSON: %s", buf);

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"JSON parse error\"}");
        return HttpJsonUtils::send_json(req, json);
    }

    if (!TransmitterManager::isMACKnown()) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Transmitter MAC unknown\"}");
        return HttpJsonUtils::send_json(req, json);
    }

    network_config_update_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = msg_network_config_update;

    msg.use_static_ip = doc["use_static_ip"].as<bool>() ? 1 : 0;

    if (msg.use_static_ip) {
        String ip_str = doc["ip"].as<String>();
        String gateway_str = doc["gateway"].as<String>();
        String subnet_str = doc["subnet"].as<String>();
        String dns1_str = doc.containsKey("dns_primary") ? doc["dns_primary"].as<String>() : "8.8.8.8";
        String dns2_str = doc.containsKey("dns_secondary") ? doc["dns_secondary"].as<String>() : "8.8.4.4";

        sscanf(ip_str.c_str(), "%hhu.%hhu.%hhu.%hhu",
               &msg.ip[0], &msg.ip[1], &msg.ip[2], &msg.ip[3]);
        sscanf(gateway_str.c_str(), "%hhu.%hhu.%hhu.%hhu",
               &msg.gateway[0], &msg.gateway[1], &msg.gateway[2], &msg.gateway[3]);
        sscanf(subnet_str.c_str(), "%hhu.%hhu.%hhu.%hhu",
               &msg.subnet[0], &msg.subnet[1], &msg.subnet[2], &msg.subnet[3]);
        sscanf(dns1_str.c_str(), "%hhu.%hhu.%hhu.%hhu",
               &msg.dns_primary[0], &msg.dns_primary[1], &msg.dns_primary[2], &msg.dns_primary[3]);
        sscanf(dns2_str.c_str(), "%hhu.%hhu.%hhu.%hhu",
               &msg.dns_secondary[0], &msg.dns_secondary[1], &msg.dns_secondary[2], &msg.dns_secondary[3]);

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
        snprintf(json, sizeof(json),
                 "{\"success\":true,\"message\":\"Network config sent - awaiting transmitter response\"}");
    } else {
        LOG_ERROR("API: ✗ ESP-NOW send FAILED: %s", esp_err_to_name(result));
        snprintf(json, sizeof(json),
                 "{\"success\":false,\"message\":\"ESP-NOW send failed: %s\"}",
                 esp_err_to_name(result));
    }

    return HttpJsonUtils::send_json(req, json);
}

esp_err_t api_get_mqtt_config_handler(httpd_req_t *req) {
    Serial.println("\n===== API GET MQTT CONFIG CALLED =====");
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
    Serial.println("\n===== API SAVE MQTT CONFIG CALLED =====");

    char json[256];
    char buf[512];

    LOG_INFO("API: save_mqtt_config called, content_len=%d", req->content_len);

    const char* read_error = nullptr;
    if (!HttpJsonUtils::read_request_body(req, buf, sizeof(buf), nullptr, &read_error)) {
        return HttpJsonUtils::send_json_error(req, read_error);
    }

    Serial.printf("Received JSON: %s\n", buf);
    LOG_INFO("API: Received MQTT config JSON: %s", buf);

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"JSON parse error\"}");
        return HttpJsonUtils::send_json(req, json);
    }

    if (!TransmitterManager::isMACKnown()) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Transmitter MAC unknown\"}");
        return HttpJsonUtils::send_json(req, json);
    }

    mqtt_config_update_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = msg_mqtt_config_update;

    msg.enabled = doc["enabled"].as<bool>() ? 1 : 0;

    String server_str = doc["server"].as<String>();
    sscanf(server_str.c_str(), "%hhu.%hhu.%hhu.%hhu",
           &msg.server[0], &msg.server[1], &msg.server[2], &msg.server[3]);

    msg.port = doc["port"].as<uint16_t>();

    String username = doc.containsKey("username") ? doc["username"].as<String>() : "";
    String password = doc.containsKey("password") ? doc["password"].as<String>() : "";
    String client_id = doc.containsKey("client_id") ? doc["client_id"].as<String>() : "espnow_transmitter";

    strncpy(msg.username, username.c_str(), sizeof(msg.username) - 1);
    strncpy(msg.password, password.c_str(), sizeof(msg.password) - 1);
    strncpy(msg.client_id, client_id.c_str(), sizeof(msg.client_id) - 1);

    msg.config_version = 0;
    msg.checksum = 0;

    LOG_INFO("API: Sending MQTT config: %s, %d.%d.%d.%d:%d",
             msg.enabled ? "ENABLED" : "DISABLED",
             msg.server[0], msg.server[1], msg.server[2], msg.server[3],
             msg.port);

    esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&msg, sizeof(msg));
    if (result == ESP_OK) {
        LOG_INFO("API: ✓ MQTT config sent to transmitter");
        snprintf(json, sizeof(json),
                 "{\"success\":true,\"message\":\"MQTT config sent - awaiting transmitter response\"}");
    } else {
        LOG_ERROR("API: ✗ ESP-NOW send FAILED: %s", esp_err_to_name(result));
        snprintf(json, sizeof(json),
                 "{\"success\":false,\"message\":\"ESP-NOW send failed: %s\"}",
                 esp_err_to_name(result));
    }

    return HttpJsonUtils::send_json(req, json);
}
