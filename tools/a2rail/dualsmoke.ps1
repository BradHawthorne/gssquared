<#
    dualsmoke.ps1 -- short dual-platform gate for shared rail/instrument work.

    Docs/DUAL_PLATFORM.md: IIe-green is not ship-green for shared gssquared
    code. This is the command agents and humans run after a rail/MMU/device
    change that is not sealed to one platform.

        .\tools\dualsmoke.ps1
        .\tools\dualsmoke.ps1 -SkipOffline
        .\tools\dualsmoke.ps1 -KillStrays   # ALSO reap GSSquared processes this
                                            # run started but never declared

    -KillStrays is opt-in on purpose: see the comment on Get-SuiteEmulators.
    Without it the reaper kills only the pid in each suite's session.json, so
    running this gate cannot take out another agent's or a human's emulator --
    and a stray is REPORTED rather than quietly swept away.

    This stack is owned by gssquared-fork (tools/a2rail/). a2engine keeps a pulled copy:
    edit here, then in a2engine run .\tools\sync-a2rail.ps1 to refresh it. The broader
    fork-local gate set is tools\a2rail\gates.ps1, which runs this plus the rest.

    Suites (intentionally small -- full gscheck remains the deep net):

      offline     gssquared ctest (no emulator)
      rail IIe    railcheck -Platform 3
      rail IIgs   railcheck -Platform 5
      exec IIe    execcheck -Platform 3
      exec IIgs   execcheck -Platform 5
      altzp IIe   altzpcheck -Platform 3
      altzp IIgs  altzpcheck -Platform 5

    Exit 0 only if every suite returns 0.
#>
[CmdletBinding()]
param(
    [switch]$SkipOffline,
    # Opt-in widening of the reaper: also kill GSSquared processes whose parent
    # chain reaches this wrapper but which no session.json named. Off by default
    # because a reaper that guesses is how this suite used to kill strangers.
    [switch]$KillStrays,
    [string]$Dir = $(if ($env:A2RAIL_DIR) { $env:A2RAIL_DIR } else { Join-Path $env:TEMP 'a2rail' }),
    [string]$Report = (Join-Path $(if ($env:A2RAIL_DIR) { $env:A2RAIL_DIR } else { Join-Path $env:TEMP 'a2rail' }) 'dualsmoke.ndjson')
)
$ErrorActionPreference = "Continue"
$here = $PSScriptRoot
$results = @()

function Write-SuiteEvent([hashtable]$Event) {
    $Event.schema='a2suite-event-v1'; $Event.ts=[DateTime]::UtcNow.ToString('o')
    [IO.File]::AppendAllText($Report, (($Event | ConvertTo-Json -Compress -Depth 4) + [Environment]::NewLine))
    Write-Host ("SUITE {0} {1}" -f $Event.event.ToUpperInvariant(),$Event.suite) -ForegroundColor DarkCyan
}

# WHICH EMULATORS MAY THIS WRAPPER KILL? Only the ones it caused to exist.
#
# The sweep here used to match by process NAME across the whole machine: snapshot
# `Get-Process GSSquared` before a suite, kill anything new after it. That is not
# a leak detector, it is a machine-wide reaper. It destroyed any emulator a human
# had open while the gate ran, and two concurrent dualsmoke runs each killed the
# other's sessions and then printed the wreckage as a check FAILURE -- the loudest
# possible way to blame the emulator for the harness.
#
# Ownership is proved two ways instead, and neither can reach a process this
# wrapper did not start:
#   1. DECLARED -- the suite's own $Dir/session.json, which session.ps1 writes
#      with the pid it started. This is the only set killed by default.
#   2. STRAY -- a GSSquared.exe whose parent chain reaches THIS wrapper ($PID)
#      but that no session.json declared. Provably ours, but it is also a real
#      harness bug: something started an emulator and did not record it. Reported
#      every run; killed only under -KillStrays, because silently reaping it is
#      how the leak stays invisible for another year.
# Not cwd and not CommandLine: Win32_Process exposes no working directory, and
# session.ps1 hands $Dir to the emulator in the A2GSPU_CTRL environment variable
# rather than on the command line, so neither could tell ours from a stranger's.
function Get-SuiteEmulators([string]$suiteDir) {
    $declared = @{}

    if ($suiteDir) {
        $sf = Join-Path $suiteDir 'session.json'
        if (Test-Path $sf) {
            # A truncated session.json is a missing claim, not a crash: fall
            # through to the stray scan rather than aborting the whole run.
            try {
                $s = Get-Content $sf -Raw | ConvertFrom-Json
                if ($s.pid) { $declared[[int]$s.pid] = $true }
            } catch { }
        }
    }

    $strays = @{}
    $wrapperStart = (Get-Process -Id $PID).StartTime
    $emus = @(Get-CimInstance Win32_Process -Filter "Name='GSSquared.exe'" -ErrorAction SilentlyContinue)
    if ($emus.Count) {
        $byPid = @{}
        foreach ($p in @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)) {
            $byPid[[int]$p.ProcessId] = $p
        }
        foreach ($p in $emus) {
            if ($declared[[int]$p.ProcessId]) { continue }
            # Pids are recycled. Anything that existed before this wrapper did
            # cannot be its descendant, whatever ParentProcessId now points at.
            if ($p.CreationDate -and $p.CreationDate -lt $wrapperStart) { continue }
            # Bounded walk: session.ps1 -> pwsh -> wrapper is the deepest real
            # chain here, and an unbounded walk over recycled pids is exactly how
            # a sweep starts killing strangers again.
            $cur = [int]$p.ParentProcessId
            for ($hop = 0; $hop -lt 4 -and $cur -gt 0; $hop++) {
                if ($cur -eq $PID) { $strays[[int]$p.ProcessId] = $true; break }
                $parent = $byPid[$cur]
                if (-not $parent) { break }
                $cur = [int]$parent.ParentProcessId
            }
        }
    }
    return @{ declared = @($declared.Keys); strays = @($strays.Keys) }
}

function Run-One([string]$name, [scriptblock]$body, [string]$suiteDir = "") {
    Write-Host ""
    Write-Host ("##### {0} " -f $name).PadRight(64, '#') -ForegroundColor Cyan
    $started=[DateTime]::UtcNow
    Write-SuiteEvent @{event='start';suite=$name;wrapper_pid=$PID;dir=$suiteDir}
    & $body
    $suiteExit=$LASTEXITCODE; $ok = ($suiteExit -eq 0)
    # Exit 3 is session.ps1's "the rail process was TERMINATED externally" code.
    # Somebody else's kill landing on us is not a check result, and printing it
    # as FAIL beside real failures is how a machine-wide reaper hides its damage.
    $outcome = if ($ok) { 'pass' } elseif ($suiteExit -eq 3) { 'aborted-externally' } else { 'fail' }
    # `killed_processes` is named for what it is: a list of processes this suite
    # DESTROYED. The old name `leaked_processes` read as diagnostics, which hid
    # the fact that half the entries were other people's emulators.
    $found = Get-SuiteEmulators $suiteDir
    $toKill = @($found.declared)
    if ($KillStrays) { $toKill += @($found.strays) }
    $killed=@()
    foreach($opid in $toKill){
        if (Get-Process -Id $opid -ErrorAction SilentlyContinue) {
            Stop-Process -Id $opid -Force -ErrorAction SilentlyContinue
            $killed += $opid
        }
    }
    # A stray this wrapper started but nothing declared is a leak in the harness.
    # Say so out loud -- it survives into the next suite otherwise, and a quiet
    # kill would have made the leak unobservable, which is the whole complaint.
    if ($found.strays.Count -and -not $KillStrays) {
        Write-Host ("##### LEAKED: {0} left GSSquared pid(s) {1} running that no session.json declared -- rerun with -KillStrays to reap them" -f $name, ($found.strays -join ',')) -ForegroundColor Yellow
    }
    $ms=[int]([DateTime]::UtcNow-$started).TotalMilliseconds
    $script:results += [pscustomobject]@{ Suite = $name; Pass = $ok; Outcome = $outcome; DurationMs=$ms }
    Write-SuiteEvent @{event='end';suite=$name;pass=$ok;outcome=$outcome;exit_code=$suiteExit;duration_ms=$ms;killed_processes=$killed;stray_processes=@($found.strays)}
    if ($outcome -eq 'aborted-externally') {
        Write-Host ("##### ABORTED EXTERNALLY: {0} -- rail process was killed by something outside this run" -f $name) -ForegroundColor Yellow
    } elseif (-not $ok) {
        Write-Host ("##### FAILED: {0}" -f $name) -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "=== DUALSMOKE (IIe + IIgs instrument gate) ".PadRight(64, '=') -ForegroundColor Cyan
Write-Host "  Docs/DUAL_PLATFORM.md -- shared rail changes need both platforms" -ForegroundColor DarkGray

. (Join-Path $here "toolpin.ps1")
. (Join-Path $here "runtime.ps1")
$null = Initialize-A2RailRuntime
$reportDir=Split-Path $Report -Parent
if($reportDir -and -not(Test-Path $reportDir)){New-Item -ItemType Directory $reportDir -Force|Out-Null}
if(Test-Path $Report){Remove-Item $Report -Force}
Write-SuiteEvent @{event='run-start';suite='dualsmoke';report=$Report;wrapper_pid=$PID}
try { $null = Write-ToolPin -Label "GSSquared" -Exe (Get-GSSquaredExe) -RepoHint "gssquared" } catch {}

if (-not $SkipOffline) {
    $gsBuild = Join-Path (Get-GssquaredRoot) "build"
    Run-One "gssquared ctest (offline)" {
        Push-Location $gsBuild
        & ctest --output-on-failure
        Pop-Location
    }
    # Rails must be discoverable (AGENTS.md rule 6). Nothing checked that, and
    # the catalogue drifted to 96 of 128 -- so the gate is here, not in habit.
    # Through the fork root, not $here: a2engine runs a pulled copy of this script, and
    # railparity checks the FORK's source wherever it is called from.
    Run-One "rail parity (offline)" { & (Join-Path (Get-GssquaredRoot) "tools\a2rail\railparity.ps1") }
}

# ONE PROTOCOL DIRECTORY PER SUITE. All six used to share $Dir, so two dualsmoke
# runs at once interleaved their cmd.N/ack.N files in the same folder and each
# read the other's replies -- and with one session.json between them, "whose
# emulator is this?" had no answer at all. A per-suite directory makes ownership
# a fact on disk. The NDJSON report stays at the top level, one per run.
$dRail3  = Join-Path $Dir 'railcheck-p3'
$dRail5  = Join-Path $Dir 'railcheck-p5'
$dExec3  = Join-Path $Dir 'exec-p3'
$dExec5  = Join-Path $Dir 'exec-p5'
$dAltzp3 = Join-Path $Dir 'altzp-p3'
$dAltzp5 = Join-Path $Dir 'altzp-p5'

Run-One "railcheck (IIe / platform 3)"  { & (Join-Path $here "railcheck.ps1") -Platform 3 -Dir $dRail3 }  $dRail3
Run-One "railcheck (IIgs / platform 5)" { & (Join-Path $here "railcheck.ps1") -Platform 5 -Dir $dRail5 }  $dRail5
Run-One "exec control (IIe)"            { & (Join-Path $here "gs816\execcheck.ps1") -Platform 3 -Dir $dExec3 }  $dExec3
Run-One "exec control (IIgs)"           { & (Join-Path $here "gs816\execcheck.ps1") -Platform 5 -Dir $dExec5 }  $dExec5
Run-One "ALTZP/LC (IIe)"                { & (Join-Path $here "gs816\altzpcheck.ps1") -Platform 3 -Dir $dAltzp3 } $dAltzp3
Run-One "ALTZP/LC (IIgs)"               { & (Join-Path $here "gs816\altzpcheck.ps1") -Platform 5 -Dir $dAltzp5 } $dAltzp5

Write-Host ""
Write-Host "=== DUALSMOKE MATRIX ".PadRight(64, '=') -ForegroundColor Cyan
$fail = 0
$aborted = 0
# ABORT is its own row, not a FAIL: an externally killed rail proves nothing
# either way, and filing it under FAIL sends the reader hunting an emulator bug
# that is really a second harness (or a human) on the same box.
foreach ($r in $results) {
    if ($r.Pass) { $tag = "PASS"; $col = "Green" }
    elseif ($r.Outcome -eq 'aborted-externally') { $tag = "ABORT"; $col = "Yellow"; $aborted++ }
    else { $tag = "FAIL"; $col = "Red"; $fail++ }
    Write-Host ("  {0}  {1}" -f $tag, $r.Suite) -ForegroundColor $col
}
Write-Host ""
if ($fail -eq 0 -and $aborted -eq 0) {
    Write-SuiteEvent @{event='run-end';suite='dualsmoke';pass=$true;total=$results.Count;failed=0;aborted=0}
    Write-Host ("ALL GREEN -- {0} dual-platform suites" -f $results.Count) -ForegroundColor Green
    exit 0
}
Write-SuiteEvent @{event='run-end';suite='dualsmoke';pass=$false;total=$results.Count;failed=$fail;aborted=$aborted}
if ($aborted -gt 0) {
    Write-Host ("{0} FAILED, {1} ABORTED EXTERNALLY, of {2} dual-platform suites -- an ABORT is not a verdict; re-run with nothing else touching GSSquared" -f $fail, $aborted, $results.Count) -ForegroundColor Yellow
} else {
    Write-Host ("{0} of {1} dual-platform suites FAILED" -f $fail, $results.Count) -ForegroundColor Red
}
exit 1
