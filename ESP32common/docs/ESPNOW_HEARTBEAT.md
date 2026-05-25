# ESP-NOW Heartbeat — Design Decision Record

**Status:** Superseded  
**Date superseded:** May 2026

---

## Why an application heartbeat was needed under ESP-NOW

ESP-NOW has no session layer — a peer going silent is indistinguishable from a quiet period without an explicit application-level keepalive. A `msg_heartbeat` packet with a monotonic sequence number and CRC was used to detect:

- Packet loss (sequence gaps)
- Transmitter resets (sequence regression)
- Link degradation (N consecutive missed ACKs)

## How liveness works now under MQTT

MQTT provides a native keepalive via the PINGREQ/PINGRESP mechanism. The broker detects a dead client within one keepalive interval and publishes the configured Last Will message. No additional application-level heartbeat packet is required.

Receivers track connection health via three independent signals:

1. **MQTT session state** — broker-managed keepalive; session disconnect is authoritative.
2. **Topic freshness** — each telemetry topic carries a timestamp; the receiver state machine flags stale data independently of the MQTT session.
3. **State machine health checks** — receiver polls MQTT connection state and data-freshness flags on a fixed interval and transitions to a degraded/disconnected state if either fails.
