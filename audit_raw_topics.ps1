$paths = @(
  'C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\src',
  'C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\lib',
  'C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD\src',
  'C:\Users\GrahamWillsher\ESP32Projects\ESPnowtransmitter2\espnowtransmitter2\src'
)
foreach ($path in $paths) {
  Write-Host "--- $path"
  $hits = Get-ChildItem $path -Recurse -Include '*.cpp','*.h' |
    Select-String 'batt-emu/mqtt-v1' |
    Where-Object { $_.Line -notmatch '(LOG_|^\s*//)' }
  if ($hits.Count -eq 0) {
    Write-Host "  CLEAN"
  } else {
    $hits | ForEach-Object { Write-Host ("  " + $_.Filename + ":" + $_.LineNumber + "  " + $_.Line.Trim()) }
  }
}
