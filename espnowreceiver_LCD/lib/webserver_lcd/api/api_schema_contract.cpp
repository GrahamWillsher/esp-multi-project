#include "api_schema_contract.h"

namespace ApiSchemaContract {
namespace {

bool require_keys(const JsonDocument& doc,
                  const char* const* keys,
                  size_t key_count,
                  const char** out_error_message) {
    for (size_t i = 0; i < key_count; ++i) {
        if (!doc.containsKey(keys[i])) {
            if (out_error_message) {
                *out_error_message = "Missing required JSON field";
            }
            return false;
        }
    }
    return true;
}

bool validate_save_setting(const JsonDocument& doc, const char** out_error_message) {
    static const char* kRequired[] = {"category", "field", "value"};
    if (!require_keys(doc, kRequired, sizeof(kRequired) / sizeof(kRequired[0]), out_error_message)) {
        return false;
    }

    if (doc["value"].isNull()) {
        if (out_error_message) {
            *out_error_message = "Field 'value' cannot be null";
        }
        return false;
    }

    return true;
}

bool validate_save_receiver_network(const JsonDocument& doc, const char** out_error_message) {
    static const char* kRequired[] = {"ssid"};
    if (!require_keys(doc, kRequired, sizeof(kRequired) / sizeof(kRequired[0]), out_error_message)) {
        return false;
    }

    const char* ssid = doc["ssid"] | "";
    if (!ssid || ssid[0] == '\0') {
        if (out_error_message) {
            *out_error_message = "SSID is required";
        }
        return false;
    }

    const bool use_static_ip = doc["use_static_ip"].as<bool>();
    if (use_static_ip) {
        static const char* kStaticRequired[] = {"ip", "gateway", "subnet"};
        if (!require_keys(doc, kStaticRequired, sizeof(kStaticRequired) / sizeof(kStaticRequired[0]), out_error_message)) {
            return false;
        }
    }

    if (doc.containsKey("power_bar_renderer_mode")) {
        const uint8_t mode = doc["power_bar_renderer_mode"].as<uint8_t>();
        if (mode > 4U) {
            if (out_error_message) {
                *out_error_message = "power_bar_renderer_mode must be in range 0-4";
            }
            return false;
        }
    }

    return true;
}

bool validate_save_network_config(const JsonDocument& doc, const char** out_error_message) {
    if (!doc.containsKey("use_static_ip")) {
        if (out_error_message) {
            *out_error_message = "Missing required JSON field";
        }
        return false;
    }

    const bool use_static_ip = doc["use_static_ip"].as<bool>();
    if (use_static_ip) {
        static const char* kStaticRequired[] = {"ip", "gateway", "subnet"};
        if (!require_keys(doc, kStaticRequired, sizeof(kStaticRequired) / sizeof(kStaticRequired[0]), out_error_message)) {
            return false;
        }
    }

    return true;
}

bool validate_save_mqtt_config(const JsonDocument& doc, const char** out_error_message) {
    static const char* kRequired[] = {"enabled", "port"};
    if (!require_keys(doc, kRequired, sizeof(kRequired) / sizeof(kRequired[0]), out_error_message)) {
        return false;
    }

    const bool enabled = doc["enabled"].as<bool>();
    if (enabled) {
        const char* server = doc["server"] | "";
        if (!server || server[0] == '\0') {
            if (out_error_message) {
                *out_error_message = "MQTT server IP is required when MQTT is enabled";
            }
            return false;
        }
    }

    return true;
}

bool validate_set_test_data_mode(const JsonDocument& doc, const char** out_error_message) {
    if (!doc.containsKey("mode")) {
        if (out_error_message) {
            *out_error_message = "Missing required JSON field";
        }
        return false;
    }

    return true;
}

}  // namespace

bool validate(const JsonDocument& doc, SchemaId schema_id, const char** out_error_message) {
    if (out_error_message) {
        *out_error_message = nullptr;
    }

    switch (schema_id) {
        case SchemaId::SaveSetting:
            return validate_save_setting(doc, out_error_message);
        case SchemaId::SaveReceiverNetwork:
            return validate_save_receiver_network(doc, out_error_message);
        case SchemaId::SaveNetworkConfig:
            return validate_save_network_config(doc, out_error_message);
        case SchemaId::SaveMqttConfig:
            return validate_save_mqtt_config(doc, out_error_message);
        case SchemaId::SetTestDataMode:
            return validate_set_test_data_mode(doc, out_error_message);
        case SchemaId::NetworkV1Post:
            return validate_save_receiver_network(doc, out_error_message);
    }

    if (out_error_message) {
        *out_error_message = "Unknown schema";
    }
    return false;
}

}  // namespace ApiSchemaContract
