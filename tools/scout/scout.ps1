<#
  scout.ps1  --  the golden-splice-scout (gdiff G4): the predictive control-forward
  superpower, PACKAGED. Given a wall (a BRK/stall PC) and a DELIBERATE splice (POKE
  golden's bytes over the failing site, OR an auto force-jump to the next milestone),
  it boots FORWARD bounded (the G3 run-until pattern: hard frame cap + wall-clock
  watchdog), then reports HOW FAR it got and the NEXT WALL (PC + a class hint from the
  symbolizer/gdiff). Optionally REPEATs -- forcing past N successive walls to MAP the
  remaining boot path ("spike the path, then pave").

  This was PROVEN predictive during the owned-GS/OS campaign: the SIG-014 splice (POKE
  golden's 6 bytes past a wall) forecast the durable fix's EXACT faithful outcome
  (advanced to frame 336). This tool makes that repeatable.

  READ-ONLY on the oracle: it only sets A2GSPU_* env and launches GSSquared.exe. The
  splice is a DELIBERATE control experiment -- ALWAYS banner'd, NEVER conflated with a
  faithful run. It pins the G5 determinism manifest (Set-OracleEnv -Mode Scout) so the
  forward boot rides the same oracle env as the floor (minus the deliberate splice).

  Splice sources (pick one; if none, the scout AUTO-spikes to the next milestone):
    -Splice "<pc>:<acts>"   a raw A2GSPU_POKE spec (e.g. "FF0C64:ME119A0=D0;ME119A1=19")
    -At <pc> -Force "<acts>"  convenience: builds "<pc>:<acts>"
                              acts: M<hex24>=<hexbyte> (poke golden bytes), PB=/PC= (force-
                              branch), A/X/Y/S/D/P/DBR=, RTS/RTL, SKIP=n  (see A2GSPU_POKE)

  Examples:
    # Auto-spike: force past the current 10/12 stall to the next on-path milestone, name the next wall
    & scout.ps1 -Img .../owned_boot.po -Frames 300

    # The named write-24 case: POKE pristine's record-link bytes over ours' $E06014
    & scout.ps1 -Splice "FF0C64:ME119A0=D0;ME119A1=19;ME119A2=E1" -Label "write-24 record-addr" -Frames 320

    # Map three hops of the remaining path
    & scout.ps1 -Repeat 3
#>
[CmdletBinding()]
param(
  [string] $Img        = "D:/projects/a2vga/m0/_kernel/owned_boot.po",
  [string] $Splice     = "",                 # raw A2GSPU_POKE spec "<pc>:<acts>"
  [string] $At         = "",                 # convenience trigger PC (with -Force)
  [string] $Force      = "",                 # convenience action list (with -At)
  [int]    $Nth        = 1,                   # fire the splice on the Nth PC hit (A2GSPU_POKE_NTH; e.g. write-24)
  [string] $Label      = "",                 # human name of the wall being spiked (banner)
  [int]    $Frames     = 300,                # forward frame cap (baseline + splice both)
  [int]    $Repeat     = 1,                  # spike past N successive walls (map the path)
  [string] $Ndev       = "32",              # A2GSPU_SP_NDEV (oracle pin)
  [int]    $TimeoutSec = 120,
  [switch] $NoBaseline,                       # skip the baseline run (wall already known)
  [string] $Kernel     = "D:/projects/a2vga/m0/_kernel",
  [string] $Gss        = "D:/projects/a2vga/gssquared/build/GSSquared.exe",
  [string] $Manifest   = "D:/projects/a2vga/m0/oracle_env.ps1"
)
$ErrorActionPreference = "Continue"
$PSNativeCommandUseErrorActionPreference = $false
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
$HERE = Split-Path -Parent $MyInvocation.MyCommand.Path
$SYMPY = "$HERE/scout_sym.py"
$SYM   = "$Kernel/kernel.sym.dbg"
$MILE  = "$Kernel/kernel.milestones.txt"
$SP = "C:/Users/X99/AppData/Local/Temp/claude/D--projects-a2vga/58f9a28a-3801-45c3-bff9-ceb148184cc9/scratchpad"
if (-not (Test-Path $SP)) { $SP = [System.IO.Path]::GetTempPath() }
if (Test-Path $Manifest) { . $Manifest }    # G5: Set-OracleEnv / Assert-OracleEnv

# ---- milestones (name -> addr), in boot order ----
$Miles = @()
if (Test-Path $MILE) {
  foreach ($ln in Get-Content $MILE) {
    if ($ln -match '^\s*([0-9A-Fa-f]+)\s+(\S+)') { $Miles += [pscustomobject]@{ Addr=[Convert]::ToInt32($Matches[1],16); Name=$Matches[2] } }
  }
}
function Is-Death($n) { return ($n -match '(?i)death|fatal') }

# ---- one bounded forward boot (the G3 run-until pattern; returns rich telemetry incl. the wall PC) ----
function Invoke-ScoutBoot {
  param([int]$nframes, [string]$tag, [string]$poke)
  # env hygiene: clear every arming/trace var so nothing leaks across runs (rb.ps1 discipline)
  foreach ($v in "A2GSPU_ITRACE_LO","A2GSPU_ITRACE_HI","A2GSPU_ITRACE_OUT","A2GSPU_ITRACE_REARM",
                  "A2GSPU_ITRACE_FROM","A2GSPU_ITRACE_N","A2GSPU_ITRACE_FRAME","A2GSPU_WATCH",
                  "A2GSPU_WATCH_MAX","A2GSPU_WATCH_OUT","A2GSPU_WATCH_READ","A2GSPU_WATCH_CHANGE",
                  "A2GSPU_PCTRAP","A2GSPU_SNAP_LO","A2GSPU_SNAP_HI","A2GSPU_SNAP_PCS","A2GSPU_SNAP_OUT",
                  "A2GSPU_RETGUARD","A2GSPU_CALLTRACE","A2GSPU_CALLTRACE_N","A2GSPU_GOLDEN",
                  "A2GSPU_POKE","A2GSPU_POKE_NTH","A2GSPU_SAVE_AT","A2GSPU_RESTORE","A2GSPU_STACKWATCH","A2GSPU_TEXT40",
                  "A2GSPU_MODETRACE","A2GSPU_INTLOG") {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
  }
  # G5: pin the determinism manifest (Scout mode = allow the deliberate splice).
  if (Get-Command Set-OracleEnv -ErrorAction SilentlyContinue) {
    Set-OracleEnv -Mode Scout -Frames $nframes -Ndev $Ndev -Allow @("A2GSPU_POKE") -Quiet | Out-Null
  } else {
    $env:A2GSPU_SEED = "1"; $env:A2GSPU_FAKETIME = "1000000000"; $env:A2GSPU_SP_NDEV = "$Ndev"; $env:A2GSPU_SPIKE_FRAMES = "$nframes"
  }
  $env:A2GSPU_BRKDUMP = "1"; $env:A2GSPU_STOP_ON_FAULT = "1"; $env:A2GSPU_BRKMEM = "1"; $env:A2GSPU_SYM_BASE = "0"
  if (Test-Path $SYM)  { $env:A2GSPU_SYMBOLS    = $SYM }
  if (Test-Path $MILE) { $env:A2GSPU_MILESTONES = $MILE }
  if ($poke) { $env:A2GSPU_POKE = $poke; if ($Nth -gt 1) { $env:A2GSPU_POKE_NTH = "$Nth" } }

  $outf = "$SP/$tag.out"; $errf = "$SP/$tag.err"
  Remove-Item $outf,$errf -ErrorAction SilentlyContinue
  $p = Start-Process -FilePath $Gss -ArgumentList @("-p","5","-d","s7d1=$Img","-n") `
         -WorkingDirectory (Split-Path $Gss) -NoNewWindow -PassThru `
         -RedirectStandardOutput $outf -RedirectStandardError $errf
  $killed = $false
  Wait-Process -Id $p.Id -Timeout $TimeoutSec -ErrorAction SilentlyContinue
  $p.Refresh()
  if (-not $p.HasExited) { $killed = $true; try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {}; Start-Sleep -Milliseconds 300 }
  $txt = ((Get-Content $outf -Raw -ErrorAction SilentlyContinue) + "`n" + (Get-Content $errf -Raw -ErrorAction SilentlyContinue))

  $r = @{ frames=$nframes; killed=$killed; tag=$tag; poke=$poke
          status='?'; brk=-1; distinct=-1; hash=''; msReached=-1; msTotal=-1
          hang=$false; wildPc=''; wallPc=''; wallWhy=''; wallSym=''
          reachedNames=@(); notReached=@() }
  if ($txt -match 'GSDIAG: status=(\w+)')                 { $r.status   = $Matches[1] }
  if ($txt -match 'GSDIAG:[^\n]* brk=(\d+)')              { $r.brk      = [int]$Matches[1] }
  if ($txt -match 'SPIKE E1:[^\n]* distinct=(\d+)')       { $r.distinct = [int]$Matches[1] }
  if ($txt -match 'SPIKE E1:[^\n]* hash=([0-9A-Fa-f]+)')  { $r.hash     = $Matches[1] }
  if ($txt -match 'IIGS MILESTONES: (\d+)/(\d+) reached') { $r.msReached=[int]$Matches[1]; $r.msTotal=[int]$Matches[2] }
  if ($txt -match 'IIGS HANG:')                           { $r.hang = $true }
  if ($txt -match 'IIGS WILDPC:.* at ([0-9A-Fa-f]{2})/([0-9A-Fa-f]{4})') { $r.wildPc = "$($Matches[1])/$($Matches[2])" }
  $r.reachedNames = @([regex]::Matches($txt,'IIGS MILESTONE: reached (\S+) @([0-9A-Fa-f]{2})/([0-9A-Fa-f]{4}) \(frame (\d+)\)') |
                       ForEach-Object { [pscustomobject]@{ Name=$_.Groups[1].Value; Frame=[int]$_.Groups[4].Value } })
  $r.notReached = @([regex]::Matches($txt,'\*\*\* NOT REACHED: (\S+) \(\$?([0-9A-Fa-f]+)\)') |
                     ForEach-Object { $_.Groups[1].Value })
  # wall PC: BRK > HANG > SPIKE-END(stall/complete)
  # the dump is "PC=bb/aaaa[ sym] A=..."; the optional symbol is any token that is NOT the A= register dump.
  if     ($txt -match 'IIGS CPU \[BRK\]: PC=([0-9A-Fa-f]{2})/([0-9A-Fa-f]{4})(?:\s+(?!A=)(\S+))?')      { $r.wallWhy='BRK';      $r.wallPc="$($Matches[1])/$($Matches[2])"; $r.wallSym=$Matches[3] }
  elseif ($txt -match 'IIGS CPU \[HANG\]: PC=([0-9A-Fa-f]{2})/([0-9A-Fa-f]{4})(?:\s+(?!A=)(\S+))?')     { $r.wallWhy='HANG';     $r.wallPc="$($Matches[1])/$($Matches[2])"; $r.wallSym=$Matches[3] }
  elseif ($txt -match 'IIGS CPU \[SPIKE-END\]: PC=([0-9A-Fa-f]{2})/([0-9A-Fa-f]{4})(?:\s+(?!A=)(\S+))?'){ $r.wallWhy='SPIKE-END'; $r.wallPc="$($Matches[1])/$($Matches[2])"; $r.wallSym=$Matches[3] }
  return $r
}

# ---- name the wall via scout_sym.py (JSON telemetry on stdin) ----
function Name-Wall($r) {
  $tel = @{ status=$r.status; brk=$r.brk; hang=$r.hang; msReached=$r.msReached; msTotal=$r.msTotal
            wallPc=$r.wallPc; wallSym=$r.wallSym; wildPc=$r.wildPc
            reachedNames=@($r.reachedNames | ForEach-Object { $_.Name }) } | ConvertTo-Json -Compress
  $out = $tel | & python $SYMPY classify --sym $SYM --milestones $MILE 2>&1
  return ($out | Out-String)
}

function Fmt-Line($r) {
  $ms = "$($r.msReached)/$($r.msTotal)"
  $wall = if ($r.wallPc) { "$($r.wallWhy) @$($r.wallPc)$(if($r.wallSym){' '+$r.wallSym})" } else { $r.status }
  return "milestones $ms  distinct $($r.distinct)  hash $($r.hash)`n    wall: $wall$(if($r.killed){'  [WATCHDOG-KILLED]'})"
}

# ============================================================================
"======== GOLDEN-SPLICE-SCOUT (G4) ========"
"  img=$(Split-Path $Img -Leaf)  frames=$Frames  ndev=$Ndev  repeat=$Repeat"
"  *** DELIBERATE SPLICE (POKE) -- a control forecast, NOT a faithful boot ***"
if (-not (Test-Path $Gss)) { Write-Error "GSSquared.exe not found: $Gss"; exit 2 }

# ---- baseline ----
$base = $null
if (-not $NoBaseline) {
  "`n--- baseline (no splice) ---"
  $base = Invoke-ScoutBoot -nframes $Frames -tag "scout_base" -poke ""
  Fmt-Line $base
  if ($base.notReached.Count) { "    on-path not-reached: $((@($base.notReached | Where-Object { -not (Is-Death $_) }))[0])" }
}

# ---- resolve the splice for hop 1 ----
function Resolve-Splice($fromRun, $hopNum) {
  # An explicit -Splice / -At applies on hop 1 only; subsequent hops AUTO-spike to map the
  # path (so "-Splice <golden bytes> -Repeat N" = apply my fix, then map the rest).
  if ($hopNum -eq 1 -and $Splice) { return $Splice, ($Label ? $Label : "explicit POKE") }
  if ($hopNum -eq 1 -and $At -and $Force) { return "$($At):$Force", ($Label ? $Label : "explicit POKE @$At") }
  # AUTO-spike: force from the last reached milestone to the first unreached one (LEDGER order --
  # kernel.milestones.txt order, which is not always strict execution order; each hop is printed).
  if (-not $fromRun) { return $null, $null }
  $reached = @($fromRun.reachedNames | ForEach-Object { $_.Name })
  $lastReached = $null; $nextUn = $null
  foreach ($m in $Miles) {
    if ($reached -contains $m.Name) { $lastReached = $m }
    elseif (-not $nextUn -and -not (Is-Death $m.Name)) { $nextUn = $m }
  }
  if (-not $lastReached -or -not $nextUn) { return $null, $null }
  $trig = "{0:X6}" -f $lastReached.Addr
  $bank = "{0:X2}" -f (($nextUn.Addr -shr 16) -band 0xFF)
  $lo   = "{0:X4}" -f ($nextUn.Addr -band 0xFFFF)
  return "$($trig):PB=$bank;PC=$lo", "auto-spike $($lastReached.Name)->$($nextUn.Name)"
}

# ---- hop loop (splice + name next wall; up to -Repeat hops mapping the path) ----
$prev = $base
$anyAdvance = $false
for ($hop = 1; $hop -le $Repeat; $hop++) {
  $spec, $desc = Resolve-Splice $prev $hop
  if (-not $spec) { "`n(no splice resolvable for hop $hop -- boot complete or no next milestone); stopping."; break }
  "`n--- hop $hop splice: $spec   [$desc] ---"
  $run = Invoke-ScoutBoot -nframes $Frames -tag "scout_hop$hop" -poke $spec
  # echo the emulator's own DELIBERATE-splice banner + applied actions (proof it fired)
  $errtxt = Get-Content "$SP/scout_hop$hop.err" -Raw -ErrorAction SilentlyContinue
  @($errtxt -split "`n" | Select-String 'IIGS POKE:') | Select-Object -First 6 | ForEach-Object { "    " + $_.ToString().Trim() }
  Fmt-Line $run

  # advance? (vs the run before this hop)
  $ref = if ($prev) { $prev } else { $run }
  $newMs = @($run.reachedNames | ForEach-Object { $_.Name } | Where-Object { $_ -notin @($ref.reachedNames | ForEach-Object { $_.Name }) })
  $dMs = $run.msReached - $ref.msReached
  $dDist = $run.distinct - $ref.distinct
  $advanced = ($dMs -gt 0) -or ($newMs.Count -gt 0) -or ($dDist -gt 0)
  if ($advanced) { $anyAdvance = $true }
  $adv = if ($advanced) {
    $bits = @()
    if ($dMs -gt 0 -or $newMs.Count) { $bits += "+$([Math]::Max($dMs,$newMs.Count)) milestone$(if([Math]::Max($dMs,$newMs.Count) -ne 1){'s'})$(if($newMs.Count){' ('+($newMs -join ',')+')'})" }
    if ($dDist -gt 0) { $bits += "+$dDist distinct" }
    # frame the new milestone landed on
    $nm = @($run.reachedNames | Where-Object { $_.Name -in $newMs }) | Select-Object -First 1
    if ($nm) { $bits += "reached $($nm.Name) @frame $($nm.Frame)" }
    "  ADVANCED: " + ($bits -join ", ")
  } else { "  NO ADVANCE (fingerprint did not move forward vs the prior run) -- splice necessary-but-not-sufficient OR wrong bytes." }
  $adv

  "  NEXT WALL:"
  (Name-Wall $run) -split "`n" | Where-Object { $_.Trim() } | ForEach-Object { "    $_" }
  $prev = $run
  if (-not $advanced -and $hop -ge 1 -and -not $Splice -and -not $At) { "  (auto-spike did not advance -- stopping the path map.)"; break }
}

"`n======== SCOUT DONE  (advanced=$anyAdvance) ========"
if ($anyAdvance) { exit 0 } else { exit 3 }
