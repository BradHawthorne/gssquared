<#
  run_until.ps1  --  first-class run-until-condition boot primitive (gdiff G3).

  Formalizes the scratchpad rb.ps1 frame-cap+watchdog runner into a bounded boot
  harness that runs the emulator until a NAMED condition and reports a decisive verdict.
  It is READ-ONLY: it only sets A2GSPU_* env and launches the existing GSSquared.exe;
  it never changes boot behavior or the flag-OFF golden.

  NEVER UNBOUNDED. Every run is capped two ways at once:
    * hard frame cap        (A2GSPU_SPIKE_FRAMES)
    * wall-clock watchdog    (Stop-Process after -TimeoutSec)   <- caught the 371 CPU-sec wedge

  Run-until conditions (the spec grammar):
    BRK            a crash BRK/COP fired            (GSDIAG status=CRASH_BRK / brk>0)
    HANG           a degenerate single-opcode loop  (the emulator's built-in detector)
    COMPLETE       all boot milestones reached      (GSDIAG status=OK)
    MILESTONE:<n>  >= n milestones reached (or a milestone NAME appears)
    DISTINCT:<n>   SHR distinct-colour count >= n
    STALL          a NO-BRK stall: identical (hash,distinct,milestones) across TWO
                   checkpoints -> wedged, not progressing (vs SLOW-PROGRESS which advances)
    TIMEOUT        the wall-clock backstop tripped

  The STALL vs SLOW-PROGRESS classification is the >=600-frame lesson mechanized: when a
  boot is STALLED (milestones incomplete, no BRK/HANG) the harness runs a SECOND, longer
  checkpoint and compares the state fingerprint -- unchanged => genuinely wedged; advanced
  => just slow, give it more frames.

  In-process use (env injection preserved from rb.ps1):
    & run_until.ps1 -Img <boot.po> -Kernel <dir with kernel.milestones.txt> -Until stall -Frames 300 -Env @{A2GSPU_SP_NDEV="32"}
    & run_until.ps1 -Img <boot.po> -Until 'brk,complete,stall' -Milestone init_gim -Distinct 41 -Frames 800

  -Until takes the condition NAMES only (brk, hang, complete, milestone, distinct, stall,
  timeout); the targets for milestone and distinct are the -Milestone and -Distinct
  parameters, not a MILESTONE:<n> suffix.

  Inputs are parameters, with environment fallbacks for a workspace that sets them once:
    -Img      GDIFF_IMG          the boot image (required)
    -Kernel   GDIFF_KERNEL       directory holding kernel.milestones.txt / kernel.sym.dbg
    -Manifest GDIFF_ORACLE_ENV   a script defining Set-OracleEnv (optional; pins the run env)
    -Gss      A2_GSSQUARED       the emulator (default: this repo's build, via toolpin.ps1)
#>
[CmdletBinding()]
param(
  [string]   $Img         = $env:GDIFF_IMG,
  [string]   $Until       = "brk,complete,stall",   # comma-list of target conditions
  [int]      $Frames      = 300,                     # checkpoint-A frame cap
  [int]      $Frames2     = 0,                        # checkpoint-B cap (0 => auto)
  [string]   $Milestone   = "",                       # target: count (int) or a name
  [int]      $Distinct    = -1,                        # target SHR distinct >=
  [int]      $TimeoutSec  = 120,
  [int]      $Ndev        = 32,                        # A2GSPU_SP_NDEV pin (oracle env)
  [string]   $Kernel      = $env:GDIFF_KERNEL,
  [string]   $Gss         = "",
  [string]   $Manifest    = $env:GDIFF_ORACLE_ENV,
  [string]   $Tag         = "run_until",
  [hashtable]$Env         = @{}
)
$ErrorActionPreference = "Continue"
$PSNativeCommandUseErrorActionPreference = $false
. (Join-Path $PSScriptRoot '..\a2rail\toolpin.ps1')
. (Join-Path $PSScriptRoot '..\a2rail\runtime.ps1')
$null = Initialize-A2RailRuntime
if (-not $Gss) { $Gss = Get-GSSquaredExe }
# Fail loud on a missing input: a boot that never started reports a verdict about nothing.
if (-not $Img -or -not (Test-Path $Img)) { Write-Host "run_until: FAIL - boot image '$Img' not found (pass -Img or set GDIFF_IMG)" -ForegroundColor Red; exit 2 }
if (-not (Test-Path $Gss)) { Write-Host "run_until: FAIL - emulator '$Gss' not found (build it, or pass -Gss / set A2_GSSQUARED)" -ForegroundColor Red; exit 2 }
# G5 env-explicit oracle: pin the determinism manifest instead of ad-hoc inline pins.
if ($Manifest) {
  if (-not (Test-Path $Manifest)) { Write-Host "run_until: FAIL - oracle manifest '$Manifest' not found" -ForegroundColor Red; exit 2 }
  . $Manifest
}
$SP = Join-Path ([System.IO.Path]::GetTempPath()) 'gdiff-run-until'
New-Item -ItemType Directory -Force -Path $SP | Out-Null
if ($Frames2 -le 0) { $Frames2 = $Frames + [Math]::Max(150, [int]($Frames / 2)) }

# ---- one bounded boot -> parsed telemetry -------------------------------------
function Invoke-Boot([int]$nframes, [string]$tag) {
  # Clean env slate: every instrumentation variable, by prefix, so no rail -- including
  # ones added after this script was written -- leaks from the caller or a previous run.
  # (This used to be a hand list of 28 names that had missed TRAP, TAP, RECORDS, BRAM,
  # every MEMVU_* and every WIZ5_* rail while its README said "every arming var".)
  Get-ChildItem Env: | Where-Object { $_.Name -match '^(A2GSPU_|MEMVU_|WIZ5_)' } |
    ForEach-Object { Remove-Item ("Env:" + $_.Name) -ErrorAction SilentlyContinue }
  # G5: pin the determinism manifest (SEED/FAKETIME/SP_NDEV/SPIKE_FRAMES) + WARN on drift.
  # Scout mode: leave any deliberate -Env splice (A2GSPU_POKE) alone; -Ndev override warns
  # if it deviates from the oracle pin (=32) -- the dev-loop faithfulness axis.
  if (Get-Command Set-OracleEnv -ErrorAction SilentlyContinue) {
    Set-OracleEnv -Mode Scout -Frames $nframes -Ndev "$Ndev" -Allow @("A2GSPU_POKE") -Quiet | Out-Null
  } else {
    $env:A2GSPU_SPIKE_FRAMES = "$nframes"; $env:A2GSPU_FAKETIME = "1000000000"; $env:A2GSPU_SEED = "1"; $env:A2GSPU_SP_NDEV = "$Ndev"; $env:A2GSPU_BRAM = "default"
  }
  $env:A2GSPU_BRKDUMP       = "1"
  $env:A2GSPU_STOP_ON_FAULT = "1"
  $env:A2GSPU_BRKMEM        = "1"
  $env:A2GSPU_SYM_BASE      = "0"
  if (Test-Path "$Kernel/kernel.sym.dbg")        { $env:A2GSPU_SYMBOLS    = "$Kernel/kernel.sym.dbg" }
  if (Test-Path "$Kernel/kernel.milestones.txt") { $env:A2GSPU_MILESTONES = "$Kernel/kernel.milestones.txt" }
  foreach ($k in $Env.Keys) { Set-Item "Env:$k" $Env[$k] }

  $outf = "$SP/$tag.out"; $errf = "$SP/$tag.err"
  Remove-Item $outf,$errf -ErrorAction SilentlyContinue
  $t0 = Get-Date
  $p = Start-Process -FilePath $Gss -ArgumentList @("-p","5","-d","s7d1=$Img","-n") `
         -WorkingDirectory $SP -NoNewWindow -PassThru `
         -RedirectStandardOutput $outf -RedirectStandardError $errf
  $killed = $false
  Wait-Process -Id $p.Id -Timeout $TimeoutSec -ErrorAction SilentlyContinue
  $p.Refresh()
  if (-not $p.HasExited) {
    $killed = $true
    try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {}
    Start-Sleep -Milliseconds 300
  }
  $secs = [int]((Get-Date) - $t0).TotalSeconds
  $txt  = ((Get-Content $outf -Raw -ErrorAction SilentlyContinue) + "`n" +
           (Get-Content $errf -Raw -ErrorAction SilentlyContinue))

  $r = @{ frames=$nframes; secs=$secs; killed=$killed; tag=$tag
          status='?'; brk=-1; distinct=-1; hash=''; msReached=-1; msTotal=-1
          hangDetected=$false; reachedNames=@() }
  if ($txt -match 'GSDIAG: status=(\w+)')                { $r.status   = $Matches[1] }
  if ($txt -match 'GSDIAG:[^\n]* brk=(\d+)')             { $r.brk      = [int]$Matches[1] }
  if ($txt -match 'SPIKE E1:[^\n]* distinct=(\d+)')      { $r.distinct = [int]$Matches[1] }
  if ($txt -match 'SPIKE E1:[^\n]* hash=([0-9A-Fa-f]+)') { $r.hash     = $Matches[1] }
  if ($txt -match 'IIGS MILESTONES: (\d+)/(\d+) reached'){ $r.msReached=[int]$Matches[1]; $r.msTotal=[int]$Matches[2] }
  if ($txt -match 'IIGS HANG:' -or $txt -match 'hang detected') { $r.hangDetected = $true }
  $r.reachedNames = @([regex]::Matches($txt,'IIGS MILESTONE: reached (\S+)') | ForEach-Object { $_.Groups[1].Value })
  $r.fingerprint = "$($r.hash)|d$($r.distinct)|m$($r.msReached)"
  return $r
}

function Show-Telemetry($r, $label) {
  "  [$label] frames=$($r.frames) secs=$($r.secs) status=$($r.status) brk=$($r.brk) " +
  "distinct=$($r.distinct) milestones=$($r.msReached)/$($r.msTotal) hash=$($r.hash)" +
  $(if ($r.killed) { " WATCHDOG-KILLED" } else { "" })
}

# ---- checkpoint A -------------------------------------------------------------
$wants = ($Until -split '[, ]+' | Where-Object { $_ }) | ForEach-Object { $_.ToUpper() }
"===== run_until [$Tag]  img=$(Split-Path $Img -Leaf)  until={$($wants -join ',')}  cap=$Frames (watchdog ${TimeoutSec}s, ndev=$Ndev) ====="
$A = Invoke-Boot $Frames "$Tag`_A"
Show-Telemetry $A "checkpoint-A"

# milestone / distinct target evaluation
$msTargetMet = $false
if ($Milestone -ne "") {
  if ($Milestone -match '^\d+$') { $msTargetMet = ($A.msReached -ge [int]$Milestone) }
  else { $msTargetMet = ($A.reachedNames -contains $Milestone) }
}
$distinctMet = ($Distinct -ge 0 -and $A.distinct -ge $Distinct)

# ---- decisive verdict (bounded) ----------------------------------------------
$verdict = $null; $detail = ""
if     ($A.killed)                                   { $verdict = "TIMEOUT";  $detail = "wall-clock watchdog tripped at ${TimeoutSec}s (hard cap) -- treat as a wedge; lower -Frames or investigate." }
elseif ($A.brk -gt 0 -or $A.status -eq "CRASH_BRK")  { $verdict = "BRK";      $detail = "a crash BRK/COP fired (brk=$($A.brk)) -- see the [BRK]/BRKHIST dump in $SP/$Tag`_A.err." }
elseif ($A.status -eq "HANG" -or $A.hangDetected)    { $verdict = "HANG";     $detail = "the built-in degenerate single-opcode loop detector fired (a wild/looping spin, no BRK)." }
elseif ($A.status -eq "OK")                          { $verdict = "COMPLETE"; $detail = "all $($A.msTotal) boot milestones reached (status=OK)." }
elseif ($msTargetMet)                                { $verdict = "MILESTONE";$detail = "milestone target '$Milestone' met (reached=$($A.msReached)/$($A.msTotal))." }
elseif ($distinctMet)                                { $verdict = "DISTINCT"; $detail = "SHR distinct target met (distinct=$($A.distinct) >= $Distinct)." }
else {
  # inconclusive (STALLED / GSOS_ERROR): classify NO-BRK STALL vs SLOW-PROGRESS with a
  # SECOND, longer checkpoint -- identical fingerprint across both => genuinely wedged.
  "  ...status=$($A.status): running checkpoint-B (cap=$Frames2) to classify stall vs slow-progress..."
  $B = Invoke-Boot $Frames2 "$Tag`_B"
  Show-Telemetry $B "checkpoint-B"
  if ($B.brk -gt 0 -or $B.status -eq "CRASH_BRK") { $verdict = "BRK";  $detail = "checkpoint-B crashed (brk=$($B.brk))." }
  elseif ($B.status -eq "OK")                     { $verdict = "SLOW-PROGRESS"; $detail = "checkpoint-B reached status=OK -- the boot just needed more frames." }
  elseif ($A.fingerprint -eq $B.fingerprint) {
    $verdict = "NO-BRK STALL"
    $detail  = "state fingerprint IDENTICAL across cap=$Frames and cap=$Frames2 " +
               "(hash/distinct/milestones unchanged) -- genuinely wedged, NOT a crash and NOT slow-progress. " +
               "milestones $($A.msReached)/$($A.msTotal); NOT-REACHED list in $SP/$Tag`_A.err."
  } else {
    $verdict = "SLOW-PROGRESS"
    $detail  = "fingerprint ADVANCED between checkpoints (A=$($A.fingerprint) -> B=$($B.fingerprint)) " +
               "-- still progressing; give it more frames (the >=600-frame lesson)."
  }
}

"----- VERDICT: $verdict -----"
"  $detail"
# exit-code contract for callers: 0 if a wanted positive condition was met, else 3.
$met = $wants -contains ($verdict -replace '[- ]','')  -or `
       ($wants -contains 'STALL' -and $verdict -eq 'NO-BRK STALL')
"(full logs: $SP/$Tag`_A.out/.err" + $(if ($verdict -match 'PROGRESS|STALL') { " + $SP/$Tag`_B.*" } else { "" }) + ")"
if ($met) { exit 0 } else { exit 3 }
