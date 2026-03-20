#ifndef TRANSMITTER_RUNTIME_STATUS_UPDATE_H
#define TRANSMITTER_RUNTIME_STATUS_UPDATE_H

namespace TransmitterRuntimeStatusUpdate {

void update_runtime_status(bool mqtt_conn, bool eth_conn);

} // namespace TransmitterRuntimeStatusUpdate

#endif // TRANSMITTER_RUNTIME_STATUS_UPDATE_H