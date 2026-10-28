param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$logPath = Join-Path ([IO.Path]::GetTempPath()) ('destiny-rtm-smoke-' + [guid]::NewGuid().ToString('N') + '.log')
$process = Start-Process -FilePath $resolvedExecutable -ArgumentList @('-no-open','-listen','127.0.0.1:0') -WindowStyle Hidden -RedirectStandardOutput $logPath -PassThru
try {
    $origin = $null
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($process.HasExited) { throw 'Desktop host exited during startup' }
        if (Test-Path -LiteralPath $logPath) {
            $line = Get-Content -LiteralPath $logPath -TotalCount 1
            if ($line -match '^Destiny RTM: (http://127\.0\.0\.1:\d+)$') { $origin = $Matches[1]; break }
        }
        Start-Sleep -Milliseconds 100
    }
    if (!$origin) { throw 'Desktop host did not start within 10 seconds' }
    $configuration = Invoke-RestMethod -Uri ($origin + '/host-config') -TimeoutSec 5
    if ($configuration.host -ne 'destiny-rtm' -or !$configuration.token) { throw 'Invalid desktop configuration response' }
    foreach ($asset in @('index.html','js/transport.js','js/operations.js','js/connection.js','base.css','design.css')) {
        $response = Invoke-WebRequest -Uri ($origin + '/' + $asset) -TimeoutSec 5 -UseBasicParsing
        if ($response.StatusCode -ne 200 -or !$response.Content) { throw "Missing embedded asset: $asset" }
    }
    $status = 0
    try { Invoke-WebRequest -Uri ($origin + '/console?url=http%3A%2F%2F192.168.1.100%2Findex.ps3') -TimeoutSec 5 -UseBasicParsing | Out-Null }
    catch { $status = [int]$_.Exception.Response.StatusCode }
    if ($status -ne 403) { throw 'Unauthenticated console request was not rejected' }
    $status = 0
    try { Invoke-WebRequest -Uri ($origin + '/console?url=http%3A%2F%2F8.8.8.8%2Findex.ps3') -Headers @{'X-RTM-Token'=$configuration.token} -TimeoutSec 5 -UseBasicParsing | Out-Null }
    catch { $status = [int]$_.Exception.Response.StatusCode }
    if ($status -ne 400) { throw 'Unsupported console destination was not rejected' }
    Write-Output 'Packaged desktop smoke checks passed; no console requests were sent.'
} finally {
    if (!$process.HasExited) { Stop-Process -Id $process.Id }
    Remove-Item -LiteralPath $logPath -ErrorAction SilentlyContinue
}
