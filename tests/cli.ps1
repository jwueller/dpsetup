# Checks extraction mode's command line: the aliases extract what a bare
# folder does, rejected command lines exit 87 and create nothing, and /log
# moves the log. Install mode's switches are in install-windows.ps1, and /?
# is left out because it waits for a click. CI only: a regression in a
# rejected case could fall through to install mode.
param([Parameter(Mandatory)] [string] $Exe)
$ErrorActionPreference = 'Stop'

$root = Join-Path ([IO.Path]::GetTempPath()) "dpsetup-cli-$PID"
New-Item -ItemType Directory $root | Out-Null
$defaultLog = Join-Path (Split-Path $Exe) dpsetup.log
$script:failures = 0
$script:case = 0

function Invoke-Dpsetup([string] $Arguments, [string] $WorkingDirectory = $root) {
    Write-Host "+ `"$Exe`" $Arguments"
    $p = Start-Process $Exe -ArgumentList $Arguments -WorkingDirectory $WorkingDirectory -Wait -PassThru -NoNewWindow
    $p.ExitCode
}

function New-Target {
    $script:case++
    Join-Path $root "target$script:case"
}

function Get-Extracted([string] $Dir) {
    (Get-ChildItem $Dir -File | Sort-Object Name | ForEach-Object {
        "$($_.Name) $((Get-FileHash $_.FullName -Algorithm SHA256).Hash)"
    }) -join "`n"
}

function Assert-That([bool] $Condition, [string] $What) {
    if ($Condition) { "  ok: $What" } else { "  FAILED: $What"; $script:failures++ }
}

$reference = New-Target
$exitCode = Invoke-Dpsetup "`"$reference`""
Assert-That ($exitCode -eq 0) "a bare folder extracts (exit $exitCode)"
$referenceFiles = Get-Extracted $reference

foreach ($template in @(
    '/extract "{0}"'
    '"{0}" /EXTRACT'
    '/quiet /passive /norestart "{0}"'
)) {
    $target = New-Target
    $exitCode = Invoke-Dpsetup ($template -f $target)
    Assert-That ($exitCode -eq 0 -and (Get-Extracted $target) -eq $referenceFiles) "extracts what a bare folder does (exit $exitCode)"
}

foreach ($template in @(
    '/bogus "{0}"'
    '/uninstall "{0}"'
    '/layout "{0}"'
    '/install "{0}"'
    '"{0}" "{0}-second"'
    '/extract'
    '/log'
)) {
    $target = New-Target
    $exitCode = Invoke-Dpsetup ($template -f $target)
    Assert-That ($exitCode -eq 87 -and -not (Test-Path $target) -and -not (Test-Path "$target-second")) "rejected without creating anything (exit $exitCode)"
}

Remove-Item $defaultLog -ErrorAction SilentlyContinue
$log = Join-Path $root 'absolute.log'
$exitCode = Invoke-Dpsetup "/log `"$log`" `"$(New-Target)`""
Assert-That ($exitCode -eq 0 -and (Test-Path $log) -and -not (Test-Path $defaultLog)) "/log with an absolute path replaces dpsetup.log (exit $exitCode)"

$exitCode = Invoke-Dpsetup "/log relative.log `"$(New-Target)`"" $root
Assert-That ($exitCode -eq 0 -and (Test-Path (Join-Path $root 'relative.log'))) "/log with a relative path is relative to the working directory (exit $exitCode)"

Remove-Item $root -Recurse -Force
if ($script:failures) { throw "$script:failures command line checks failed" }
