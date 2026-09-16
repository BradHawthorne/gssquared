<#
    run_emu.ps1 -- run the hardware-truth program on the emulator and save its
    results page, for report.py to read (or compare with a real machine's).

        tools\hwtruth\run_emu.ps1 -Image build\hwtruth\hwtruth.po -Out emu.bin
        tools\hwtruth\run_emu.ps1 -Bin tools\hwtruth\hwtruth.bin -Out emu.bin

    -Image boots a COPY of the disk from slot 7 (the IIgs config's block
    device), exactly as a real machine boots it from its 3.5 drive: the boot
    block loads the program, and the program writes its results block into the
    copy. -Bin needs no disk and no toolchain: the ROM is left idle for 400
    frames, the committed program is loaded at $2000 and entered; nothing is
    written to a disk (RESBLK is 0). The two start from different machine state
    ($C02D, the language card, the motor), so compare like with like.

    Exits 0 when the program finished and its results were saved.
#>
[CmdletBinding()]
param(
    [string]$Image = "",
    [string]$Bin = "",
    [Parameter(Mandatory)] [string]$Out,
    [int]$Platform = 5,
    [int]$MaxFrames = 3600,
    [string]$Dir = (Join-Path $env:TEMP 'a2rail-hwtruth')
)
$ErrorActionPreference = "Stop"
$bgBefore = $env:A2GSPU_BACKGROUND_WINDOW
$env:A2GSPU_BACKGROUND_WINDOW = '1'
function Restore-Env { if ($null -eq $bgBefore) { Remove-Item Env:\A2GSPU_BACKGROUND_WINDOW -ErrorAction SilentlyContinue } else { $env:A2GSPU_BACKGROUND_WINDOW = $bgBefore } }
$tools = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) "tools\a2rail"
$ctl = Join-Path $tools "ctl.ps1"
if (-not (Test-Path $Dir)) { New-Item -ItemType Directory $Dir -Force | Out-Null }
function Say([string]$c) { return ((& $ctl -Dir $Dir -Cmd $c) -join "`n") }
function Fwd([string]$p) { return ($p -replace '\\', '/') }
if (-not $Image -and -not $Bin) { throw "give -Image or -Bin" }

if (Test-Path (Join-Path $Dir "session.json")) { Say "quit" | Out-Null; Start-Sleep -Milliseconds 1300 }
& (Join-Path $tools "session.ps1") -Dir $Dir -Platform $Platform | Out-Null

if ($Image) {
    $copy = Join-Path $Dir "hwtruth-run.po"
    Copy-Item (Resolve-Path $Image) $copy -Force
    $m = Say ("mount s7d1 {0}" -f (Fwd $copy))
    if ($m -notmatch 'status=OK') { Say "quit" | Out-Null; Restore-Env; throw "mount failed: $m" }
    Say "reset" | Out-Null
} else {
    Say "run 400" | Out-Null
    $l = Say ("load 00:2000 {0}" -f (Fwd (Resolve-Path $Bin).Path))
    if ($l -notmatch 'status=OK') { Say "quit" | Out-Null; Restore-Env; throw "load failed: $l" }
    Say "setreg e 1" | Out-Null
    Say "setreg pc 002000" | Out-Null
}

$page = Join-Path $Dir "page.bin"
$done = $false
for ($f = 0; $f -lt $MaxFrames; $f += 60) {
    Say "run 60" | Out-Null
    if (Test-Path $page) { Remove-Item $page -Force }
    Say ("read 00:1000 512 {0}" -f (Fwd $page)) | Out-Null
    $b = [IO.File]::ReadAllBytes($page)
    if ($b.Length -eq 512 -and $b[0] -eq 0x48 -and $b[1] -eq 0x57 -and $b[2] -eq 0x54 -and $b[5] -eq 1) { $done = $true; break }
}
Say "quit" | Out-Null
Start-Sleep -Milliseconds 1500
Restore-Env
if (-not $done) { Write-Host "hwtruth: the program did not finish within $MaxFrames frames"; exit 1 }
Copy-Item $page $Out -Force
Write-Host ("hwtruth: finished after about {0} frames; results page saved to {1}" -f ($f + 60), $Out)
if ($Image) { Write-Host ("hwtruth: the booted copy (with its RESULTS block) is {0}" -f $copy) }
exit 0
