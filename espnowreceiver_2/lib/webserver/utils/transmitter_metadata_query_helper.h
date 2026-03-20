#ifndef TRANSMITTER_METADATA_QUERY_HELPER_H
#define TRANSMITTER_METADATA_QUERY_HELPER_H

#include <cstdint>

namespace TransmitterMetadataQueryHelper {
    bool has_metadata();
    bool is_metadata_valid();
    const char* get_metadata_env();
    const char* get_metadata_device();
    void get_metadata_version(uint8_t& major, uint8_t& minor, uint8_t& patch);
    uint32_t get_metadata_version_number();
    const char* get_metadata_build_date();
}

#endif
