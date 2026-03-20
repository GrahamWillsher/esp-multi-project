#include "transmitter_metadata_query_helper.h"
#include "transmitter_status_cache.h"

namespace TransmitterMetadataQueryHelper {
    bool has_metadata() {
        return TransmitterStatusCache::has_metadata();
    }

    bool is_metadata_valid() {
        return TransmitterStatusCache::is_metadata_valid();
    }

    const char* get_metadata_env() {
        return TransmitterStatusCache::get_metadata_env();
    }

    const char* get_metadata_device() {
        return TransmitterStatusCache::get_metadata_device();
    }

    void get_metadata_version(uint8_t& major, uint8_t& minor, uint8_t& patch) {
        TransmitterStatusCache::get_metadata_version(major, minor, patch);
    }

    uint32_t get_metadata_version_number() {
        return TransmitterStatusCache::get_metadata_version_number();
    }

    const char* get_metadata_build_date() {
        return TransmitterStatusCache::get_metadata_build_date();
    }
}
