# Fix duplicate tags created by over-aggressive regex
# Example: LOG_INFO("TAG", "TAG", "TAG", "message") -> LOG_INFO("TAG", "message")

Write-Host "Fixing duplicate logging tags..." -ForegroundColor Cyan

$files = Get-ChildItem -Recurse -Filter *.cpp | Where-Object { $_.FullName -notmatch "\\\.pio\\" }

foreach ($file in $files) {
    $content = Get-Content $file.FullName -Raw
    if (!$content) { continue }
    
    $original = $content
    
    # Fix patterns like LOG_INFO("TAG", "TAG", "TAG", "TAG", "TAG", "TAG", "message")
    # Keep reducing until we have just LOG_INFO("TAG", "message")
    $maxIterations = 10
    $iteration = 0
    
    do {
        $changed = $false
        $iteration++
        
        # Pattern: LOG_LEVEL("TAG", "TAG", ... -> LOG_LEVEL("TAG", ...
        # This removes one duplicate TAG at a time
        $newContent = $content -replace '(LOG_(?:INFO|ERROR|WARN|DEBUG|TRACE))\s*\(\s*"([^"]+)",\s*"\2",', '$1("$2",'
        
        if ($newContent -ne $content) {
            $changed = $true
            $content = $newContent
        }
        
    } while ($changed -and $iteration -lt $maxIterations)
    
    if ($content -ne $original) {
        Set-Content $file.FullName -Value $content -NoNewline
        Write-Host "  ✓ Fixed $($file.Name)" -ForegroundColor Green
    }
}

Write-Host "Done!" -ForegroundColor Cyan
