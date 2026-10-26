param([string]$Go = 'go')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    foreach ($script in (Get-ChildItem -LiteralPath ./source/js -Filter '*.js' -File)) {
        node --check $script.FullName
        if ($LASTEXITCODE -ne 0) { throw "JavaScript syntax check failed: $($script.Name)" }
    }
    & $Go test ./...
    if ($LASTEXITCODE -ne 0) { throw 'Go tests failed' }
    node --test tests/*.test.cjs
    if ($LASTEXITCODE -ne 0) { throw 'JavaScript tests failed' }
    New-Item -ItemType Directory -Force dist | Out-Null
    & $Go build -trimpath -ldflags '-s -w' -o dist/DestinyRTM.exe .
    if ($LASTEXITCODE -ne 0) { throw 'Desktop build failed' }
    & $Go build -trimpath -ldflags '-s -w' -o dist/rtmctl.exe ./cmd/rtmctl
    if ($LASTEXITCODE -ne 0) { throw 'RTM CLI build failed' }
    $cliHelp = & ./dist/rtmctl.exe help | Out-String
    if ($LASTEXITCODE -ne 0 -or $cliHelp -notmatch 'rtmctl status' -or $cliHelp -notmatch 'rtmctl write' -or $cliHelp -notmatch '--json') {
        throw 'RTM CLI smoke check failed'
    }
    & ./tests/smoke-desktop.ps1 -Executable ./dist/DestinyRTM.exe
    $binaryHash = Get-FileHash dist/DestinyRTM.exe -Algorithm SHA256
    $cliHash = Get-FileHash dist/rtmctl.exe -Algorithm SHA256
    $sourceHashes = @{}
    foreach ($file in (Get-ChildItem -LiteralPath ./source -File -Recurse)) {
        $relative = $file.FullName.Substring($PSScriptRoot.Length + 1).Replace('\','/')
        $sourceHashes[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    @{ executable='DestinyRTM.exe'; sha256=$binaryHash.Hash; cliExecutable='rtmctl.exe'; cliSha256=$cliHash.Hash; source=$sourceHashes } |
        ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -LiteralPath ./dist/build-manifest.json
    $binaryHash
} finally { Pop-Location }
