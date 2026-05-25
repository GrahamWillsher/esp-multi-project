$r2api = 'C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\lib\webserver\api'
$files = 'api_control_handlers.cpp','api_debug_handlers.cpp','api_led_handlers.cpp','api_network_handlers.cpp','api_settings_handlers.cpp','api_type_selection_handlers.cpp'
foreach ($f in $files) {
    $path = "$r2api\$f"
    $rawHits = (Select-String 'batt-emu/mqtt' $path).Count
    $canonHits = (Select-String 'mqtt::topics' $path).Count
    Write-Host "$f : raw_strings=$rawHits canonical=$canonHits"
}

# Also check mqtt_client.cpp subscribe calls
$mc = 'C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\src\mqtt\mqtt_client.cpp'
$subHits = (Select-String 'subscribe\("batt-emu' $mc).Count
$subCanon = (Select-String 'subscribe\(mqtt::topics' $mc).Count
Write-Host "mqtt_client.cpp subscribe: raw=$subHits canonical=$subCanon"
