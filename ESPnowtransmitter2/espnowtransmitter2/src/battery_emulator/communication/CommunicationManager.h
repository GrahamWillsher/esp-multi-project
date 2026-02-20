/**
 * @file CommunicationManager.h
 * @brief Manages registration of CAN receivers and periodic transmitters
 * 
 * This file provides the minimal registration infrastructure needed by
 * Battery Emulator classes without requiring the full framework.
 */

#ifndef _COMMUNICATION_MANAGER_H
#define _COMMUNICATION_MANAGER_H

#include <list>
#include <map>
#include "can/comm_can.h"
#include "can/CanReceiver.h"
#include "Transmitter.h"
#include "../devboard/utils/types.h"

class CommunicationManager {
 public:
  static CommunicationManager& instance();
  
  /**
   * Register a CAN receiver (called by Battery class constructors)
   */
  void register_can_receiver(CanReceiver* receiver, CAN_Interface interface, CAN_Speed speed);
  
  /**
   * Register a periodic transmitter (called by Battery/Charger constructors)
   */
  void register_transmitter(Transmitter* transmitter);
  
  /**
   * Process an incoming CAN frame by routing to registered receivers
   */
  void process_can_frame(const CAN_frame& frame);
  
  /**
   * Call transmit() on all registered transmitters
   */
  void update_transmitters(unsigned long currentMillis);
  
  /**
   * Get count of registered receivers
   */
  size_t receiver_count() const { return can_receivers_.size(); }
  
  /**
   * Get count of registered transmitters
   */
  size_t transmitter_count() const { return transmitters_.size(); }

 private:
  CommunicationManager() = default;
  ~CommunicationManager() = default;
  
  // CAN receiver registry: interface -> (receiver, speed)
  std::map<CAN_Interface, std::pair<CanReceiver*, CAN_Speed>> can_receivers_;
  
  // Periodic transmitters
  std::list<Transmitter*> transmitters_;
};

// Global registration functions (called by Battery Emulator classes)
void register_can_receiver(CanReceiver* receiver, CAN_Interface interface, CAN_Speed speed);
void register_transmitter(Transmitter* transmitter);

#endif
