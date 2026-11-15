# Runs install mode on a disposable Windows machine and checks that
# DirectPlay works afterwards. It enables a Windows feature: CI only.
param([Parameter(Mandatory)] [string] $Exe)
$ErrorActionPreference = 'Stop'

$classes = @{
    'DirectPlay (dplayx.dll)'      = '{D1EB6D20-8923-11D0-9D97-00A0C90A43CB}'
    'DirectPlay8Peer (dpnet.dll)'  = '{286F484D-375E-4458-A272-B138E2F80A6A}'
}

function Get-FeatureState {
    (Get-WindowsOptionalFeature -Online -FeatureName DirectPlay).State
}

# Machine-wide until the next restart; other installers read it as "restart
# pending" and may refuse to run.
function Test-RebootPending {
    Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending'
}

# The classes are 32-bit only, so they are created from a 32-bit process.
function Test-ComClass([string] $Clsid) {
    & "$env:windir\SysWOW64\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -Command `
        "[void][Activator]::CreateInstance([Type]::GetTypeFromCLSID('$Clsid'))" 2>&1 | Out-Null
    $LASTEXITCODE -eq 0
}

function Invoke-Install {
    $arguments = '/quiet'
    "+ `"$Exe`" $arguments"
    $p = Start-Process $Exe -ArgumentList $arguments -Wait -PassThru -NoNewWindow
    ''
    Get-Content (Join-Path (Split-Path $Exe) dpsetup.log) | ForEach-Object { "  $_" }
    ''
    if ($p.ExitCode -ne 0) { throw "install mode exited with $($p.ExitCode)" }
}

$before = Get-FeatureState
"DirectPlay before: $before"
"RebootPending before: $(Test-RebootPending)"
if ($before -ne 'Disabled') { throw 'DirectPlay is not disabled beforehand, so this run proves nothing' }
foreach ($name in $classes.Keys) { "$name before: $(Test-ComClass $classes[$name])" }

# Each run is checked on its own, so the second cannot stand in for a first
# run that left the feature off.
function Assert-Installed([string] $When) {
    $state = Get-FeatureState
    "DirectPlay $When`: $state"
    "RebootPending $When`: $(Test-RebootPending)"
    if ($state -eq 'EnablePending') {
        'The feature waits for a restart, so the COM classes cannot be checked'
        return
    }
    if ($state -ne 'Enabled') { throw "DirectPlay is $state $When" }
    foreach ($name in $classes.Keys) {
        if (-not (Test-ComClass $classes[$name])) { throw "$name cannot be created $When" }
        "$name $When`: True"
    }
}

Invoke-Install
Assert-Installed 'after the first run'

# A second run finds the feature enabled and must still succeed.
Invoke-Install
Assert-Installed 'after the second run'
