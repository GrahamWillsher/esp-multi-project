$file = 'C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\src\mqtt\mqtt_client.cpp'
$content = Get-Content $file -Raw

# Add canonical include after first #include line
if ($content -notmatch '#include <esp32common/mqtt/mqtt_topics_common.h>') {
    $content = $content -replace '(#include "mqtt_client\.h")', '$1' + "`n#include <esp32common/mqtt/mqtt_topics_common.h>"
}

# Replace local constexpr string literals (rx topics used for publish)
$content = $content -replace '"batt-emu/mqtt-v1/rx/cmd/stream/event_logs"', 'mqtt::topics::rx::CMD_STREAM_EVENT_LOGS'
$content = $content -replace '"batt-emu/mqtt-v1/rx/cmd/stream/cell_data"', 'mqtt::topics::rx::CMD_STREAM_CELL_DATA'

# Replace tx ACK topic literals
$content = $content -replace '"batt-emu/mqtt-v1/tx/ack/battery"', 'mqtt::topics::tx::ACK_BATTERY'
$content = $content -replace '"batt-emu/mqtt-v1/tx/ack/power"', 'mqtt::topics::tx::ACK_POWER'
$content = $content -replace '"batt-emu/mqtt-v1/tx/ack/network"', 'mqtt::topics::tx::ACK_NETWORK'
$content = $content -replace '"batt-emu/mqtt-v1/tx/ack/mqtt"', 'mqtt::topics::tx::ACK_MQTT'
$content = $content -replace '"batt-emu/mqtt-v1/tx/ack/inverter"', 'mqtt::topics::tx::ACK_INVERTER'
$content = $content -replace '"batt-emu/mqtt-v1/tx/ack/can"', 'mqtt::topics::tx::ACK_CAN'
$content = $content -replace '"batt-emu/mqtt-v1/tx/ack/contactor"', 'mqtt::topics::tx::ACK_CONTACTOR'
$content = $content -replace '"batt-emu/mqtt-v1/tx/ack/control"', 'mqtt::topics::tx::ACK_CONTROL'
$content = $content -replace '"batt-emu/mqtt-v1/tx/ack/component"', 'mqtt::topics::tx::ACK_COMPONENT'
$content = $content -replace '"batt-emu/mqtt-v1/tx/ack/event_logs_clear"', 'mqtt::topics::tx::ACK_EVENT_LOGS_CLEAR'
$content = $content -replace '"batt-emu/mqtt-v1/tx/ack/refresh"', 'mqtt::topics::tx::ACK_REFRESH'
$content = $content -replace '"batt-emu/mqtt-v1/tx/ack/#"', 'mqtt::topics::tx::ACK_WILDCARD'

# Replace tx state topics
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/cell_data/chunk"', 'mqtt::topics::tx::STATE_CELL_DATA_CHUNK'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/battery_live"', 'mqtt::topics::tx::STATE_BATTERY_LIVE'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/runtime/led"', 'mqtt::topics::tx::STATE_RUNTIME_LED'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/runtime/system"', 'mqtt::topics::tx::STATE_RUNTIME_SYSTEM'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/runtime/charger"', 'mqtt::topics::tx::STATE_RUNTIME_CHARGER'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/runtime/inverter"', 'mqtt::topics::tx::STATE_RUNTIME_INVERTER'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/summary/event_logs"', 'mqtt::topics::tx::STATE_SUMMARY_EVENT_LOGS'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/event_logs/chunk"', 'mqtt::topics::tx::STATE_EVENT_LOGS_CHUNK'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/static/battery"', 'mqtt::topics::tx::STATE_STATIC_BATTERY'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/static/inverter"', 'mqtt::topics::tx::STATE_STATIC_INVERTER'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/static/catalog_battery"', 'mqtt::topics::tx::STATE_STATIC_CATALOG_BATTERY'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/static/catalog_inverter"', 'mqtt::topics::tx::STATE_STATIC_CATALOG_INVERTER'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/static/network"', 'mqtt::topics::tx::STATE_STATIC_NETWORK'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/static/mqtt"', 'mqtt::topics::tx::STATE_STATIC_MQTT'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/static/power"', 'mqtt::topics::tx::STATE_STATIC_POWER'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/static/led"', 'mqtt::topics::tx::STATE_STATIC_LED'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/static/settings"', 'mqtt::topics::tx::STATE_STATIC_SETTINGS'
$content = $content -replace '"batt-emu/mqtt-v1/tx/state/heartbeat"', 'mqtt::topics::tx::STATE_HEARTBEAT'

# Replace tx meta topics
$content = $content -replace '"batt-emu/mqtt-v1/tx/meta/version"', 'mqtt::topics::tx::META_VERSION'
$content = $content -replace '"batt-emu/mqtt-v1/tx/meta/schema_versions"', 'mqtt::topics::tx::META_SCHEMA_VERSIONS'
$content = $content -replace '"batt-emu/mqtt-v1/tx/meta/runtime"', 'mqtt::topics::tx::META_RUNTIME'

Set-Content $file $content -NoNewline

# Report
$remaining = (Select-String 'batt-emu' $file).Count
$canonical = (Select-String 'mqtt::topics' $file).Count
Write-Host "Done. Remaining raw batt-emu strings: $remaining  |  Canonical references: $canonical"
