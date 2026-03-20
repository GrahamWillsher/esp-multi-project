#include "transmitter_metadata_store_workflow.h"

#include "transmitter_status_cache.h"
#include "transmitter_write_through.h"

namespace TransmitterMetadataStoreWorkflow {

void store_metadata(bool valid,
                    const char* env,
                    const char* device,
                    uint8_t major,
                    uint8_t minor,
                    uint8_t patch,
                    const char* build_date_str) {
    TransmitterStatusCache::store_metadata(valid, env, device, major, minor, patch, build_date_str);
    TransmitterWriteThrough::persist_to_nvs();
}

} // namespace TransmitterMetadataStoreWorkflow
