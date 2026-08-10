<#
    execcheck.ps1 -- execution control: run, step, bp, resume.

    These are the verbs everything else is built on. A gate that measures a
    device is worthless if the `run` underneath it did not actually run, so this
    suite deliberately sits upstream of every other suite in gscheck.

    THE BUG THIS EXISTS FOR. `step` and `run-until` called arm_cpu() before
    executing; `run` never did. execute_next() no-ops while reset_asserted is
    set, and a fresh IIe session comes up with it still asserted -- the CTRL rail
    replaces the frame path that would normally clear it. So on platform 3:

        poke 00:6100 EA EA EA 4C 03 61
        setreg pc 6100
        run 2   ->  status=OK ran=2 cycles=34056 (in-command) PC $6100->$6100

    Two frames, 34,056 cycles of clock, and ZERO instructions executed. PC never
    moved. Reported as success. The existing RAN-NOTHING guard could not catch it
    because that guard keys off elapsed cycles and the cycles were perfectly
    real -- it was only the instructions that were missing.

    Worse, it was platform-dependent: the IIgs comes up without reset asserted,
    so the identical script worked on platform 5. Anything that failed on the IIe
    and passed on the IIgs would read as a machine difference rather than as the
    rail declining to execute. That is why this suite runs on BOTH.

    WHY IT MATTERS BEYOND ITSELF: `run` is the most-used verb on the rail. A
    breakpoint cannot fire in a frame where no instruction executes, so `bp`
    looked broken on the IIe too. One unarmed CPU made two instruments look dead.

    THE OTHER HALF -- getting moving again. A fired breakpoint leaves
    cpu->halt = HLT_USER and run_one_frame's top-of-frame guard then refuses to
    advance, so every later `run` answered "status=HALT ran=0 cycles=0" forever.
    The only escape on the entire rail was `reset`, which discards the state you
    stopped to look at. An interactive breakpoint you cannot continue from is a
    one-way door. run/step/run-until now clear a USER halt and say so; `resume`
    does it explicitly; and neither will paper over HLT_INSTRUCTION, which is the
    guest CPU genuinely jammed and a real machine state an agent must be able
    to see.
#>
[CmdletBinding()]
param(
    [int]$Platform = 3,
    [string]$Dir = $(if ($env:A2RAIL_DIR) { $env:A2RAIL_DIR } else { Join-Path $env:TEMP 'a2rail' })
)
$ErrorActionPreference = "Stop"
$ctl  = Join-Path (Split-Path $PSScriptRoot -Parent) "ctl.ps1"
function Say([string]$c) { return ((& $ctl -Dir $Dir -Cmd $c) -join "`n") }
$pass = 0; $fail = 0
function Check([string]$n,[bool]$ok,[string]$d="") {
    if ($ok) { Write-Host ("  PASS  {0,-34} {1}" -f $n,$d) -ForegroundColor Green; $script:pass++ }
    else     { Write-Host ("  FAIL  {0,-34} {1}" -f $n,$d) -ForegroundColor Red;   $script:fail++ }
}
function PC() { if ((Say "cpu") -match "PC=([0-9A-F]{2}):([0-9A-F]{4})") { return $Matches[2] } return "????" }

Write-Host ""
Write-Host ("=== EXEC CONTROL -- platform {0} " -f $Platform).PadRight(64,'=') -ForegroundColor Cyan

# A FRESH session is the whole point: arm_cpu's effect persists, so any earlier
# step/run-until in this emulator would hide the defect.
if (Get-Process -Name GSSquared -ErrorAction SilentlyContinue) { Say "quit" | Out-Null; Start-Sleep -Milliseconds 1300 }
& (Join-Path (Split-Path $PSScriptRoot -Parent) "session.ps1") -Dir $Dir -Platform $Platform | Out-Null

# NOP NOP NOP JMP-self at $6100. $6103 is executed on every iteration forever.
$prog = "poke 00:6100 EA EA EA 4C 03 61"

# --- 1. run, on a cold machine, actually executes -------------------------
Say $prog | Out-Null
Say "setreg pc 6100" | Out-Null
Check "PC parked at 6100" ((PC) -eq "6100") ""
$r = Say "run 2"
Check "run reports OK"     ($r -match "status=OK ran=2") ""
Check "run MOVED PC"       ((PC) -eq "6103") ("PC=`${0} -- the arm bug left this at 6100" -f (PC))
Check "run ack shows PC"   ($r -match 'PC \$6100->\$6103') "the ack must not claim motion the CPU did not make"

# --- 2. step is instruction-granular and agrees with run ------------------
Say "setreg pc 6100" | Out-Null
Say "step 1" | Out-Null
Check "step 1 advances one"  ((PC) -eq "6101") ""
Say "step 2" | Out-Null
Check "step 2 advances two"  ((PC) -eq "6103") ""

# --- 3. bp: the contract, then the behaviour ------------------------------
Check "bare bp reports off"  ((Say "bp") -match "status=OK bp off") "was: no status= prefix at all"
$b = Say "bp zzzz"
Check "bad bp FAILs w/ status" ($b -match "status=FAIL bp-parse-fail") $b
Check "bp armed has status="   ((Say "bp 6103") -match "status=OK bp @00/6103 armed") ""
Check "bare bp reports armed"  ((Say "bp") -match "status=OK bp @00/6103 armed") ""

# --- 4. a breakpoint fires under run (it could not, unarmed) --------------
Say "setreg pc 6100" | Out-Null
$r = Say "run 2"
Check "bp halts a run"       ($r -match "status=HALT") $r
Check "halted AT the bp"     ((PC) -eq "6103") ""
Check "cpu reports HALT=1"   ((Say "cpu") -match "HALT=1") ""

# --- 5. and the machine can be got moving again ---------------------------
# The one-way door. Without this an agent that sets a breakpoint is finished.
$r = Say "run 2"
Check "run resumes past a bp" ($r -match "resumed-from-halt") $r
$r = Say "resume"
Check "resume is idempotent"  ($r -match "status=OK resume") $r
Say "bp off" | Out-Null
Check "bp off has status="    ((Say "bp") -match "status=OK bp off") ""
Say "setreg pc 6100" | Out-Null
Say "resume" | Out-Null
$r = Say "run 2"
Check "run is clean after off" ($r -match "status=OK ran=2") $r
Check "and still executes"     ((PC) -eq "6103") ""

# --- 6. a breakpoint fires under step, too --------------------------------
Say "setreg pc 6100" | Out-Null
Say "bp 6103" | Out-Null
$r = Say "step 8"
Check "bp halts a step run"  ((Say "cpu") -match "HALT=1") $r
Check "step stopped short"   ($r -match "stepped=[1-7] ") ("8 asked; " + $(if ($r -match "stepped=(\d+)") { $Matches[1] } else { "?" }) + " taken")
Say "bp off" | Out-Null
Say "resume" | Out-Null

Write-Host ""
if ($fail -eq 0) { Write-Host ("ALL GREEN -- {0} exec-control checks" -f $pass) -ForegroundColor Green; exit 0 }
Write-Host ("{0} of {1} exec-control checks FAILED" -f $fail, ($pass+$fail)) -ForegroundColor Red
exit 1
