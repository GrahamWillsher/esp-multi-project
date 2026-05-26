#ifndef MQTT_FEATURE_FLAGS_H
#define MQTT_FEATURE_FLAGS_H

/**
 * @file mqtt_feature_flags.h
 * @brief Feature flags for gating MQTT telemetry transmission
 * 
 * This header provides compile-time feature flags to enable/disable
 * non-minimal MQTT data transmission. During Phase M1 (minimal viability),
 * all non-minimal features are disabled. They are progressively re-enabled
 * in subsequent phases after checkpoint validation.
 * 
 * All minimal features (heartbeat, retained snapshots) are ALWAYS on.
 */

// ============================================================================
// MINIMAL FEATURES (ALWAYS ENABLED - DO NOT MODIFY)
// ============================================================================

/// Always on: Heartbeat publishing (10s interval, QoS0)
#define MQTT_FEATURE_HEARTBEAT 1

/// Always on: Retained static configuration snapshots (battery, power, etc.)
#define MQTT_FEATURE_RETAINED_SNAPSHOTS 1

/// Always on: Transmitter version and schema metadata
#define MQTT_FEATURE_VERSION_METADATA 1

/// Always on: Command ACK correlation by request_id
#define MQTT_FEATURE_ACK_CORRELATION 1

// ============================================================================
// NON-MINIMAL FEATURES (GATED BY PHASES, CURRENTLY DISABLED FOR M1)
// ============================================================================

/**
 * @brief Live battery telemetry (voltage, current, temperature, SoC) 
 * 
 * Gate: MQTT FSM state == BROKER_UP
 * Interval: ~1-2s (supervisory, not real-time)
 * QoS: 0 (live data, acceptable loss)
 * Retained: false
 * 
 * Enabled in Phase M4+ after checkpoint M3 validates viability.
 * During M1-M3: battery state delivered via retained snapshots only.
 * Remains gated for now because TFT receiver parity for `battery_live`
 * Both receivers subscribe to battery_live and runtime/* topics.
 * Enabled 2026-05-19 — receiver parity confirmed on both LCD and receiver_2.
 */
#define MQTT_FEATURE_LIVE_BATTERY_TELEMETRY 1

/**
 * @brief Cell voltage and temperature detail telemetry (chunked)
 * 
 * Gate: MQTT FSM state == BROKER_UP
 * Interval: ~5-10s or on-demand via refresh command
 * QoS: 1 (detailed, low frequency)
 * Retained: false
 * 
 * Enabled in Phase M4+ after checkpoint validation.
 * During M1-M3: not transmitted; cache served via demand refresh only.
 * Enabled now as the first concrete M4 re-enable step after Section 10
 * chunking/paging guards were implemented on both receiver variants.
 */
#define MQTT_FEATURE_CELL_DATA_TELEMETRY 1

/**
 * @brief Event-log streaming and chunked transmission
 * 
 * Gate: MQTT FSM state == BROKER_UP (if subscriber active)
 * Interval: on-demand via cmd/stream/event_logs subscribe/unsubscribe
 * QoS: 1 (audit/diagnostic data)
 * Retained: false (chunked)
 * 
 * Enabled in Phase M4+ after checkpoint validation.
 * During M1-M3: event-log summary available only; full logs via web API.
 * Enabled now as the first concrete M4 re-enable step after Section 10
 * chunking/paging guards were implemented on both receiver variants.
 */
#define MQTT_FEATURE_EVENT_LOG_STREAMING 1

/**
 * @brief Demand refresh command (receiver requests data re-sync)
 * 
 * Gate: MQTT FSM state == BROKER_UP
 * Pattern: rx/cmd/refresh/<model>
 * QoS: 1 (state synchronization)
 * 
 * Enabled in Phase M2 (HTTP work) for cache-miss recovery.
 * Must be ready before Phase M3 checkpoint.
 */
#define MQTT_FEATURE_DEMAND_REFRESH 1

/**
 * @brief Receiver command→ACK request/response operations
 * 
 * Gate: MQTT FSM state == BROKER_UP
 * Patterns: rx/cmd/{update,control,stream,refresh}/* → tx/ack/*
 * QoS: 1 (commands + acknowledgments)
 * Timeout: 2000 ms (web API SLA)
 * 
 * Enabled in Phase M2 to wire web API handlers to MQTT.
 * Must be working before Phase M3 checkpoint.
 */
#define MQTT_FEATURE_COMMANDS 1

// ============================================================================
// RUNTIME CONFIGURATION PARAMETERS (NOT FEATURE FLAGS)
// ============================================================================

/**
 * @brief Maximum telemetry retained in memory during MQTT-down
 * 
 * When broker is down (state != BROKER_UP):
 * - Heartbeat: always published (buffered if queue full)
 * - Live telemetry: latest snapshot retained (not queued)
 * - Event logs: bounded ring buffer, ~200 entries
 * 
 * This is NOT a feature flag; it's a sizing constant.
 */
#define MQTT_TELEMETRY_BUFFER_SIZE 256

/**
 * @brief Timeout for waiting for ACK on command operations
 * 
 * Web API handlers wait up to this duration for transmitter ACK
 * on configuration/control commands.
 */
#define MQTT_COMMAND_ACK_TIMEOUT_MS 2000

/**
 * @brief Heartbeat publish interval (milliseconds)
 * 
 * Transmitter publishes to tx/state/heartbeat every N ms.
 * Receiver considers transmitter "alive" based on transmitter timeout policy.
 */
#define MQTT_HEARTBEAT_INTERVAL_MS 10000

/**
 * @brief Broker reconnection retry backoff base (milliseconds)
 * 
 * Initial retry delay when broker disconnects.
 * Exponential backoff with jitter up to ~60s max.
 */
#define MQTT_RECONNECT_BACKOFF_MS 5000

#endif // MQTT_FEATURE_FLAGS_H
