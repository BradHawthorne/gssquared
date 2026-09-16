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

$j=Q 'json cpu';$last=(Get-ChildItem $Dir -Filter 'ack.*'|Sort-Object {[int]($_.Name-replace'ack\.','')}|Select-Object -Last 1);$ack=Get-Content $last.FullName -Raw|ConvertFrom-Json;if(-not$ack.ok-or$ack.schema-ne'a2ctrl-reply-v2'){throw $j}

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
