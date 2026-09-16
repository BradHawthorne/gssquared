<#
    assertparity.ps1 -- the CTRL `assert` verb and the A2GSPU_ASSERT env rail
    must return the SAME VERDICT for the same expression.

    THE BUG THIS EXISTS FOR. They are one rail: one registry row, one paragraph
    in Docs/A2GSPU_RAILS.md, one name. They were two implementations, and
    against the same binary, in the same minute, they disagreed five ways:

        assert peek:FFFA62==A9      CTRL: PASS       env: (== 169) FAIL
        assert peek:FFFA62==$A9     CTRL: parse-fail env: PASS
        assert peek:FFFA62==169     CTRL: PASS on $69 (silent truncation)
        assert scb_mode==0          CTRL: parse-fail env: evaluates
        assert peek:00C029==EE      CTRL: PASS       env: refuses -- the page is
                                    handler-backed, so CTRL passed on floating
                                    bus noise
        (and CTRL printed got=A9 where env printed actual=169 -- the same byte
         in two bases, so even the transcripts could not be diffed)

    WHY NOTHING CAUGHT IT. Each surface was entirely self-consistent. Every
    single-surface test passes on both. The defect only exists BETWEEN them, so
    the only instrument that can see it is one that runs both and compares --
    which is this script, and it is why a shared primitive is not on its own a
    fix. Two callers of one helper still drift in the code around the helper.

    WHAT IS COMPARED, AND WHAT DELIBERATELY IS NOT. The two surfaces observe
    different machines: this runs a short headless spike and a cold CTRL
    session, so any clause whose answer depends on how far the machine has got
    is NOT comparable and would make this gate flaky rather than strict. So the
    expression list is chosen to be state-independent:

      * grammar and refusal contract -- an unknown field, a single-`=` typo, an
        ambiguous hex comparand, a handler-backed address. These must come back
        UNEVALUATED on both, and "unevaluated" is the third outcome the CTRL
        ack used to merge into `fail=1`.
      * operators over a tautology -- `peek:X >= 0d0` is true of every byte and
        `peek:X < 0d0` is false of every byte, so the six-operator set is
        exercised without pinning a single transcribed constant.
      * NO transcribed ROM byte anywhere. Instead the two surfaces are made to
        PRINT the byte they read at a ROM address (ROM is the one thing that
        cannot differ between them) and the printed text must match exactly.
        That is the check for the base divergence: `$A9 (169)` on both, or red.

    ONE KNOWN, DECLARED DIVERGENCE. `nonzero` / `distinct` are statistics the
    spike computes over its SHR window; the live rail does not compute them and
    answers UNEVALUATED. That is a real difference in what the two surfaces can
    see, not a drift, so it is asserted explicitly here -- if it ever silently
    becomes equal, something started inventing a number.

    Not yet wired into dualsmoke.ps1 (that file belongs to another change in
    flight). Run it directly, or add it beside the execcheck rows.
#>
[CmdletBinding()]
param(
    [int]$Platform = 5,
    [int]$Frames   = 20,
    [string]$Dir = $(if ($env:A2RAIL_DIR) { $env:A2RAIL_DIR } else { Join-Path $env:TEMP 'a2rail-assertparity' })
)
$ErrorActionPreference = "Stop"
$here = Split-Path $PSScriptRoot -Parent
$ctl  = Join-Path $here "ctl.ps1"

$pass = 0; $fail = 0
function Check([string]$n,[bool]$ok,[string]$d="") {
    if ($ok) { Write-Host ("  PASS  {0,-40} {1}" -f $n,$d) -ForegroundColor Green; $script:pass++ }
    else     { Write-Host ("  FAIL  {0,-40} {1}" -f $n,$d) -ForegroundColor Red;   $script:fail++ }
}

# The clause set. Verdict is what BOTH surfaces must say.
#   PASS / FAIL      -- the check ran and was true / false
#   UNEVAL           -- the rail could not perform the check at all
$cases = @(
    @{ e = 'peek:FFFA62 >= 0d0';  v = 'PASS';   why = 'every byte is >= 0 -- operator >=, which CTRL did not have' }
    @{ e = 'peek:FFFA62 <= 0d255';v = 'PASS';   why = 'every byte is <= 255 -- operator <=' }
    @{ e = 'peek:FFFA62 < 0d0';   v = 'FAIL';   why = 'no byte is < 0 -- a FAIL that is not an UNEVAL' }
    @{ e = 'peek:FFFA62 != 0d999';v = 'PASS';   why = 'operator !=' }
    @{ e = 'peek:FFFA62 == 22';   v = 'UNEVAL'; why = 'ambiguous base: 22 or $22? refuse, do not guess' }
    @{ e = 'peek:00C029 == $EE';  v = 'UNEVAL'; why = 'handler-backed page -- CTRL used to PASS this on floating bus' }
    @{ e = 'no_such_field == 0d1';v = 'UNEVAL'; why = 'unknown field must not compare as a sentinel' }
    @{ e = 'qd_carry = 0d0';      v = 'UNEVAL'; why = 'single-= typo: a dropped clause is not a passed clause' }
    @{ e = 'qd_carry == 0d0';     v = 'UNEVAL'; why = 'QDStartUp never fired in either run -- no result to compare' }
    @{ e = 'gsos_err == 0d0';     v = 'PASS';   why = 'no GS/OS error on either surface; a non-peek field CTRL could not even parse' }
)

Write-Host ""
Write-Host ("=== ASSERT PARITY -- platform {0} " -f $Platform).PadRight(64,'=') -ForegroundColor Cyan

# ---- surface A: the env rail, in a short headless spike ------------------
. (Join-Path $here "runtime.ps1"); $null = Initialize-A2RailRuntime
. (Join-Path $here "toolpin.ps1"); $exe = Get-GSSquaredExe
if (-not (Test-Path $exe)) { Write-Host "assertparity: GSSquared not found: $exe" -ForegroundColor Red; exit 1 }

function Spike-Verdict([string]$expr) {
    # A2GSPU_CTRL must be OFF or the process takes the CTRL rail instead of the
    # spike and this measures nothing while looking fine.
    #
    # Every variable this sets is saved first and restored after, including the
    # two pins: FAKETIME and SEED used to be set and never cleared, so every gate
    # run after this one in the same shell inherited them (gates.ps1 caught it).
    $names = 'A2GSPU_CTRL', 'A2GSPU_ASSERT', 'A2GSPU_SPIKE_FRAMES', 'A2GSPU_FAKETIME', 'A2GSPU_SEED', 'A2GSPU_BRAM'
    $saved = @{}
    foreach ($n in $names) { $saved[$n] = [Environment]::GetEnvironmentVariable($n) }
    $env:A2GSPU_CTRL = $null
    $env:A2GSPU_ASSERT = $expr
    $env:A2GSPU_SPIKE_FRAMES = "$Frames"
    $env:A2GSPU_FAKETIME = "1000000000"
    $env:A2GSPU_SEED = "1"
    $env:A2GSPU_BRAM = "default"
    try {
        $out = (& $exe -p $Platform -n 2>&1 | Out-String)
    } finally {
        foreach ($n in $names) {
            # Remove-Item, not SetEnvironmentVariable($n, $null): PowerShell coerces the
            # $null argument to "" and leaves an EMPTY variable behind instead of none.
            if ($null -eq $saved[$n]) { Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue }
            else { Set-Item ("Env:" + $n) $saved[$n] }
        }
    }
    $lines = @($out -split "`r?`n" | Where-Object { $_ -match 'IIGS ASSERT:' })
    $v = 'UNEVAL'
    if ($out -match 'IIGS ASSERT:.*-> PASS')                              { $v = 'PASS' }
    if ($out -match 'NOT CHECKED|unevaluatable|UNEVALUATABLE|ambiguous base|cannot be evaluated') { $v = 'UNEVAL' }
    elseif ($out -match 'IIGS ASSERT:.*-> FAIL')                          { $v = 'FAIL' }
    $act = ''
    if ($out -match 'actual=(\$[0-9A-Fa-f]+ \(-?\d+\))') { $act = $Matches[1] }
    return @{ verdict = $v; actual = $act; text = ($lines -join ' | ') }
}

# ---- surface B: the live CTRL verb ---------------------------------------
if (Test-Path (Join-Path $Dir 'cmd.txt')) { & $ctl -Dir $Dir -Cmd "quit" | Out-Null; Start-Sleep -Milliseconds 1200 }
& (Join-Path $here "session.ps1") -Dir $Dir -Platform $Platform | Out-Null
if ($LASTEXITCODE -eq 3) {
    Write-Host "assertparity: rail process terminated externally -- ABORTING, not a check failure" -ForegroundColor Yellow
    exit 3
}
function Ctrl-Verdict([string]$expr) {
    $r = ((& $ctl -Dir $Dir -Cmd ("assert " + $expr)) -join "`n")
    $v = 'UNEVAL'
    if     ($r -match 'status=FAIL\s+assert-unevaluated') { $v = 'UNEVAL' }
    elseif ($r -match 'status=PASS\s+assert')             { $v = 'PASS' }
    elseif ($r -match 'status=FAIL\s+assert')             { $v = 'FAIL' }
    $act = ''
    if ($r -match 'actual=(\$[0-9A-Fa-f]+ \(-?\d+\))') { $act = $Matches[1] }
    return @{ verdict = $v; actual = $act; text = $r }
}

# ---- the comparison ------------------------------------------------------
foreach ($c in $cases) {
    $s = Spike-Verdict $c.e
    $k = Ctrl-Verdict  $c.e
    Check ("AGREE  " + $c.e) ($s.verdict -eq $k.verdict) `
          ("env=" + $s.verdict + " ctrl=" + $k.verdict + " -- " + $c.why)
    Check ("EXPECT " + $c.e) ($k.verdict -eq $c.v) `
          ("want=" + $c.v + " got=" + $k.verdict + " :: " + $k.text)
}

# THE BASE DIVERGENCE, DIRECTLY. Both surfaces read the same ROM byte -- ROM is
# the one thing a cold session and a 20-frame spike cannot disagree about -- and
# a clause that is false for every byte makes both of them PRINT it. The two
# renderings must be character-identical. `got=A9` against `actual=169` is what
# this refuses to allow back.
$s = Spike-Verdict 'peek:FFFA62 < 0d0'
$k = Ctrl-Verdict  'peek:FFFA62 < 0d0'
Check "both printed the observed byte" (($s.actual -ne '') -and ($k.actual -ne '')) `
      ("env='" + $s.actual + "' ctrl='" + $k.actual + "'")
Check "SAME byte, SAME base, SAME text" ($s.actual -eq $k.actual) `
      ("env='" + $s.actual + "' ctrl='" + $k.actual + "' -- one rail must not print two bases")

# The declared divergence. Asserted, not ignored: the spike computes SHR-window
# statistics and the live rail does not, so these MUST differ. If they ever
# agree, the live rail started answering a question it cannot see.
$s = Spike-Verdict 'nonzero >= 0d0'
$k = Ctrl-Verdict  'nonzero >= 0d0'
Check "nonzero: spike computes it"   ($s.verdict -eq 'PASS')   ("env=" + $s.verdict)
Check "nonzero: live rail refuses"   ($k.verdict -eq 'UNEVAL') ("ctrl=" + $k.verdict + " -- must not invent an SHR statistic")

& $ctl -Dir $Dir -Cmd "quit" | Out-Null

Write-Host ""
if ($fail -eq 0) { Write-Host ("ALL GREEN -- {0} assert-parity checks" -f $pass) -ForegroundColor Green; exit 0 }
Write-Host ("{0} of {1} assert-parity checks FAILED" -f $fail, ($pass+$fail)) -ForegroundColor Red
exit 1
