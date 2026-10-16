[CmdletBinding()]
param(
    [int]$StartAt = 0,
    [int]$Limit = 0,
    [switch]$Visible
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$probeRoot = Join-Path $root 'build\node_probe'
$manifestPath = Join-Path $probeRoot 'manifest.json'
$launcherPath = Join-Path $root 'build\dev\out\WrapChineseLauncher.exe'
$configPath = Join-Path $probeRoot 'WrapChinese-probe.ini'
$variableNames = @(
    'WRAP_ROOT',
    'WRAP_CHINESE_AUTO_CAPTURE',
    'WRAP_CHINESE_SMOKE_TEST',
    'WRAP_CHINESE_HEADLESS_CAPTURE',
    'WRAP_CHINESE_CAPTURE_PATH',
    'WRAP_CHINESE_CONFIG_PATH',
    'WRAP_CHINESE_QUIET_TEST',
    'WRAP_CHINESE_WIDGET_INVENTORY_PATH',
    'WRAP_CHINESE_PROBE_NODE_TYPE',
    'WRAP_CHINESE_PROBE_STATUS_PATH',
    'WRAP_CHINESE_CAPTURE_INCLUDE_TRANSLATED',
    'WRAP_CHINESE_PROBE_PLAN_PATH'
)

if ($StartAt -lt 0 -or $Limit -lt 0) {
    throw 'StartAt and Limit must not be negative.'
}
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Generate the probe projects first: $manifestPath"
}
if (-not (Test-Path -LiteralPath $launcherPath -PathType Leaf)) {
    throw "Build developer tools first (source\build.bat --developer-tools): $launcherPath"
}
if (@(Get-Process -Name 'Wrap' -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'Wrap is already running; the node probe will not touch a user session.'
}

$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$allJobs = @([pscustomobject]@{
    node_type = '__baseline__'
    project = [string]$manifest.baseline.project
    capture = [string]$manifest.baseline.capture
}) + @($manifest.nodes)
if ($StartAt -ge $allJobs.Count) {
    throw "StartAt is outside the generated job range 0..$($allJobs.Count - 1)."
}
$jobs = @($allJobs | Select-Object -Skip $StartAt)
if ($Limit -gt 0) {
    $jobs = @($jobs | Select-Object -First $Limit)
}

$previousEnvironment = @{}
foreach ($name in $variableNames) {
    $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
    # Use the same installation that generated the project format/node list.
    $env:WRAP_ROOT = Split-Path -Parent ([string]$manifest.wrap_cmd)
    $env:WRAP_CHINESE_AUTO_CAPTURE = '0'
    $env:WRAP_CHINESE_SMOKE_TEST = '0'
    $env:WRAP_CHINESE_CONFIG_PATH = $configPath
    $env:WRAP_CHINESE_QUIET_TEST = '1'
    $env:WRAP_CHINESE_CAPTURE_INCLUDE_TRANSLATED = '1'
    if ($Visible) {
        Remove-Item Env:WRAP_CHINESE_HEADLESS_CAPTURE -ErrorAction SilentlyContinue
    }
    else {
        $env:WRAP_CHINESE_HEADLESS_CAPTURE = '1'
    }

    $statusDirectory = Join-Path $probeRoot 'status'
    if (-not (Test-Path -LiteralPath $statusDirectory)) {
        $null = New-Item -ItemType Directory -Path $statusDirectory
    }
    $baselineCapture = ''
    $nodeJobs = @()
    foreach ($job in $jobs) {
        $capturePath = [string]$job.capture
        $captureDirectory = Split-Path -Parent $capturePath
        if (-not (Test-Path -LiteralPath $captureDirectory)) {
            $null = New-Item -ItemType Directory -Path $captureDirectory
        }
        if (Test-Path -LiteralPath $capturePath) {
            Remove-Item -LiteralPath $capturePath -Force
        }
        if ($job.node_type -eq '__baseline__') {
            $baselineCapture = $capturePath
        }
        else {
            $statusPath = Join-Path $statusDirectory ($job.node_type + '.json')
            if (Test-Path -LiteralPath $statusPath) {
                Remove-Item -LiteralPath $statusPath -Force
            }
            $nodeJobs += [ordered]@{
                node_type = [string]$job.node_type
                capture = $capturePath
                status = $statusPath
            }
        }
    }
    if ($nodeJobs.Count -eq 0) { throw 'The selected range contains no nodes.' }
    $planPath = Join-Path $probeRoot 'run-plan.json'
    $plan = [ordered]@{
        schema = 'wrap-node-probe-plan-v1'
        baseline_capture = $baselineCapture
        nodes = $nodeJobs
    }
    [IO.File]::WriteAllText($planPath, ($plan | ConvertTo-Json -Depth 5),
                            [Text.UTF8Encoding]::new($false))
    $env:WRAP_CHINESE_PROBE_PLAN_PATH = $planPath
    Remove-Item Env:WRAP_CHINESE_PROBE_NODE_TYPE -ErrorAction SilentlyContinue
    Remove-Item Env:WRAP_CHINESE_PROBE_STATUS_PATH -ErrorAction SilentlyContinue

    $projectPath = [string]$manifest.baseline.project
    $startedAt = Get-Date
    Write-Output "probing $($nodeJobs.Count) nodes in one Wrap session"
    $argument = '"' + $projectPath + '"'
    $launcher = Start-Process -FilePath $launcherPath -ArgumentList $argument -WindowStyle Hidden -PassThru
    if (-not $launcher.WaitForExit(30000) -or $launcher.ExitCode -ne 0) {
        throw 'Launcher failed.'
    }
    $hostProcess = $null
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline -and -not $hostProcess) {
        $hostProcess = Get-Process -Name 'Wrap' -ErrorAction SilentlyContinue |
            Where-Object { $_.StartTime -ge $startedAt } | Select-Object -First 1
        if (-not $hostProcess) { Start-Sleep -Milliseconds 200 }
    }
    if (-not $hostProcess) { throw 'Wrap did not start.' }
    $null = $hostProcess.Handle
    $timeoutMs = [Math]::Max(30000, 20000 + $nodeJobs.Count * 5000)
    if (-not $hostProcess.WaitForExit($timeoutMs)) { throw 'Wrap probe timed out.' }
    if ($hostProcess.ExitCode -ne 0) {
        throw "Wrap exited with code $($hostProcess.ExitCode)."
    }
    foreach ($job in $nodeJobs) {
        if (-not (Test-Path -LiteralPath $job.capture -PathType Leaf)) {
            throw "No capture was produced for $($job.node_type)."
        }
        $status = Get-Content -Raw -LiteralPath $job.status | ConvertFrom-Json
        if (-not $status.success) {
            throw "Wrap did not create $($job.node_type): $($status.error)"
        }
    }
}
finally {
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

Write-Output "completed=$($nodeJobs.Count)"
