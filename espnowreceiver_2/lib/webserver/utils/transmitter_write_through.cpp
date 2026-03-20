#include "transmitter_write_through.h"

#include "sse_notifier.h"
#include "transmitter_nvs_persistence.h"

namespace TransmitterWriteThrough {

void persist_to_nvs() {
    TransmitterNvsPersistence::saveToNVS();
}

void notify_and_persist() {
    SSENotifier::notifyDataUpdated();
    persist_to_nvs();
}

} // namespace TransmitterWriteThrough