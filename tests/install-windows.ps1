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

# The classes are 32-bit only, so they are created from a 32-bit process.
function Test-ComClass([string] $Clsid) {
    & "$env:windir\SysWOW64\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -Command `
        "[void][Activator]::CreateInstance([Type]::GetTypeFromCLSID('$Clsid'))" 2>&1 | Out-Null
    $LASTEXITCODE -eq 0
}

function Invoke-Install {
    $p = Start-Process $Exe -ArgumentList '/quiet' -Wait -PassThru -NoNewWindow
    'dpsetup.log:'
    Get-Content (Join-Path (Split-Path $Exe) dpsetup.log) | ForEach-Object { "  | $_" }
    if ($p.ExitCode -ne 0) { throw "install mode exited with $($p.ExitCode)" }
}

$before = Get-FeatureState
"DirectPlay before: $before"
if ($before -ne 'Disabled') { throw 'DirectPlay is not disabled beforehand, so this run proves nothing' }
foreach ($name in $classes.Keys) { "$name before: $(Test-ComClass $classes[$name])" }

Invoke-Install
# A second run finds the feature enabled and must still succeed.
Invoke-Install

$after = Get-FeatureState
"DirectPlay after: $after"
if ($after -eq 'EnablePending') {
    'The feature waits for a restart, so the COM classes cannot be checked'
    exit 0
}
if ($after -ne 'Enabled') { throw "DirectPlay is $after afterwards" }
foreach ($name in $classes.Keys) {
    if (-not (Test-ComClass $classes[$name])) { throw "$name cannot be created afterwards" }
    "$name after: True"
}
