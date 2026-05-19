#include "mqtt/mqtt_topics_receiver.h"
#include <algorithm>

namespace MqttTopicsReceiver {

bool ExtractAckModel(const std::string& ack_topic, std::string& out_model) {
  const std::string prefix = TxAck::PREFIX;
  if (ack_topic.substr(0, prefix.length()) != prefix) {
    return false;
  }
  
  // Topic format: "batt-emu/mqtt-v1/tx/ack/{model}"
  size_t last_slash = ack_topic.rfind('/');
  if (last_slash == std::string::npos || last_slash + 1 >= ack_topic.length()) {
    return false;
  }
  
  out_model = ack_topic.substr(last_slash + 1);
  return !out_model.empty();
}

const char* GetTxSubscriptionPattern() {
  return "batt-emu/mqtt-v1/tx/#";
}

const char* GetTxAckSubscriptionPattern() {
  return "batt-emu/mqtt-v1/tx/ack/#";
}

} // namespace MqttTopicsReceiver
