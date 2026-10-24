param(
    [string]$UV4Path = $env:KEIL_UV4_PATH
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'MDK-ARM\PF-MCS-freeRTOS.uvprojx'
$outputPath = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$logPath = Join-Path $outputPath 'keil-build.log'

if (-not $UV4Path) {
    $command = Get-Command UV4.exe -ErrorAction SilentlyContinue
    if ($command) { $UV4Path = $command.Source }
}
if (-not $UV4Path -or -not (Test-Path -LiteralPath $UV4Path -PathType Leaf)) {
    throw 'Set KEIL_UV4_PATH or pass -UV4Path with the full path to UV4.exe.'
}

$startedAt = Get-Date
$arguments = @('-r', ('"' + $projectPath + '"'), '-t', 'PF-MCS-freeRTOS', '-j0', '-o', ('"' + $logPath + '"'))
$process = Start-Process -FilePath $UV4Path -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
if (Test-Path -LiteralPath $logPath) {
    Get-Content -LiteralPath $logPath | Where-Object {
        $_ -match 'Program Size:|Error\(s\)|Warning\(s\)|error:|warning:|Error:|Warning:'
    }
}
if ($process.ExitCode -ne 0) {
    throw "Keil rebuild failed or reported warnings (exit $($process.ExitCode)); inspect the local build log."
}
$firmware = Join-Path $outputPath 'PF-MCS-freeRTOS.hex'
if (-not (Test-Path -LiteralPath $firmware)) {
    throw 'Rebuild did not generate the expected HEX file.'
}
if ((Get-Item -LiteralPath $firmware).LastWriteTime -lt $startedAt.AddSeconds(-2)) {
    throw 'HEX timestamp predates this rebuild.'
}
Get-FileHash -LiteralPath $firmware -Algorithm SHA256
Write-Output 'Build complete. Generated files and logs are local only and ignored by Git.'
