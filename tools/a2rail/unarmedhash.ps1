<#
    unarmedhash.ps1 -- the dual-mode half a gate can check, and observe-don't-disturb, proven.

    WHY THIS EXISTS

    Commit messages quoted "unarmed IIgs 300-frame hash D29F204D690D417F unchanged" as the
    dual-mode check for a week. No gate encoded it: the number was produced by hand, from a
    run whose working directory held a gs2-bram.bin that the emulator rewrites at every exit.
    Re-run later from a fresh directory the same boot hashed 5F78F7C15131F43F, and from the
    old directory 89FCD3DA7D947DFF. The check was never reproducible, so it could never have
    failed for the reason it was quoted -- and the three commits that landed the IIgs display
    pipeline did not quote it at all.

    So this gate does two things, both mechanically:

    1. UNARMED. A small set of boots, every input pinned (seed, faketime, ramdisk, and BRAM --
       the pin that was missing), must reproduce the SHR/HGR hash and cycle count recorded in
       unarmed.baseline.json for the ROM they ran on. A change that moves stock emulation
       moves one of these numbers, and the gate says which.

    2. ARMED. The same boots with observation rails armed (call trace and stream, MEMVU
       accounting and blame, coverage, liveness, a trap, a watch, a tap session, a dump, an
       instruction trace, a region logger) must reproduce THEIR OWN unarmed hash, cycle count
       and run witness exactly. An observer that changes the machine it watches fails here.
       Before a2gspu_observe.hpp, a TAP session did: 754 cycles short on a 300-frame IIgs
       boot, with the SHR hash unchanged. And every armed rail's sink must hold records: an
       armed run that matched only because a rail never armed would otherwise pass.

    3. NAMES. The armed boots' trap records must carry names: RESET on both ROMs, and on
       the GS/OS boot a System Loader call, the System Dispatch Table and the Dispatcher.
       A symbol table that stopped loading leaves every sym null and nothing else notices.
       And the GS/OS boot's load map must hold the three drivers and the FST the Loader
       placed, each with a block that contains its start address.

    What it does NOT check: the windowed, no-environment emulator. Every run here is a spike,
    and a spike is itself an environment variable. The interactive dual-mode check is still
    done by hand (AGENTS.md rule 5), and this header says so rather than implying otherwise.

    INPUTS
      -GsImage   a GS/OS boot volume (the baseline is recorded against gsos601.po);
                 default $env:A2RAIL_GSOS_IMAGE, then <repo>\corpus\gsos601.po.
                 Missing: FAIL, unless -SkipCorpus, which runs the ROM-only rows and says
                 PARTIAL -- a skipped row is reported, never silently green.
      -Bless     record the current numbers as the baseline (after a deliberate change to
                 stock emulation; say why in the commit).

    EXIT  0 all rows pass | 1 any row fails | 2 an input is missing
#>
[CmdletBinding()]
param(
    [string]$GsImage = $env:A2RAIL_GSOS_IMAGE,
    [switch]$SkipCorpus,
    [switch]$Bless,
    [string]$Dir = (Join-Path $env:TEMP ('a2rail-unarmed-' + [guid]::NewGuid().ToString('N').Substring(0, 8))),
    [int]$TimeoutSec = 180
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
. (Join-Path $here 'toolpin.ps1')
. (Join-Path $here 'runtime.ps1')
$null = Initialize-A2RailRuntime
$root = Get-GssquaredRoot
$exe  = Get-GSSquaredExe
$baselineFile = Join-Path $here 'unarmed.baseline.json'
if (-not (Test-Path $exe)) { Write-Host "unarmedhash: FAIL - emulator not found at $exe" -ForegroundColor Red; exit 2 }
if (-not $GsImage) { $GsImage = Join-Path $root 'corpus\gsos601.po' }
$haveCorpus = Test-Path $GsImage
if (-not $haveCorpus -and -not $SkipCorpus) {
    Write-Host "unarmedhash: FAIL - GS/OS boot image not found at '$GsImage'" -ForegroundColor Red
    Write-Host '  (put gsos601.po in <repo>\corpus\, set A2RAIL_GSOS_IMAGE, or pass -SkipCorpus to run the ROM-only rows)'
    exit 2
}
New-Item -ItemType Directory -Force -Path $Dir | Out-Null

$pins = @{ A2GSPU_SEED = '1'; A2GSPU_FAKETIME = '1780000000'; A2GSPU_RAMDISK = '1'; A2GSPU_BRAM = 'default' }

# The observation rails: every one of them promises not to change the machine.
function Get-ArmedEnv([string]$runDir, [int]$platform) {
    $e = @{
        A2GSPU_CALLTRACE     = '1'
        A2GSPU_CALLTRACE_N   = '4000'
        A2GSPU_CALLSTREAM    = (Join-Path $runDir 'callstream.ndjson')
        MEMVU_STOREVIS       = '1'
        MEMVU_LOADVIS        = '1'
        MEMVU_OPMIX          = '1'
        MEMVU_WORKSET        = '1'
        MEMVU_BLAME          = '8'
        A2GSPU_COVERAGE      = '000000-00FFFF'
        A2GSPU_COVERAGE_OUT  = (Join-Path $runDir 'cov.bin')
        A2GSPU_LIVENESS      = '30'
        A2GSPU_LIVENESS_OUT  = (Join-Path $runDir 'liveness.ndjson')
        A2GSPU_TRAP          = 'name=reset at=FA62 every do=regs,ring=8,zp=00-1f'
        A2GSPU_TRAP_OUT      = (Join-Path $runDir 'trap.ndjson')
        A2GSPU_WATCH         = '00:0000-01FF'
        A2GSPU_WATCH_MAX     = '0'
        A2GSPU_WATCH_OUT     = (Join-Path $runDir 'watch.ndjson')
        A2GSPU_TAP           = (Join-Path $runDir 'tap.ndjson')
        A2GSPU_TAP_PCS       = 'FA62,FB2F,FDED'
        A2GSPU_TAP_SESSION   = (Join-Path $runDir 'sess')
        A2GSPU_TAP_DUMP_EVERY = '1'
        A2GSPU_TAP_ZP_EVERY  = '1'
        A2GSPU_DUMP          = '00/0000-00FF,00/C000-C0FF,00/C600-C6FF'
        # the per-instruction trace and the region logger read guest memory on
        # every hit; both must be observers, not bus traffic
        A2GSPU_ITRACE_FRAME  = '10'
        A2GSPU_ITRACE_N      = '20000'
        A2GSPU_ITRACE_OUT    = (Join-Path $runDir 'itrace.txt')
        A2GSPU_SNAP_LO       = '000000'
        A2GSPU_SNAP_HI       = '0000FF'
        A2GSPU_SNAP_PCS      = 'FA62,FB2F,FDED,C300'
        A2GSPU_SNAP_OUT      = (Join-Path $runDir 'snap.ndjson')
    }
    if ($platform -eq 5) {
        $e.A2GSPU_TBTRACE = '1'; $e.A2GSPU_DUMP += ',E1/0000-003F,FF/4100-41FF'
        $e.A2GSPU_LOADMAP = (Join-Path $runDir 'loadmap.ndjson')
        # One landing in each GS/OS kernel region, for the names check below.
        $e.A2GSPU_TRAP += ';name=ldr at=01:A600-FBFF first=1 cap=1 do=regs' +
                          ';name=sdt at=01:FC00-FCFF first=1 cap=1 do=regs' +
                          ';name=disp at=E0:E000-FF38 first=1 cap=1 do=regs'
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $runDir 'sess') | Out-Null
    return $e
}

# Names by default (SHAPE M7): the trap records' sym fields a row must contain. A
# ROM symbol table, the toolbox names or the GS/OS segment map that stopped
# loading would leave every one of these null and nothing else would notice.
$expectNames = @{
    'iie-rom'      = @('"sym":"RESET"')
    'iigs-rom'     = @('"sym":"RESET"')
    'iigs-gsos601' = @('"sym":"RESET"', '"sym":"SystemLoader.', '"sym":"GSOS.SystemDispatchTable+', '"sym":"GSOS.Dispatcher+')
}
# The load map (A2GSPU_LOADMAP) on the GS/OS boot: the drivers and the FST the
# Loader placed by frame 300, each with a block that holds its start address.
$expectLoads = @{
    'iigs-gsos601' = @('Load.AppleDisk3.5', 'Load.AppleDisk5.25', 'Load.Console.Driver', 'Load.Char.FST')
}

# The files the armed rails write; each must be non-empty after an armed run.
# The call stream records toolbox and GS/OS calls, which only a IIgs makes.
function Get-ArmedSinks([int]$platform) {
    $s = @('cov.bin', 'liveness.ndjson', 'trap.ndjson', 'watch.ndjson', 'tap.ndjson', 'itrace.txt', 'snap.ndjson')
    if ($platform -eq 5) { $s += 'callstream.ndjson' }
    return $s
}

function Invoke-Spike([string]$name, [int]$platform, [int]$frames, [string]$disk, [hashtable]$extra) {
    $runDir = Join-Path $Dir $name
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    $saved = @{}
    $envs = @{} + $pins
    $envs.A2GSPU_SPIKE_FRAMES = "$frames"
    if ($extra) { foreach ($k in $extra.Keys) { $envs[$k] = $extra[$k] } }
    # A clean slate: no instrumentation variable from the caller rides along.
    Get-ChildItem Env: | Where-Object { $_.Name -match '^(A2GSPU_|MEMVU_|WIZ5_)' } | ForEach-Object {
        $saved[$_.Name] = $_.Value; Remove-Item ("Env:" + $_.Name)
    }
    foreach ($k in $envs.Keys) { Set-Item ("Env:" + $k) $envs[$k] }
    $argv = @('-p', "$platform")
    if ($disk) { $argv += @('-d', "s7d1=$disk") }
    $argv += '-n'
    $out = Join-Path $runDir 'stdout.log'; $err = Join-Path $runDir 'stderr.log'
    try {
        $p = Start-Process -FilePath $exe -ArgumentList $argv -WorkingDirectory $runDir -NoNewWindow -PassThru `
                           -RedirectStandardOutput $out -RedirectStandardError $err
        if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); $rc = 'timeout' } else { $rc = $p.ExitCode }
    } finally {
        foreach ($k in $envs.Keys) { Remove-Item ("Env:" + $k) -ErrorAction SilentlyContinue }
        foreach ($k in $saved.Keys) { Set-Item ("Env:" + $k) $saved[$k] }
    }
    $text = if (Test-Path $out) { [IO.File]::ReadAllText($out) } else { '' }
    $diag = [regex]::Match($text, 'GSDIAG: status=(\S+).*? hash=([0-9A-F]{16}).*? cycles=(\d+)')
    $rom  = [regex]::Match($text, 'A2GSPU PINS: .*? rom=\d+/([0-9A-F]{16})')
    # M9: run= folds instructions and stores into the witness. An observer that
    # left the screen and the clock alone but changed the work would pass the
    # hash and cycles checks and fail this one.
    $runh = [regex]::Match($text, 'GSDIAG: .*? run=([0-9A-F]{16})')
    return [pscustomobject]@{
        name = $name; rc = $rc
        status = if ($diag.Success) { $diag.Groups[1].Value } else { '' }
        hash   = if ($diag.Success) { $diag.Groups[2].Value } else { '' }
        cycles = if ($diag.Success) { [int64]$diag.Groups[3].Value } else { -1 }
        run    = if ($runh.Success) { $runh.Groups[1].Value } else { '' }
        rom    = if ($rom.Success) { $rom.Groups[1].Value } else { '' }
        dir    = $runDir
    }
}

$rows = @(
    @{ name = 'iigs-rom';  platform = 5; frames = 300; disk = '' },
    @{ name = 'iie-rom';   platform = 3; frames = 120; disk = '' }
)
if ($haveCorpus) { $rows += @{ name = 'iigs-gsos601'; platform = 5; frames = 300; disk = $GsImage } }

$baseline = @{}
if (Test-Path $baselineFile) {
    foreach ($b in (Get-Content $baselineFile -Raw | ConvertFrom-Json).rows) { $baseline[$b.name] = $b }
}

$pass = 0; $fail = 0; $results = @()
function Check([string]$n, [bool]$ok, [string]$d) {
    if ($ok) { Write-Host ("  PASS  {0,-44} {1}" -f $n, $d) -ForegroundColor Green; $script:pass++ }
    else     { Write-Host ("  FAIL  {0,-44} {1}" -f $n, $d) -ForegroundColor Red;   $script:fail++ }
}

Write-Host ''
Write-Host '=== UNARMED HASH / OBSERVE-DON''T-DISTURB ====================' -ForegroundColor Cyan
Write-Host ("exe: {0}" -f $exe)
foreach ($r in $rows) {
    $u = Invoke-Spike ($r.name + '-unarmed') $r.platform $r.frames $r.disk @{}
    $a = Invoke-Spike ($r.name + '-armed') $r.platform $r.frames $r.disk (Get-ArmedEnv (Join-Path $Dir ($r.name + '-armed')) $r.platform)
    $results += [pscustomobject]@{ name = $r.name; platform = $r.platform; frames = $r.frames
                                   disk = if ($r.disk) { Split-Path $r.disk -Leaf } else { '' }
                                   rom = $u.rom; hash = $u.hash; cycles = $u.cycles; run = $u.run }
    Check ("{0}: unarmed run completed" -f $r.name) ($u.rc -eq 0 -and $u.hash) ("rc={0} status={1}" -f $u.rc, $u.status)
    Check ("{0}: armed run completed" -f $r.name) ($a.rc -eq 0 -and $a.hash) ("rc={0} status={1}" -f $a.rc, $a.status)
    Check ("{0}: armed == unarmed (hash)" -f $r.name) ($u.hash -and $a.hash -eq $u.hash) ("unarmed={0} armed={1}" -f $u.hash, $a.hash)
    Check ("{0}: armed == unarmed (cycles)" -f $r.name) ($u.cycles -ge 0 -and $a.cycles -eq $u.cycles) ("unarmed={0} armed={1}" -f $u.cycles, $a.cycles)
    Check ("{0}: armed == unarmed (run)" -f $r.name) ($u.run -and $a.run -eq $u.run) ("unarmed={0} armed={1}" -f $u.run, $a.run)
    # An armed run that equals the unarmed one proves nothing if a rail never
    # armed. Every sink must have written something.
    $sinks = Get-ArmedSinks $r.platform
    $quiet = @($sinks | Where-Object {
        $f = Join-Path $a.dir $_
        -not (Test-Path $f) -or (Get-Item $f).Length -eq 0
    })
    Check ("{0}: armed rails all recorded" -f $r.name) ($quiet.Count -eq 0) $(if ($quiet.Count) { "empty or missing: " + ($quiet -join ', ') } else { "$($sinks.Count) sinks" })
    if ($expectNames.ContainsKey($r.name)) {
        $tf = Join-Path $a.dir 'trap.ndjson'
        $tt = if (Test-Path $tf) { [IO.File]::ReadAllText($tf) } else { '' }
        $missing = @($expectNames[$r.name] | Where-Object { -not $tt.Contains($_) })
        Check ("{0}: trap records carry names" -f $r.name) ($missing.Count -eq 0) $(if ($missing.Count) { "no record with " + ($missing -join ', ') } else { ($expectNames[$r.name] | ForEach-Object { $_.Substring(7).TrimEnd('"') }) -join ' ' })
    }
    if ($expectLoads.ContainsKey($r.name)) {
        $lf = Join-Path $a.dir 'loadmap.ndjson'
        $loads = @(if (Test-Path $lf) { Get-Content $lf | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json } })
        $bad = @($expectLoads[$r.name] | Where-Object {
            $n = $_
            -not ($loads | Where-Object { $_.event -eq 'loadmap.load' -and $_.name -eq $n -and $_.start_block -ge 0 })
        })
        Check ("{0}: load map names the drivers" -f $r.name) ($bad.Count -eq 0) $(if ($bad.Count) { "no load record (with a start block) for " + ($bad -join ', ') } else { "{0} loads, {1}" -f $loads.Count, (($expectLoads[$r.name] | ForEach-Object { $_.Substring(5) }) -join ' ') })
    }
    if (-not $Bless) {
        $b = $baseline[$r.name]
        if (-not $b) {
            Check ("{0}: baseline recorded" -f $r.name) $false "no row in unarmed.baseline.json (run with -Bless after checking the numbers)"
        } elseif ($b.rom -ne $u.rom) {
            Check ("{0}: baseline is for this ROM" -f $r.name) $false ("baseline rom={0} this run rom={1}" -f $b.rom, $u.rom)
        } else {
            Check ("{0}: unarmed == baseline (hash)" -f $r.name) ($u.hash -eq $b.hash) ("baseline={0} now={1}" -f $b.hash, $u.hash)
            Check ("{0}: unarmed == baseline (cycles)" -f $r.name) ($u.cycles -eq [int64]$b.cycles) ("baseline={0} now={1}" -f $b.cycles, $u.cycles)
            # A row blessed before run= existed has no run to compare: that is a
            # baseline to re-bless, not a pass.
            Check ("{0}: unarmed == baseline (run)" -f $r.name) ($b.run -and $u.run -eq $b.run) ("baseline={0} now={1}" -f $(if ($b.run) { $b.run } else { '(none: re-bless)' }), $u.run)
        }
    }
}

if ($Bless) {
    if ($fail -gt 0) { Write-Host 'unarmedhash: refusing to bless -- the armed runs disagree with the unarmed ones' -ForegroundColor Red; exit 1 }
    $old = @{}; foreach ($k in $baseline.Keys) { $old[$k] = $baseline[$k] }
    foreach ($x in $results) { $old[$x.name] = $x }
    $doc = [ordered]@{
        schema  = 'a2rail-unarmed-baseline-v1'
        pins    = 'A2GSPU_SEED=1 A2GSPU_FAKETIME=1780000000 A2GSPU_RAMDISK=1 A2GSPU_BRAM=default'
        blessed = (Get-Date).ToString('yyyy-MM-dd')
        rows    = @($old.Values | Sort-Object name)
    }
    ($doc | ConvertTo-Json -Depth 4) | Set-Content -Path $baselineFile -Encoding utf8
    Write-Host ("unarmedhash: BLESSED {0} row(s) into {1}" -f $results.Count, $baselineFile) -ForegroundColor Yellow
    exit 0
}

Write-Host ''
if (-not $haveCorpus) { Write-Host "unarmedhash: PARTIAL - the GS/OS row did not run (-SkipCorpus); only ROM-only boots were checked" -ForegroundColor Yellow }
if ($fail -eq 0) {
    Write-Host ("ALL GREEN -- {0} checks over {1} boot(s), unarmed and armed" -f $pass, $rows.Count) -ForegroundColor Green
    Remove-Item -Recurse -Force $Dir -ErrorAction SilentlyContinue
    exit 0
}
Write-Host ("{0} of {1} checks FAILED -- run directories kept under {2}" -f $fail, ($pass + $fail), $Dir) -ForegroundColor Red
exit 1
