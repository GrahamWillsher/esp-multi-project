#ifndef TRANSMITTER_EVENT_LOG_TYPES_H
#define TRANSMITTER_EVENT_LOG_TYPES_H

#include <stdint.h>

namespace TransmitterEventLogTypes {

struct EventLogEntry {
    uint32_t timestamp;
    uint8_t level;
    int32_t data;
    char message[96];
};

} // namespace TransmitterEventLogTypes

#endif // TRANSMITTER_EVENT_LOG_TYPES_H
