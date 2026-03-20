#ifndef TRANSMITTER_WRITE_THROUGH_H
#define TRANSMITTER_WRITE_THROUGH_H

namespace TransmitterWriteThrough {

void persist_to_nvs();
void notify_and_persist();

} // namespace TransmitterWriteThrough

#endif // TRANSMITTER_WRITE_THROUGH_H