#ifndef TRANSMITTER_PEER_REGISTRY_H
#define TRANSMITTER_PEER_REGISTRY_H

#include <stdint.h>

namespace TransmitterPeerRegistry {

bool ensure_peer_registered(const uint8_t* mac);

} // namespace TransmitterPeerRegistry

#endif // TRANSMITTER_PEER_REGISTRY_H
