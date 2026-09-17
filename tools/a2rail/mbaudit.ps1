<#
    mbaudit.ps1 -- the Mockingboard passes Tom Charlesworth's MB-Audit.

    WHY THIS EXISTS

    MB-Audit (github.com/tomcw/mb-audit) tests the 6522 VIAs and AY-3-8913s of a
    Mockingboard to the cycle: timer loads, reads just before and after underflow, small
    latch values in both timer modes, the N+2 free-run period, every instruction that can
    write a timer, IRQ flags, two cards at once, and Ctrl-Reset. Its expected values come
    from real cards. The fork's fidelity study once listed 6522 timing defects read from
    mb.cpp, which the build does not compile; the live 6522 (W6522.hpp) passes this audit,
    and this gate keeps it passing.

    It boots MB-Audit v1.61 on the built-in "Apple IIe Enhanced Dual Mockingboard"
    configuration, runs the quick audit, answers the two Ctrl-Reset prompts (the rail's
    warm reset) and skips the two interactive tone tests with ESC. It passes only on
    "All tests passed (total=$000782)": the count pins the version and says every test ran.

    The image is a local corpus input (corpus/README.md), not redistributed:

      .\tools\a2rail\mbaudit.ps1                          # <repo>\corpus\mb-audit-v1.61.po
      .\tools\a2rail\mbaudit.ps1 -Image D:\x\mb-audit.po  # or A2RAIL_MBAUDIT_IMAGE
#>
[CmdletBinding()]
param(
    [string]$Image = $env:A2RAIL_MBAUDIT_IMAGE,
    [int]$Config = 5,
    [int]$Frames = 12000,
    [string]$Dir = (Join-Path $env:TEMP 'a2rail-mbaudit-gate')
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
. (Join-Path $here 'toolpin.ps1')
$root = Get-GssquaredRoot
if (-not $Image) { $Image = Join-Path $root 'corpus\mb-audit-v1.61.po' }
$want = 'All tests passed (total=$000782)'

Write-Host ''
Write-Host '=== MB-AUDIT (Mockingboard 6522 + AY, two cards) ==================='
if (-not (Test-Path $Image)) {
    Write-Host "  FAIL  no MB-Audit image at $Image (see corpus/README.md, or pass -Image / A2RAIL_MBAUDIT_IMAGE)" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $Dir)) { New-Item -ItemType Directory $Dir -Force | Out-Null }
$ctl = Join-Path $here 'ctl.ps1'
function Say([string]$c) { return ((& $ctl -Dir $Dir -Cmd $c) -join "`n") }
function Screen() {
    Say 'screen' | Out-Null
    $f = Join-Path $Dir 'screen.txt'
    if (Test-Path $f) { return (Get-Content $f) -join "`n" }
    return ''
}
if (Test-Path (Join-Path $Dir 'session.json')) { Say 'quit' | Out-Null; Start-Sleep -Milliseconds 1500 }
$disk = Join-Path $Dir 'mb-audit.po'
Copy-Item $Image $disk -Force

& (Join-Path $here 'session.ps1') -Dir $Dir -Platform 3 -Config $Config | Out-Null
$ok = $false; $last = ''
try {
    $m = Say ('mount s6d1 {0}' -f $disk.Replace('\', '/'))
    if ($m -notmatch 'status=OK') { throw "mount failed: $m" }
    Say 'reset' | Out-Null
    for ($f = 0; $f -lt 1200; $f += 60) { Say 'run 60' | Out-Null; if ((Screen) -match 'SELECT:') { break } }
    Say 'key 41' | Out-Null                        # (A) AUDIT & DIAGNOSTICS - QUICK
    $prompts = 0; $tone = ''
    for ($f = 0; $f -lt $Frames; $f += 120) {
        Say 'run 120' | Out-Null
        $s = Screen; $last = $s
        # one warm reset per new "Press CTRL+RESET" line (earlier prompts stay on the screen)
        $n = @($s -split "`n" | Where-Object { $_ -match 'Press CTRL\+RESET' }).Count
        if ($n -gt $prompts) { $prompts = $n; Say 'reset' | Out-Null; continue }
        # the tone test waits for keys; ESC leaves each card's
        $t = [string](($s -split "`n") | Where-Object { $_ -match ':Tone:' } | Select-Object -Last 1)
        if ($t -and $t -ne $tone) { $tone = $t; Say 'key 1B' | Out-Null; continue }
        if ($s -match 'Press any key to reboot') { break }
    }
    $ok = $last.Contains($want)
} finally {
    Say 'quit' | Out-Null
    Start-Sleep -Milliseconds 1500
}
$lines = @($last -split "`n" | Where-Object { $_ -match '^(TEXT|junk) T\d\d \|' } | ForEach-Object { '        ' + $_ })
if ($ok) {
    Write-Host "  PASS  MB-Audit: $want (Ctrl-Reset prompts answered: $prompts)" -ForegroundColor Green
    exit 0
}
Write-Host "  FAIL  MB-Audit did not report '$want'; the last screen:" -ForegroundColor Red
$lines | ForEach-Object { Write-Host $_ }
exit 1
