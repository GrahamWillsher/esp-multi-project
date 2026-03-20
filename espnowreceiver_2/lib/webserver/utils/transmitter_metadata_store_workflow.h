#ifndef TRANSMITTER_METADATA_STORE_WORKFLOW_H
#define TRANSMITTER_METADATA_STORE_WORKFLOW_H

#include <stdint.h>

namespace TransmitterMetadataStoreWorkflow {

void store_metadata(bool valid,
                    const char* env,
                    const char* device,
                    uint8_t major,
                    uint8_t minor,
                    uint8_t patch,
                    const char* build_date_str);

} // namespace TransmitterMetadataStoreWorkflow

#endif // TRANSMITTER_METADATA_STORE_WORKFLOW_H
