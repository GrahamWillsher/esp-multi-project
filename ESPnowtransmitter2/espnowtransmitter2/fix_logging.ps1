# Fix embedded-tag logging to tagged logging style
# Converts LOG_INFO("[TAG] message", args) to LOG_INFO("TAG", "message", args)
# Also adds default tags to untagged LOG calls based on file context

$fileTagMap = @{
    "src\settings\settings_manager.cpp" = "SETTINGS"
    "src\network\mqtt_manager.cpp" = "MQTT"
    "src\network\ethernet_manager.cpp" = "ETHERNET"
    "src\network\ota_manager.cpp" = "OTA"
    "src\network\time_manager.cpp" = "TIME"
    "src\network\mqtt_task.cpp" = "MQTT"
    "src\espnow\message_handler.cpp" = "MSG_HANDLER"
    "src\testing\dummy_data_generator.cpp" = "DUMMY_DATA"
    "src\espnow\version_beacon_manager.cpp" = "VERSION"
    "src\espnow\transmission_task.cpp" = "TX_TASK"
    "src\espnow\keep_alive_manager.cpp" = "KEEPALIVE"
    "src\espnow\data_cache.cpp" = "CACHE"
    "src\espnow\enhanced_cache.cpp" = "CACHE"
}

foreach ($file in $fileTagMap.Keys) {
    $defaultTag = $fileTagMap[$file]
    
    if (Test-Path $file) {
        Write-Host "Processing $file (default tag: $defaultTag)..."
        $content = Get-Content $file -Raw
        $original = $content
        
        # Fix LOG_INFO("[TAG] format", args) -> LOG_INFO("TAG", "format", args)
        $content = $content -replace 'LOG_INFO\s*\(\s*"\[([A-Z_]+)\]\s+([^"]+)"', 'LOG_INFO("$1", "$2"'
        
        # Fix LOG_ERROR("[TAG] format", args) -> LOG_ERROR("TAG", "format", args)
        $content = $content -replace 'LOG_ERROR\s*\(\s*"\[([A-Z_]+)\]\s+([^"]+)"', 'LOG_ERROR("$1", "$2"'
        
        # Fix LOG_WARN("[TAG] format", args) -> LOG_WARN("TAG", "format", args)
        $content = $content -replace 'LOG_WARN\s*\(\s*"\[([A-Z_]+)\]\s+([^"]+)"', 'LOG_WARN("$1", "$2"'
        
        # Fix LOG_DEBUG("[TAG] format", args) -> LOG_DEBUG("TAG", "format", args)
        $content = $content -replace 'LOG_DEBUG\s*\(\s*"\[([A-Z_]+)\]\s+([^"]+)"', 'LOG_DEBUG("$1", "$2"'
        
        # Fix untagged LOG calls - add default tag based on file
        # LOG_INFO("message") -> LOG_INFO("TAG", "message")
        $content = $content -replace '(LOG_INFO|LOG_ERROR|LOG_WARN|LOG_DEBUG)\s*\(\s*"([^"]+)"', "`$1(`"$defaultTag`", `"`$2`""
        
        if ($content -ne $original) {
            Set-Content $file -Value $content -NoNewline
            Write-Host "  ✓ Updated $file" -ForegroundColor Green
        } else {
            Write-Host "  - No changes needed for $file" -ForegroundColor Yellow
        }
    } else {
        Write-Host "  ✗ File not found: $file" -ForegroundColor Red
    }
}

Write-Host "`nDone! All files processed." -ForegroundColor Cyan
