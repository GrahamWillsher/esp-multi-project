# ESP-NOW Communication Architecture — Design Decision Record

**Status:** Superseded  
**Date superseded:** May 2026  
**Replaced by:** MQTT-over-IP

---

## Why ESP-NOW was chosen initially

ESP-NOW (IEEE 802.11 vendor action frames) was the original inter-device transport because:

- **No broker dependency** — peer-to-peer delivery works with no infrastructure, which was practical during early prototyping before the Ethernet path was proven.
- **Low latency** — sub-10 ms delivery without TCP handshake overhead.
- **Simple pairing** — MAC-address-based peering; no DHCP or DNS dependency at runtime.
- **Fast iteration** — allowed the transmitter/receiver split to be validated quickly before the IP networking layer was stable.

## Why it was replaced with MQTT

- **Coexistence complexity** — ESP-NOW requires the WiFi radio in STA mode with no IP address. Running STA for both ESP-NOW frames and normal WiFi connectivity simultaneously creates AP/STA coexistence edge cases and channel-lock conflicts on the transmitter.
- **Ethernet available** — the Olimex ESP32-POE2 transmitter has wired Ethernet (LAN8720 PHY). With a reliable IP path available, the rationale for a wireless-only transport disappeared.
- **MQTT semantics** — QoS levels, retained topics, and wildcard subscriptions give proper delivery guarantees and settings-sync semantics that ESP-NOW cannot provide natively.
- **Scalability** — adding a second receiver (Waveshare LCD) required only a new MQTT subscription; under ESP-NOW it would have required explicit peer registration and MAC management per device.

## Active architecture references

- [ESPnowtransmitter2/espnowtransmitter2/PROJECT_ARCHITECTURE_MASTER.md](../../ESPnowtransmitter2/espnowtransmitter2/PROJECT_ARCHITECTURE_MASTER.md)
- [espnowreceiver_2/PROJECT_ARCHITECTURE_MASTER.md](../../espnowreceiver_2/PROJECT_ARCHITECTURE_MASTER.md)
- [espnowreceiver_LCD/README.md](../../espnowreceiver_LCD/README.md)
