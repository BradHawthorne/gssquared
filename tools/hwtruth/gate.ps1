<#
    gate.ps1 -- the emulator still gives the answers it gave when they were recorded.

    Runs the committed hwtruth.bin on the emulator (run_emu.ps1 -Bin: no disk, no
    toolchain) and compares report.py's VERDICTS with emulator_verdicts.json -- the
    answers to the hardware questions, not the raw bytes. Some raw bytes are floating
    bus and move with any cycle-cost change; the verdicts move only when the emulator's
    answer to a question changes, which is exactly what should stop a run.

    When a verdict changes on purpose -- a hardware result came in and the emulator was
    changed to match -- re-record with -Bless and say why in the commit.

        tools\hwtruth\gate.ps1          compare
        tools\hwtruth\gate.ps1 -Bless   record the current answers
#>
[CmdletBinding()]
param([switch]$Bless)
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$work = Join-Path $env:TEMP 'a2rail-hwtruth-gate'
if (-not (Test-Path $work)) { New-Item -ItemType Directory $work -Force | Out-Null }
$page = Join-Path $work 'page.bin'
$golden = Join-Path $here 'emulator_verdicts.json'

& (Join-Path $here 'run_emu.ps1') -Bin (Join-Path $here 'hwtruth.bin') -Out $page -Dir (Join-Path $work 'rail')
if ($LASTEXITCODE -ne 0) { Write-Host "hwtruth gate: the program did not run to completion" -ForegroundColor Red; exit 1 }

$json = python (Join-Path $here 'report.py') $page --json
if ($LASTEXITCODE -ne 0) { Write-Host "hwtruth gate: report.py failed" -ForegroundColor Red; exit 1 }
$now = @(($json | Out-String | ConvertFrom-Json)[0].verdicts | ForEach-Object { [pscustomobject]@{ question = $_.question; answer = $_.answer } })

if ($Bless) {
    $now | ConvertTo-Json | Set-Content $golden -Encoding utf8
    Write-Host ("hwtruth gate: recorded {0} verdicts in {1}" -f $now.Count, $golden)
    exit 0
}
if (-not (Test-Path $golden)) { Write-Host "hwtruth gate: no emulator_verdicts.json -- run with -Bless" -ForegroundColor Red; exit 1 }
$want = @(Get-Content $golden -Raw | ConvertFrom-Json)
$fail = 0
foreach ($w in $want) {
    $n = $now | Where-Object { $_.question -eq $w.question } | Select-Object -First 1
    $got = if ($n) { $n.answer } else { '<missing>' }
    if ($got -eq $w.answer) { Write-Host ("  PASS  {0}: {1}" -f $w.question, $got) -ForegroundColor Green }
    else { Write-Host ("  FAIL  {0}: was '{1}', now '{2}'" -f $w.question, $w.answer, $got) -ForegroundColor Red; $fail++ }
}
if ($now.Count -ne $want.Count) { Write-Host ("  FAIL  {0} verdicts now, {1} recorded" -f $now.Count, $want.Count) -ForegroundColor Red; $fail++ }
if ($fail) { Write-Host "hwtruth gate: $fail verdict(s) changed" -ForegroundColor Red; exit 1 }
Write-Host ("hwtruth gate: all {0} verdicts unchanged" -f $want.Count) -ForegroundColor Green
exit 0
