#include "firmware_compatibility_policy.h"

#include <string.h>
#include <cctype>

namespace {

inline bool is_space_char(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void normalize_upper_trim(char* s) {
    if (!s) {
        return;
    }

    size_t len = strlen(s);
    while (len > 0 && is_space_char(s[len - 1])) {
        s[--len] = '\0';
    }

    size_t start = 0;
    while (s[start] != '\0' && is_space_char(s[start])) {
        ++start;
    }

    if (start > 0) {
        memmove(s, s + start, strlen(s + start) + 1);
    }

    for (size_t i = 0; s[i] != '\0'; ++i) {
        s[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
    }
}

void normalize_expected_device_type(const char* expected_device_type, char* out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }

    out[0] = '\0';
    if (!expected_device_type || expected_device_type[0] == '\0') {
        return;
    }

    strncpy(out, expected_device_type, out_len - 1);
    out[out_len - 1] = '\0';
    normalize_upper_trim(out);
}

}  // namespace

namespace FirmwareCompatibilityPolicy {

void MetadataScan::reset() {
    memset(this, 0, sizeof(*this));
}

void MetadataScan::consume(const uint8_t* data, size_t len) {
    if (!data || len == 0 || found) {
        return;
    }

    for (size_t idx = 0; idx < len; ++idx) {
        const uint8_t b = data[idx];

        if (window_len < sizeof(window)) {
            window[window_len++] = b;
        } else {
            memmove(window, window + 1, sizeof(window) - 1);
            window[sizeof(window) - 1] = b;
        }

        if (window_len < sizeof(window)) {
            continue;
        }

        const bool magic_start =
            (window[0] == 0x41 && window[1] == 0x54 && window[2] == 0x4D && window[3] == 0x46);
        if (!magic_start) {
            continue;
        }

        const bool magic_end =
            (window[124] == 0x46 && window[125] == 0x44 && window[126] == 0x4E && window[127] == 0x45);

        found = true;
        valid = magic_end;
        if (!valid) {
            return;
        }

        memcpy(device_type, &window[36], 16);
        device_type[16] = '\0';
        normalize_upper_trim(device_type);
        version_major = window[52];
        min_compatible_major = window[55];  // Optional field (0 == unspecified/legacy)
        return;
    }
}

ValidationResult validate_scan(const MetadataScan& scan,
                               const char* expected_device_type,
                               uint8_t running_major) {
    ValidationResult result;
    result.metadata_found = scan.found;
    result.metadata_valid = scan.valid;
    result.image_major = scan.version_major;
    result.image_min_compatible_major = scan.min_compatible_major;
    strncpy(result.normalized_device_type, scan.device_type, sizeof(result.normalized_device_type) - 1);
    result.normalized_device_type[sizeof(result.normalized_device_type) - 1] = '\0';

    if (!scan.found) {
        result.allowed = true;
        result.code = ValidationCode::LegacyAllowed;
        result.message = "No embedded firmware metadata found; allowing legacy image";
        return result;
    }

    if (!scan.valid) {
        result.allowed = false;
        result.code = ValidationCode::InvalidMetadataStructure;
        result.message = "Invalid firmware metadata structure";
        return result;
    }

    char expected_norm[17] = {0};
    normalize_expected_device_type(expected_device_type, expected_norm, sizeof(expected_norm));
    if (expected_norm[0] != '\0' && strcmp(scan.device_type, expected_norm) != 0) {
        result.allowed = false;
        result.code = ValidationCode::DeviceTypeMismatch;
        result.message = "Firmware target mismatch";
        return result;
    }

    if (scan.version_major != running_major) {
        result.allowed = false;
        result.code = ValidationCode::MajorVersionIncompatible;
        result.message = "Firmware major version incompatible";
        return result;
    }

    if (scan.min_compatible_major != 0 && running_major < scan.min_compatible_major) {
        result.allowed = false;
        result.code = ValidationCode::MinimumCompatibleMajorIncompatible;
        result.message = "Firmware minimum compatible major requirement not met";
        return result;
    }

    result.allowed = true;
    result.code = ValidationCode::Allowed;
    result.message = "Firmware compatibility validated";
    return result;
}

const char* validation_code_to_string(ValidationCode code) {
    switch (code) {
        case ValidationCode::Allowed:
            return "allowed";
        case ValidationCode::LegacyAllowed:
            return "legacy_allowed";
        case ValidationCode::InvalidMetadataStructure:
            return "invalid_metadata_structure";
        case ValidationCode::DeviceTypeMismatch:
            return "device_type_mismatch";
        case ValidationCode::MajorVersionIncompatible:
            return "major_version_incompatible";
        case ValidationCode::MinimumCompatibleMajorIncompatible:
            return "minimum_compatible_major_incompatible";
        default:
            return "unknown";
    }
}

}  // namespace FirmwareCompatibilityPolicy
