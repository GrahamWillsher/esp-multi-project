#pragma once

#include <espnow_transmitter.h>

namespace TxRequestDataHandlers {

void handle_request_data(const espnow_queue_msg_t& msg);
void handle_abort_data(const espnow_queue_msg_t& msg);

} // namespace TxRequestDataHandlers
