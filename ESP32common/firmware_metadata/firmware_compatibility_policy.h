#pragma once

#include <stddef.h>
#include <stdint.h>

#include "firmware_metadata.h"

namespace FirmwareCompatibilityPolicy {

constexpr size_t METADATA_BLOCK_SIZE = 128;

struct MetadataScan {
    uint8_t window[METADATA_BLOCK_SIZE] = {0};
    size_t window_len = 0;
    bool found = false;
    bool valid = false;
    char device_type[17] = {0};
    uint8_t version_major = 0;
    uint8_t min_compatible_major = 0;

    void reset();
    void consume(const uint8_t* data, size_t len);
};

enum class ValidationCode {
    Allowed = 0,
    LegacyAllowed,
    InvalidMetadataStructure,
    DeviceTypeMismatch,
    MajorVersionIncompatible,
    MinimumCompatibleMajorIncompatible,
};

struct ValidationResult {
    bool allowed = true;
    bool metadata_found = false;
    bool metadata_valid = false;
    ValidationCode code = ValidationCode::Allowed;
    char normalized_device_type[17] = {0};
    uint8_t image_major = 0;
    uint8_t image_min_compatible_major = 0;
    const char* message = "";
};

ValidationResult validate_scan(const MetadataScan& scan,
                               const char* expected_device_type,
                               uint8_t running_major);

const char* validation_code_to_string(ValidationCode code);

}  // namespace FirmwareCompatibilityPolicy
