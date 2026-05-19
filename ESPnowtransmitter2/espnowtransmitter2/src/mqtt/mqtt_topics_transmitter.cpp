#include "mqtt/mqtt_topics_transmitter.h"
#include <algorithm>

namespace MqttTopicsTransmitter {

const char* GetRxCmdSubscriptionPattern() {
  return "batt-emu/mqtt-v1/rx/cmd/#";
}

bool ExtractCommandTopicParts(const std::string& cmd_topic,
                              std::string& out_category,
                              std::string& out_action) {
  // Topic format: "batt-emu/mqtt-v1/rx/cmd/{category}/{action}"
  const std::string prefix = RxCmd::PREFIX;
  if (cmd_topic.substr(0, prefix.length()) != prefix) {
    return false;
  }
  
  // Remove prefix: "batt-emu/mqtt-v1/rx/cmd/"
  std::string remainder = cmd_topic.substr(prefix.length() + 1); // +1 for the /
  
  // Split by /
  size_t slash_pos = remainder.find('/');
  if (slash_pos == std::string::npos) {
    return false;
  }
  
  out_category = remainder.substr(0, slash_pos);
  out_action = remainder.substr(slash_pos + 1);
  
  return !out_category.empty() && !out_action.empty();
}

const char* GetAckTopicForCategory(const std::string& category) {
  // Maps command category to ACK topic
  if (category == "update") {
    // For update commands, ACK goes to the model-specific ACK topic
    // Caller must determine which model from action
    return TxAck::PREFIX; // base, caller adds specific suffix
  } else if (category == "control") {
    return TxAck::CONTROL;
  } else if (category == "stream") {
    return TxAck::PREFIX; // custom handling needed
  } else if (category == "refresh") {
    return TxAck::PREFIX; // custom handling needed
  }
  
  return TxAck::PREFIX;
}

} // namespace MqttTopicsTransmitter
