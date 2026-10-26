param([string]$Go = 'go')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    & $Go test ./...
    if ($LASTEXITCODE -ne 0) { throw 'Go tests failed' }
    node --test tests/*.test.cjs
    if ($LASTEXITCODE -ne 0) { throw 'JavaScript tests failed' }
    New-Item -ItemType Directory -Force dist | Out-Null
    & $Go build -trimpath -ldflags '-s -w' -o dist/DestinyRTM.exe .
    if ($LASTEXITCODE -ne 0) { throw 'Desktop build failed' }
    Get-FileHash dist/DestinyRTM.exe -Algorithm SHA256
} finally { Pop-Location }
