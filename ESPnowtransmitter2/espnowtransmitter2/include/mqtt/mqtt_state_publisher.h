#ifndef MQTT_STATE_PUBLISHER_H
#define MQTT_STATE_PUBLISHER_H

#include <string>
#include <cstdint>
#include <vector>

/**
 * @brief Publishes transmitter state, configuration, and metadata to MQTT
 * 
 * Handles:
 * - Retained static models (battery, power, network, mqtt, led configs)
 * - Metadata (version, schema_versions, runtime state)
 * - Live heartbeat/telemetry
 * - Chunking for large payloads (event_logs, cell_data)
 * 
 * Per MQTT-only architecture:
 * - Static topics retain last published value
 * - Receiver subscribes on connect and gets immediate replay
 * - No polling required; MQTT push model
 */
class MqttStatePublisher {
public:
  /**
   * Initialize state publisher
   * Called after MQTT manager connects
   */
  static void Init();

  /**
   * Publish retained static models
   * Called on boot and when configuration changes
   */
  static void PublishStaticModels();

  /**
   * Publish metadata (version, schema_versions, runtime state)
   * Called on boot and periodically for keep-alive
   */
  static void PublishMetadata();

  /**
   * Publish heartbeat (indicating transmitter is alive)
   * Called periodically at 1000-2000ms intervals
   */
  static void PublishHeartbeat();

  /**
   * Publish live battery telemetry (non-retained)
   * Called frequently (10Hz or as available)
   */
  static void PublishBatteryLiveData(const std::string& battery_json);

  /**
   * Publish event log summary (non-retained)
   * Contains compact counters for dashboard display
   */
  static void PublishEventLogSummary(const std::string& summary_json);

  /**
   * Publish event logs as chunks when full dump requested
   * Handles chunking of large payloads per Section 17
   */
  static void PublishEventLogsChunked(const std::string& event_logs_json);

  /**
   * Publish cell data as chunks
   * Handles chunking of large cell arrays
   */
  static void PublishCellDataChunked(const std::string& cell_data_json);

  /**
   * Get next chunk for streaming
   * Called by scheduler to emit chunks at controlled rate
   */
  static bool GetNextChunk(std::string& out_topic, std::string& out_payload);

  /**
   * Clear any pending chunks (call if transmission interrupted)
   */
  static void ClearPendingChunks();

  /**
   * Publish specific static model (called when individual config changes)
   */
  static void PublishStaticModel(const std::string& model_name, const std::string& model_json);

private:
  struct ChunkInfo {
    std::string topic;
    std::string payload;
  };

  static std::string BuildVersionPayload();
  static std::string BuildSchemaVersionsPayload();
  static std::string BuildRuntimePayload();
  static std::vector<ChunkInfo> ChunkLargePayload(const std::string& topic_template,
                                                  const std::string& model_name,
                                                  const std::string& payload);
};

#endif // MQTT_STATE_PUBLISHER_H
