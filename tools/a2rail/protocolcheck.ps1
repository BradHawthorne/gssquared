<#
    protocolcheck.ps1 -- focused gate for discovery, ownership, JSON framing and
    the live golden families.

    WHY THE SHAPE OF IT CHANGED

    This file reported nothing at all for a long time, in three different ways,
    and each one is a distinct way an instrument lies. Naming them here because
    the same three mistakes are easy to make again.

      * IT COULD NOT RUN. Line 4 launched session.ps1 in a CHILD pwsh, and that
        call BLOCKS until the emulator exits. Every command below it therefore
        ran against a session that had already self-quit on its idle timeout,
        and the run died at the first `protocol` with "[--] NO SESSION". Timed:
        the child-pwsh launch took 17s to return with -TimeoutS 15, the same
        launch in-process took 2s. railcheck.ps1 has always called session.ps1
        in-process; so does this now.

      * IT TRANSCRIBED A COUNT. The `systems` assertion read `configs=15`. The
        sixteenth builtin config landed, the emulator started answering 16, and
        the assertion went red -- but nothing runs this file, so nobody saw. A
        number copied out of src/systemconfig.cpp is an unmaintained second copy
        of that file's length, and when it finally disagrees it names no cause.
        Both counts are now COUNTED FROM THE SOURCE, so what is checked is that
        the rail's reply agrees with the code it is reporting on.

      * ITS GOLDEN WAS A TAUTOLOGY. `shr-golden <f> bless` followed immediately
        by `shr-golden <f>` hashes the same live $E12000..$E19FFF window through
        probe_peek twice with nothing in between, so the expected value was
        derived from the subject and MATCH was guaranteed. It would have stayed
        green with the window full of zeroes, with the window destroyed between
        runs and re-blessed, or with the hash reading no memory at all -- it
        confirmed only that hashing twice is deterministic. It is a DIFFERENTIAL
        now: the same remedy railcheck.ps1's `shr renders` check already adopted
        after an absolute pixel threshold passed on a screen the poke never
        touched.

    Exit 0 only if every check passes.
#>
[CmdletBinding()]param([string]$Dir=(Join-Path $env:TEMP ('a2rail-protocol-'+[guid]::NewGuid().ToString('N'))))
$ErrorActionPreference='Stop';$h=$PSScriptRoot;$owner='protocolcheck';$root=Split-Path (Split-Path $h -Parent) -Parent

# IN-PROCESS, not `& pwsh.exe -File`. The child-pwsh form does not return until
# the emulator exits, which made every check below it run against a session that
# was already gone. session.ps1 exits 0 only after the rail has answered a real
# round trip, so its exit code IS the readiness signal: refuse to run the checks
# against a machine that never came up, rather than reporting NO SESSION eight
# times and leaving the reader to work out that none of them ever executed.
$boot = & (Join-Path $h 'session.ps1') -Dir $Dir -Platform 5 -Owner $owner -TimeoutS 300
if ($LASTEXITCODE -ne 0) { throw ("session.ps1 did not bring the rail up (exit=$LASTEXITCODE):`n"+($boot-join"`n")) }

function Q([string]$c){$global:LASTEXITCODE=0;$o=& (Join-Path $h 'ctl.ps1') -Dir $Dir -Owner $owner -Cmd $c; if($LASTEXITCODE){throw "$c failed: $o"};return($o-join"`n")}

# Get-CArrayCount comes from runtime.ps1 (dot-sourced below): count the entries of a
# C array in the source rather than transcribing the total, and refuse -- throw --
# when the scrape stops scraping. This file used to carry its own copy of it,
# identical to the shared one; two copies of a guard drift, and a drifted guard
# goes green.
. (Join-Path $h 'runtime.ps1')

$p=Q 'protocol';if($p-notmatch 'version=2'){throw $p}
# Versioned extensions: `features` must agree with the minor `protocol` reports,
# list itself, and refuse a name it does not know.
if($p-notmatch ' minor=(\d+)'){throw "protocol reply carries no minor=: $p"};$minor=[int]$Matches[1]
# `protocol` must name every extension `features` lists, in the same order.
if($p-notmatch " extensions='([^']*)'"){throw "protocol reply carries no extensions=: $p"};$pext=$Matches[1]
$fl=Q 'features';$fnames=(@(($fl-split'list=')[1].Trim()-split',')|%{($_-split'/')[0]})-join','
if($pext-ne$fnames){throw "protocol extensions='$pext' does not match the features list '$fnames': $p"}
$f=Q 'features';if($f-notmatch "features protocol=2\.$minor count=(\d+) list=(\S+)"){throw "features does not agree with protocol minor=$minor : $f"}
if(@($Matches[2]-split',')-notcontains 'features/1'){throw "features does not list itself: $f"}
$u=Q 'features no-such-extension';if($u-notmatch 'status=FAIL features-unknown'){throw "unknown extension was not refused: $u"}
# setreg-banks (2.2): DBR and PBR are settable, read back through `json cpu`,
# and an out-of-range bank is refused. The machine's own DBR is put back.
if(@($f-split'list=')[1]-split','-contains 'setreg-banks/1'){
  $c0=Q 'json cpu';if($c0-notmatch '"dbr"\s*:\s*(\d+)'){throw "json cpu carries no dbr: $c0"};$dbr0=[int]$Matches[1]
  $s=Q 'setreg dbr 7E';if($s-notmatch 'status=OK setreg dbr=\$7E'){throw "setreg dbr not accepted: $s"}
  $c1=Q 'json cpu';if($c1-notmatch '"dbr"\s*:\s*126\b'){throw "setreg dbr 7E did not reach DBR: $c1"}
  $r=Q 'setreg dbr 100';if($r-notmatch 'status=FAIL setreg-range'){throw "out-of-range dbr was not refused: $r"}
  $null=Q ('setreg dbr {0:X2}'-f$dbr0)
}
# assert-registers (2.3): a register clause is evaluated against the live CPU
# (true, false), and a mask with no base is refused as unevaluated.
if(@($f-split'list=')[1]-split','-contains 'assert-registers/1'){
  $c=Q 'json cpu';if($c-notmatch '"dbr"\s*:\s*(\d+)'){throw "json cpu carries no dbr: $c"};$d=[int]$Matches[1]
  $a=Q ('assert reg:dbr==${0:X2};reg:e<=1'-f$d);if($a-notmatch 'status=PASS assert pass=2'){throw "true register assert did not pass: $a"}
  $a=Q ('assert reg:dbr!=${0:X2}'-f$d);if($a-notmatch 'status=FAIL assert pass=0 fail=1'){throw "false register assert did not fail: $a"}
  $a=Q 'assert reg:p&10==0';if($a-notmatch 'status=FAIL assert-unevaluated'){throw "base-less mask was not refused: $a"}
}
# writes (2.4): a marked log is empty, a bad cap is refused, and `writes off`
# stops recording.
if(@($f-split'list=')[1]-split','-contains 'writes/1'){
  $w=Q 'writes mark';if($w-notmatch 'status=OK writes mark cap=65536'){throw "writes mark not accepted: $w"}
  $w=Q 'writes';if($w-notmatch 'status=OK writes on=1 n=0 phantom=0 unique=0 dropped=0 wr=$'){throw "fresh write log is not empty: $w"}
  $w=Q 'writes mark x';if($w-notmatch 'status=FAIL writes-parse'){throw "bad cap was not refused: $w"}
  $w=Q 'writes off';if($w-notmatch 'status=OK writes off'){throw "writes off not accepted: $w"}
}
# buslog (2.5): a mark names format v1 and starts empty; one step logs one timed record
# per CPU cycle (every record fast or slow); a dump starts with the v1 header; a bad cap
# is refused, and `buslog off` stops recording.
if(@($f-split'list=')[1]-split','-contains 'buslog/1'){
  $b=Q 'buslog mark';if($b-notmatch 'status=OK buslog mark v=1 cap=1048576'){throw "buslog mark not accepted: $b"}
  $b=Q 'buslog';if($b-notmatch 'status=OK buslog v=1 on=1 dropped=0 n=0 cycles=0 '){throw "fresh bus log is not empty: $b"}
  $null=Q 'step 1'
  $b=Q 'buslog';if($b-notmatch ' n=(\d+) cycles=(\d+) c14m=(\d+) fast=(\d+) fast_c14m=\d+ slow=(\d+) '){throw "no timed bus log summary after a step: $b"}
  if([int]$Matches[1]-lt 2 -or $Matches[1]-ne $Matches[2] -or ([int]$Matches[4]+[int]$Matches[5])-ne [int]$Matches[1]){throw "a step did not log one timed record per cycle: $b"}
  $bf=Join-Path $Dir 'buslog-dump.txt';$b=Q "buslog dump $bf";if($b-notmatch 'status=OK buslog dump n=\d+ wrote'){throw "buslog dump failed: $b"}
  if((Get-Content -LiteralPath $bf -TotalCount 1)-ne '# buslog v1 fields: seq cycle t14m addr kind value side c14m base wait refresh stretch reasons'){throw "buslog dump has no v1 header: $bf"}
  $b=Q 'buslog mark x';if($b-notmatch 'status=FAIL buslog-parse'){throw "bad cap was not refused: $b"}
  $b=Q 'buslog off';if($b-notmatch 'status=OK buslog off'){throw "buslog off not accepted: $b"}
}

# NUM_SYSTEM_CONFIGS is sizeof(BuiltinSystemConfigs)/sizeof(...) in
# src/systemconfig.cpp, and num_platforms the same over platforms[] in
# src/platforms.cpp. Those two files are what `systems` reports; count them
# there. The commented-out Apple IIc entries do not match either pattern, which
# is the point -- they are not compiled in and the rail does not count them.
$wantConfigs   = Get-CArrayCount (Join-Path $root 'src/systemconfig.cpp') 'BuiltinSystemConfigs\[\]\s*=\s*\{' '^    \{\s*$'  'NUM_SYSTEM_CONFIGS'
$wantPlatforms = Get-CArrayCount (Join-Path $root 'src/platforms.cpp')    'platform_info\s+platforms\[\]\s*=\s*\{'  '^ {8}PLATFORM_' 'num_platforms'
$s=Q 'systems'
if($s-notmatch 'platforms=(\d+) configs=(\d+)'){throw "systems reply does not carry a platforms=/configs= pair: $s"}
$gotPlatforms=[int]$Matches[1];$gotConfigs=[int]$Matches[2]
if($gotPlatforms -ne $wantPlatforms){throw "systems says platforms=$gotPlatforms; src/platforms.cpp holds $wantPlatforms platform entries"}
if($gotConfigs   -ne $wantConfigs)  {throw "systems says configs=$gotConfigs; src/systemconfig.cpp holds $wantConfigs builtin configs"}

$j=Q 'json cpu';$last=(Get-ChildItem $Dir -Filter 'ack.*'|Sort-Object {[int]($_.Name-replace'ack\.','')}|Select-Object -Last 1);$ack=Read-AckText $last.FullName|ConvertFrom-Json;if(-not$ack.ok-or$ack.schema-ne'a2ctrl-reply-v2'){throw $j}

# ---- shr-golden as a DIFFERENTIAL, not a self-comparison -------------------
# bless-then-compare proves only that the hash is deterministic. The question
# worth asking is whether the stored hash is compared against MEMORY: put the
# window into a KNOWN state, bless it, change eight bytes of it, and require
# DIFF -- that is the assertion the old form could not make. Then put the eight
# bytes back and require MATCH again, which proves the compare reads the FILE
# and not a freshly re-derived current value (a re-deriving implementation would
# have answered MATCH at the DIFF step and MATCH here too, indistinguishably).
#
# KNOWN STATE, NOT THE BOOT CONTENTS. The obvious form is `read E1:2000 8` to
# save the originals and `load` them back -- but `read` FAILS outright when the
# whole range is handler-backed, and the IIgs shadowed banks are named in that
# verb's own hazard note as a place probe_readable can say no. Poking a known
# value in and a known value back needs no read, uses only the verb that was
# proved by hand to reach this window, and leaves it in a state the next reader
# can predict. It does not restore the ROM's boot pattern, and nothing after
# this point depends on it.
$zeros='00 00 00 00 00 00 00 00';$ffs='FF FF FF FF FF FF FF FF'
$g=Join-Path $Dir 'shr.golden'
$null=Q "poke E1:2000 $zeros"
$b=Q "shr-golden $g bless";if($b-notmatch 'BLESSED'){throw "shr-golden bless: $b"}
$m=Q "shr-golden $g";if($m-notmatch 'MATCH'){throw "shr-golden should MATCH an unchanged window: $m"}
$null=Q "poke E1:2000 $ffs"
$d=Q "shr-golden $g"
if($d-notmatch 'DIFF'){throw "shr-golden answered '$d' after eight bytes of the hashed window were overwritten -- the golden is not reading memory, or not comparing against the blessed file"}
$null=Q "poke E1:2000 $zeros"
$m2=Q "shr-golden $g";if($m2-notmatch 'MATCH'){throw "shr-golden should MATCH again once the window is restored: $m2"}

$bad=& (Join-Path $h 'ctl.ps1') -Dir $Dir -Owner wrong -Cmd cpu;if($LASTEXITCODE-ne3){throw'owner mismatch was not refused'};$global:LASTEXITCODE=0
$null=Q 'quit';Write-Host "ALL GREEN -- protocol/discovery($wantPlatforms platforms, $wantConfigs configs)/json/ownership/SHR differential" -ForegroundColor Green
