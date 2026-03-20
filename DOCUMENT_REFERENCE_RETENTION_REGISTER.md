# Documentation Reference Retention Register

**Last Updated:** March 20, 2026 (post-deletion pass)  
**Purpose:** Track documentation to keep vs delete-candidate based on references from current master architecture documents.

## Scope used for this pass

Reference sources:
- `PROJECT_SYSTEM_MASTER.md`
- `ESPnowtransmitter2/espnowtransmitter2/PROJECT_ARCHITECTURE_MASTER.md`
- `espnowreceiver_2/PROJECT_ARCHITECTURE_MASTER.md`

Candidate pools reviewed:
- `ESPnowtransmitter2/espnowtransmitter2/*.md`
- `espnowreceiver_2/*.md`
- `esp32common/docs/*.md`

Governance source:
- `esp32common/docs/project guidlines.md`

---

## KEEP (referenced by a master architecture document)

### System-level
- `PROJECT_SYSTEM_MASTER.md`
- `DOCUMENT_REFERENCE_RETENTION_REGISTER.md`

### Transmitter
- `ESPnowtransmitter2/espnowtransmitter2/PROJECT_ARCHITECTURE_MASTER.md`
- `ESPnowtransmitter2/espnowtransmitter2/ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md`
- `ESPnowtransmitter2/espnowtransmitter2/TRANSMITTER_STATE_MACHINE_IMPLEMENTATION.md`
- `ESPnowtransmitter2/espnowtransmitter2/SERVICE_INTEGRATION_GUIDE.md`
- `ESPnowtransmitter2/espnowtransmitter2/TASK_ARCHITECTURE_AND_SERVICE_ISOLATION.md`
- `ESPnowtransmitter2/espnowtransmitter2/CAN_ETHERNET_GPIO_CONFLICT_ANALYSIS.md`
- `ESPnowtransmitter2/espnowtransmitter2/ETHERNET_SUMMARY.md`
- `ESPnowtransmitter2/espnowtransmitter2/TRANSMITTER_THOROUGH_CODE_REVIEW_2026_03_18.md`

### Receiver
- `espnowreceiver_2/PROJECT_ARCHITECTURE_MASTER.md`
- `espnowreceiver_2/START_HERE.md`
- `espnowreceiver_2/ARCHITECTURE_REDESIGN.md`
- `espnowreceiver_2/DISPLAY_ARCHITECTURE_SUMMARY.md`
- `espnowreceiver_2/EVENT_LOGS_ARCHITECTURE_DIAGRAMS.md`
- `espnowreceiver_2/MQTT_SUBSCRIPTION_ENHANCEMENTS.md`
- `espnowreceiver_2/RECEIVER_FULL_CODE_REVIEW_2026_03_16.md`
- `espnowreceiver_2/RECEIVER_COMMON_CODE_REVIEW_2026_03_17.md`
- `espnowreceiver_2/RECEIVER_HTTP_WEB_ARCHITECTURE_REVIEW_2026_03_18.md`

### Shared/common
- `esp32common/OTA_CROSS_DEVICE_COMPATIBILITY_CHECKLIST.md`
- `esp32common/docs/ESP-NOW_Communication_Architecture.md`
- `esp32common/docs/ESPNOW_HEARTBEAT.md`
- `esp32common/docs/MQTT_LOGGER_IMPLEMENTATION.md`
- `esp32common/docs/project guidlines.md`

---

## DELETED FROM CODEBASE (executed March 20, 2026)

All documents previously listed as `DELETEABLE` were removed from the repository in this pass.

### Transmitter top-level candidate set
- `ESPnowtransmitter2/espnowtransmitter2/ETHERNET_STATE_MACHINE_COMPLETE_IMPLEMENTATION.md`
- `ESPnowtransmitter2/espnowtransmitter2/ETHERNET_TIMING_ANALYSIS.md`
- `ESPnowtransmitter2/espnowtransmitter2/POST_RELEASE_IMPROVEMENTS.md`
- `ESPnowtransmitter2/espnowtransmitter2/STATE_MACHINE_ARCHITECTURE_ANALYSIS.md`
- `ESPnowtransmitter2/espnowtransmitter2/TRANSIENT_QUEUE_INVESTIGATION_2026_03_17.md`

### Receiver top-level candidate set
- `espnowreceiver_2/DISPLAY_ARCHITECTURE_PROGRESS.md`
- `espnowreceiver_2/DISPLAY_DOCS_INDEX.md`
- `espnowreceiver_2/DISPLAY_QUICK_REFERENCE.md`
- `espnowreceiver_2/IMPLEMENTATION_SUMMARY.md`
- `espnowreceiver_2/LVGL_BLACK_SCREEN_ROOT_CAUSE.md`
- `espnowreceiver_2/LVGL_IMAGE_DISPLAY_RESEARCH_FINDINGS.md`
- `espnowreceiver_2/MASTER_CHECKLIST.md`
- `espnowreceiver_2/POWER_BAR_SEGMENT_LAYOUT_REVIEW_2026_03_17.md`
- `espnowreceiver_2/QUICK_REFERENCE_IMAGE_DISPLAY.md`
- `espnowreceiver_2/RECEIVER_FUNCTIONAL_CODE_REVIEW_2026_03_17.md`
- `espnowreceiver_2/RESEARCH_DOCUMENTATION_INDEX.md`
- `espnowreceiver_2/SESSION_SUMMARY.md`
- `espnowreceiver_2/SPLASH_FADE_REVIEW_AND_RECOMMENDATIONS_2026_03_17.md`
- `espnowreceiver_2/SPLASH_FADE_TFT_ONLY_REVIEW_2026_03_17.md`
- `espnowreceiver_2/STEP13_CELL_DATA_CACHE_EXTRACTION.md`
- `espnowreceiver_2/STEP14_SPEC_CACHE_EXTRACTION.md`
- `espnowreceiver_2/STEP15_EVENT_LOG_CACHE_EXTRACTION.md`
- `espnowreceiver_2/STEP16_SETTINGS_CACHE_EXTRACTION.md`
- `espnowreceiver_2/STEP17_MQTT_CACHE_EXTRACTION.md`
- `espnowreceiver_2/STEP18_NETWORK_CACHE_EXTRACTION.md`
- `espnowreceiver_2/STEP19_IDENTITY_CACHE_EXTRACTION.md`
- `espnowreceiver_2/STEP20_NVS_PERSISTENCE_EXTRACTION.md`
- `espnowreceiver_2/STEP21_PEER_REGISTRY_EXTRACTION.md`
- `espnowreceiver_2/STEP22_EVENT_LOG_TYPE_UNIFICATION.md`
- `espnowreceiver_2/STEP23_MAC_REGISTRATION_WORKFLOW_EXTRACTION.md`
- `espnowreceiver_2/STEP24_BATTERY_SPEC_SYNC_WORKFLOW_EXTRACTION.md`
- `espnowreceiver_2/STEP25_WRITE_THROUGH_HELPER_EXTRACTION.md`
- `espnowreceiver_2/STEP26_RUNTIME_STATUS_WORKFLOW_EXTRACTION.md`
- `espnowreceiver_2/STEP27_ACTIVE_MAC_RESOLVER_EXTRACTION.md`
- `espnowreceiver_2/STEP28_MQTT_CONFIG_WORKFLOW_EXTRACTION.md`
- `espnowreceiver_2/STEP29_NETWORK_STORE_WORKFLOW_EXTRACTION.md`
- `espnowreceiver_2/STEP30_SETTINGS_STORE_WORKFLOW_EXTRACTION.md`
- `espnowreceiver_2/STEP31_METADATA_STORE_WORKFLOW_EXTRACTION.md`
- `espnowreceiver_2/STEP32_CONNECTION_STATE_RESOLVER_EXTRACTION.md`
- `espnowreceiver_2/STEP33_MAC_QUERY_HELPER_EXTRACTION.md`
- `espnowreceiver_2/STEP34_TIME_STATUS_UPDATE_WORKFLOW_EXTRACTION.md`
- `espnowreceiver_2/TFT_IMPLEMENTATION_GUIDE.md`
- `espnowreceiver_2/WEBSERVER_CODEBASE_ANALYSIS_2026_03_18.md`

### Shared docs candidate set
- `esp32common/docs/MASTER_DELETE_CANDIDATES.md`

---

## Notes

- Any future documentation-prune pass should repeat this process:
  1. classify as KEEP vs DELETEABLE,
  2. execute deletion,
  3. move deleted entries into a dated `DELETED FROM CODEBASE` section.
