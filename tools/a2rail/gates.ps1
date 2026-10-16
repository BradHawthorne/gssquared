<#
    gates.ps1 -- every fork-local gate, one command, one matrix.

    WHY THIS EXISTS

    The fork had eight gate scripts and dualsmoke ran three of them. assertparity.ps1 --
    which Docs/A2GSPU_RAILS.md calls "the only check that can see" env/CTRL assert
    divergence -- was run by nothing. protocolcheck could not pass at all for a while and
    nobody noticed, because nothing called it. A gate a human has to remember to run is a
    gate that is not run. So this runs all of them:

      offline   railparity           registry <-> code <-> catalogue <-> dispatcher
                artifacts selftest   the one artifact reader refuses what it should
                gdiff selftest       the differential tools against real captures
                buscompare selftest  the slot-bus comparator finds gaps, extras and field mismatches
                bisect selftest      real scratch builds find a planted revision, reversions and inconclusive failures
      emulator  dualsmoke            ctest + railcheck/execcheck/altzpcheck on IIe AND IIgs
                agentic              SCC capture, WDM console, counts/profiles, socket manifest, snapshot diffs (positive and negative cases)
                personalitycheck     every built-in machine configuration cold-starts
                protocolcheck        protocol v2, discovery, JSON replies, ownership, SHR golden
                copilotcheck         the windowed agent rail (starts a window that cannot take focus)
                assertparity         the env and CTRL assert surfaces give the same verdicts
                unarmedhash          stock emulation unchanged; armed observers change nothing
                hwtruth              the emulator's answers to the hardware questions in
                                     Docs/fidelity/README.md are the ones last recorded
                                     (tools/hwtruth: the same program runs on a real IIgs)
                mbaudit              two Mockingboards pass MB-Audit v1.61, whose expected
                                     values come from real cards (corpus image)

    Suites run one at a time -- they share nothing, but two emulator suites overlapping on
    one box have collided before (a shared temp directory, a command-file lock). About
    seven minutes on the development machine.

      .\tools\a2rail\gates.ps1                  # everything
      .\tools\a2rail\gates.ps1 -Offline         # the five offline gates (bisect compiles tiny fixtures)
      .\tools\a2rail\gates.ps1 -Skip copilot    # name suites to leave out; they are REPORTED as skipped
      .\tools\a2rail\gates.ps1 -SkipCorpus      # unarmedhash without the GS/OS image (PARTIAL), mbaudit skipped

    Exit 0 only when every suite that ran returned 0. A skipped suite is printed as SKIPPED
    in the matrix and in the exit line, never folded into the green.

    Not covered here, and said so: the windowed emulator with no instrumentation environment
    (a hand check, AGENTS.md rule 5), and a2engine's full device net (gscheck.ps1), which is
    a consumer's suite and lives there.
#>
[CmdletBinding()]
param(
    [switch]$Offline,
    [string[]]$Skip = @(),
    [switch]$SkipCorpus,
    [string]$Report = (Join-Path $env:TEMP 'a2rail-gates.ndjson')
)
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
. (Join-Path $here 'toolpin.ps1')
. (Join-Path $here 'runtime.ps1')
$null = Initialize-A2RailRuntime
$root = Get-GssquaredRoot
Remove-Item $Report -ErrorAction SilentlyContinue
# What the caller's shell already had: a suite is blamed only for what IT adds or changes.
$envBefore = @{}
Get-ChildItem Env: | Where-Object { $_.Name -match '^(A2GSPU_|MEMVU_|WIZ5_)' } | ForEach-Object { $envBefore[$_.Name] = $_.Value }

$suites = [ordered]@{
    'railparity'   = @{ offline = $true;  run = { & (Join-Path $here 'railparity.ps1') } }
    'artifacts'    = @{ offline = $true;  run = { python (Join-Path $here 'artifacts.py') selftest } }
    'gdiff'        = @{ offline = $true;  run = { python (Join-Path $root 'tools\gdiff\selftest.py') } }
    'buscompare'   = @{ offline = $true;  run = { python (Join-Path $here 'buscompare.py') selftest } }
    'bisect'       = @{ offline = $true;  run = { & (Join-Path $here 'bisect.ps1') -SelfTest } }
    'agentic'      = @{ offline = $false; run = { python -I (Join-Path $here 'p0check.py') --exe (Get-GSSquaredExe) } }
    'dualsmoke'    = @{ offline = $false; run = { & (Join-Path $here 'dualsmoke.ps1') } }
    'personality'  = @{ offline = $false; run = { & (Join-Path $here 'personalitycheck.ps1') } }
    'protocol'     = @{ offline = $false; run = { & (Join-Path $here 'protocolcheck.ps1') } }
    'copilot'      = @{ offline = $false; run = { & (Join-Path $here 'copilotcheck.ps1') } }
    'assertparity' = @{ offline = $false; run = { & (Join-Path $here 'gs816\assertparity.ps1') } }
    'unarmedhash'  = @{ offline = $false; run = { if ($SkipCorpus) { & (Join-Path $here 'unarmedhash.ps1') -SkipCorpus } else { & (Join-Path $here 'unarmedhash.ps1') } } }
    'hwtruth'      = @{ offline = $false; run = { & (Join-Path $root 'tools\hwtruth\gate.ps1') } }
    'mbaudit'      = @{ offline = $false; corpus = $true; run = { & (Join-Path $here 'mbaudit.ps1') } }
}

$matrix = @()
foreach ($name in $suites.Keys) {
    $s = $suites[$name]
    $why = $null
    if ($Offline -and -not $s.offline) { $why = '-Offline' }
    elseif ($Skip -contains $name) { $why = '-Skip' }
    elseif ($SkipCorpus -and $s.corpus) { $why = '-SkipCorpus' }
    if ($why) {
        $matrix += [pscustomobject]@{ suite = $name; outcome = 'SKIPPED'; rc = ''; seconds = 0; note = $why }
        continue
    }
    Write-Host ''
    Write-Host ("=== GATE {0} " -f $name).PadRight(64, '=') -ForegroundColor Cyan
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $global:LASTEXITCODE = 0
    $rc = 0
    try {
        & $s.run
        $rc = $LASTEXITCODE
    } catch {
        Write-Host $_ -ForegroundColor Red
        $rc = 99
    }
    $sw.Stop()
    # A suite that leaks A2GSPU_CTRL into this shell attaches the next suite's emulator to its
    # rail directory -- it happened (see session.ps1). Fail the leaker, and clear it.
    $leak = @(Get-ChildItem Env: | Where-Object {
        $_.Name -match '^(A2GSPU_|MEMVU_|WIZ5_)' -and (-not $envBefore.ContainsKey($_.Name) -or $envBefore[$_.Name] -ne $_.Value)
    })
    $note = ''
    if ($leak.Count) {
        $note = 'leaked env: ' + (($leak | ForEach-Object Name) -join ',')
        foreach ($l in $leak) {
            if ($envBefore.ContainsKey($l.Name)) { Set-Item ("Env:" + $l.Name) $envBefore[$l.Name] }
            else { Remove-Item ("Env:" + $l.Name) }
        }
        if ($rc -eq 0) { $rc = 98 }
    }
    $outcome = if ($rc -eq 0) { 'PASS' } else { 'FAIL' }
    $matrix += [pscustomobject]@{ suite = $name; outcome = $outcome; rc = $rc; seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1); note = $note }
    $ev = @{ schema = 'a2gates-event-v1'; ts = [DateTime]::UtcNow.ToString('o'); suite = $name; outcome = $outcome; rc = $rc; seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1); note = $note }
    [IO.File]::AppendAllText($Report, (($ev | ConvertTo-Json -Compress) + [Environment]::NewLine))
}

Write-Host ''
Write-Host '=== GATES MATRIX ===============================================' -ForegroundColor Cyan
foreach ($m in $matrix) {
    $color = switch ($m.outcome) { 'PASS' { 'Green' } 'FAIL' { 'Red' } default { 'Yellow' } }
    Write-Host ("  {0,-8} {1,-14} {2,6}s  {3}" -f $m.outcome, $m.suite, $m.seconds, $m.note) -ForegroundColor $color
}
$failed  = @($matrix | Where-Object outcome -eq 'FAIL')
$skipped = @($matrix | Where-Object outcome -eq 'SKIPPED')
$ran     = @($matrix | Where-Object outcome -ne 'SKIPPED')
Write-Host ("report: {0}" -f $Report)
if ($failed.Count) {
    Write-Host ("{0} of {1} gate(s) FAILED: {2}" -f $failed.Count, $ran.Count, (($failed | ForEach-Object suite) -join ', ')) -ForegroundColor Red
    exit 1
}
if ($skipped.Count) {
    Write-Host ("GREEN for the {0} gate(s) that ran -- {1} SKIPPED: {2}" -f $ran.Count, $skipped.Count, (($skipped | ForEach-Object suite) -join ', ')) -ForegroundColor Yellow
    exit 0
}
Write-Host ("ALL GREEN -- {0} gates" -f $ran.Count) -ForegroundColor Green
exit 0
