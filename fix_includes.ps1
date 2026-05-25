$needle = "#include <esp32common/mqtt/mqtt_topics_common.h>"
$files = @(
  "C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\lib\webserver\api\api_control_handlers.cpp",
  "C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\lib\webserver\api\api_debug_handlers.cpp",
  "C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\lib\webserver\api\api_led_handlers.cpp",
  "C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\lib\webserver\api\api_network_handlers.cpp",
  "C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\lib\webserver\api\api_settings_handlers.cpp",
  "C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\lib\webserver\api\api_type_selection_handlers.cpp",
  "C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\src\mqtt\mqtt_client.cpp",
  "C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\src\mqtt\mqtt_command_client.cpp"
)
foreach ($f in $files) {
    $lines = [System.IO.File]::ReadAllLines($f)
    $seen = $false
    $out = [System.Collections.Generic.List[string]]::new()
    foreach ($l in $lines) {
        if ($l.Trim() -eq $needle) {
            if (-not $seen) { $out.Add($l); $seen = $true }
        } else { $out.Add($l) }
    }
    [System.IO.File]::WriteAllLines($f, $out)
    $removed = $lines.Count - $out.Count
    Write-Host "$([IO.Path]::GetFileName($f)): removed $removed duplicates"
}
Write-Host "Done"
