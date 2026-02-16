# Fix embedded tags in LOG_* calls
# Converts LOG_ERROR("[TAG] message") to LOG_ERROR("TAG", "message")

$files = Get-ChildItem -Path "src" -Include *.cpp,*.h -Recurse
$fixCount = 0

foreach ($file in $files) {
    $content = Get-Content $file.FullName -Raw
    $originalContent = $content
    
    # Pattern to match LOG_*(  "[TAG] message"  )
    # Captures: LOG_LEVEL, TAG (without brackets), and message
    $pattern = '(LOG_(?:ERROR|WARN|INFO|DEBUG|TRACE))\s*\(\s*"\[([A-Z_]+)\]\s+([^"]+)"\s*\)'
    $replacement = '$1("$2", "$3")'
    
    $content = $content -replace $pattern, $replacement
    
    # Check if anything changed
    if ($content -ne $originalContent) {
        Set-Content -Path $file.FullName -Value $content -NoNewline
        Write-Host "Fixed: $($file.FullName)"
        $fixCount++
    }
}

Write-Host "`nTotal files fixed: $fixCount"
