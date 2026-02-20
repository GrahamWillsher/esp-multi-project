/**
 * @file CommunicationManager.cpp
 * @brief Implementation of communication registration infrastructure
 */

#include "CommunicationManager.h"

CommunicationManager& CommunicationManager::instance() {
  static CommunicationManager instance;
  return instance;
}

void CommunicationManager::register_can_receiver(CanReceiver* receiver, CAN_Interface interface, CAN_Speed speed) {
  can_receivers_[interface] = {receiver, speed};
  // DEBUG_PRINTF("CAN receiver registered, total: %lu\n", can_receivers_.size());
}

void CommunicationManager::register_transmitter(Transmitter* transmitter) {
  transmitters_.push_back(transmitter);
  // DEBUG_PRINTF("Transmitter registered, total: %lu\n", transmitters_.size());
}

void CommunicationManager::process_can_frame(const CAN_frame& frame) {
  // Find and call the receiver for this interface
  // For now, we route to all registered receivers
  for (auto& [interface, receiver_pair] : can_receivers_) {
    CanReceiver* receiver = receiver_pair.first;
    if (receiver) {
      CAN_frame mutable_frame = frame;  // Copy to allow modification
      receiver->receive_can_frame(&mutable_frame);
    }
  }
}

void CommunicationManager::update_transmitters(unsigned long currentMillis) {
  for (Transmitter* transmitter : transmitters_) {
    if (transmitter) {
      transmitter->transmit(currentMillis);
    }
  }
}

// Global registration functions (implementations)
// Note: register_can_receiver is implemented in communication/can/comm_can.cpp
// Note: register_transmitter is implemented in communication/modbus/comm_modbus.cpp
