#ifndef API_SCHEMA_CONTRACT_H
#define API_SCHEMA_CONTRACT_H

#include <ArduinoJson.h>

namespace ApiSchemaContract {

enum class SchemaId {
    SaveSetting,
    SaveReceiverNetwork,
    SaveNetworkConfig,
    SaveMqttConfig,
    SetTestDataMode,
    NetworkV1Post,
};

bool validate(const JsonDocument& doc, SchemaId schema_id, const char** out_error_message = nullptr);

}  // namespace ApiSchemaContract

#endif  // API_SCHEMA_CONTRACT_H
