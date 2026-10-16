[CmdletBinding()]
param([switch]$Visible)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$launcherPath = Join-Path $root 'build\dev\out\WrapChineseLauncher.exe'
$captureName = if ($Visible) { 'runtime_visible_latest.json' } else { 'runtime_smoke_latest.json' }
$capturePath = Join-Path $root ("translations\inventory\$captureName")
$expectedRoot = if ($env:WRAP_ROOT) { $env:WRAP_ROOT } else { 'C:\Program Files\Faceform\Wrap 2025.11.14' }
$expectedHost = Join-Path $expectedRoot 'Wrap.exe'
$variableNames = @(
    'WRAP_ROOT',
    'WRAP_CHINESE_AUTO_CAPTURE',
    'WRAP_CHINESE_SMOKE_TEST',
    'WRAP_CHINESE_HEADLESS_CAPTURE',
    'WRAP_CHINESE_CAPTURE_PATH',
    'WRAP_CHINESE_CONFIG_PATH',
    'WRAP_CHINESE_QUIET_TEST'
)
$previousEnvironment = @{}
foreach ($name in $variableNames) {
    $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$hostProcess = $null
$testStartedHost = $false

try {
    $existing = @(Get-Process -Name 'Wrap' -ErrorAction SilentlyContinue)
    if ($existing.Count -ne 0) {
        throw 'Wrap is already running; smoke test refused to avoid touching a user session.'
    }
    if (-not (Test-Path -LiteralPath $launcherPath -PathType Leaf)) {
        throw "Developer launcher is missing (source\build.bat --developer-tools): $launcherPath"
    }

    $startedAt = Get-Date
    $env:WRAP_ROOT = $expectedRoot
    $env:WRAP_CHINESE_AUTO_CAPTURE = '1'
    $env:WRAP_CHINESE_SMOKE_TEST = '1'
    if ($Visible) { Remove-Item Env:WRAP_CHINESE_HEADLESS_CAPTURE -ErrorAction SilentlyContinue }
    else { $env:WRAP_CHINESE_HEADLESS_CAPTURE = '1' }
    $env:WRAP_CHINESE_CAPTURE_PATH = $capturePath
    $env:WRAP_CHINESE_CONFIG_PATH = Join-Path $root 'build\test\WrapChinese-smoke.ini'
    $env:WRAP_CHINESE_QUIET_TEST = '1'

    $launcher = Start-Process -FilePath $launcherPath -WindowStyle Hidden -PassThru
    if (-not $launcher.WaitForExit(30000)) {
        throw 'Launcher did not exit within 30 seconds.'
    }
    if ($launcher.ExitCode -ne 0) {
        throw "Launcher failed with exit code $($launcher.ExitCode)."
    }

    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline -and -not $hostProcess) {
        $hostProcess = Get-Process -Name 'Wrap' -ErrorAction SilentlyContinue |
            Where-Object { $_.StartTime -ge $startedAt } |
            Select-Object -First 1
        if (-not $hostProcess) { Start-Sleep -Milliseconds 250 }
    }
    if (-not $hostProcess) {
        throw 'Injected Wrap process was not observed.'
    }
    $testStartedHost = $true
    if ($hostProcess.Path -ne $expectedHost) {
        throw "Unexpected Wrap process path: $($hostProcess.Path)"
    }

    # Cache a real process handle before exit so .NET can report ExitCode after
    # the OS removes the process from the live process table.
    $null = $hostProcess.Handle
    if (-not $hostProcess.WaitForExit(30000)) {
        throw 'Smoke-test Wrap process did not quit on schedule.'
    }
    $hostExit = $hostProcess.ExitCode
    if ($hostExit -ne 0) {
        throw "Smoke-test Wrap exited with code $hostExit."
    }
    if (-not (Test-Path -LiteralPath $capturePath -PathType Leaf)) {
        throw 'Hook loaded, but runtime capture was not produced.'
    }

    $document = Get-Content -LiteralPath $capturePath -Raw | ConvertFrom-Json
    $entryCount = @($document.translations.PSObject.Properties).Count
    $captureSize = (Get-Item -LiteralPath $capturePath).Length
    Write-Output 'launcher_exit=0'
    Write-Output "wrap_exit=$hostExit"
    Write-Output "capture_entries=$entryCount"
    Write-Output "capture_bytes=$captureSize"
    Write-Output "capture_path=$capturePath"
}
finally {
    if ($testStartedHost -and $hostProcess -and -not $hostProcess.HasExited) {
        # Only the PID observed after this run's start time is eligible here.
        Stop-Process -Id $hostProcess.Id -Force -ErrorAction SilentlyContinue
    }
    foreach ($name in $variableNames) {
        $value = $previousEnvironment[$name]
        if ($null -eq $value) {
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }
        else {
            [Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }
}
