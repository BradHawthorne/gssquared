<#
    railcheck.ps1 -- does the CTRL rail actually do what it says?

    WHY THIS EXISTS

    The rail is the instrument every measurement in this project rests on, and
    over one session it produced five separate silent lies:

      * `poke` wrote the aux bank while acking "@00/xxxx", because it honoured
        RAMWRT as the GUEST had left it
      * `load`/`read`/`poke` addressed the Mega II on a IIgs while the CPU ran
        out of the FPI -- different objects, different memory, both acking OK
      * a long command could be read mid-write and answered "unknown-cmd"
      * `dis 0400` created a file named "0400" and acked OK
      * `probe_peek` returned floating bus for banks $00/$01 on a IIgs

    Every one reported success. None was found by reading the code; each was
    found when something downstream made no sense. An instrument that cannot be
    challenged is not an instrument, so this challenges it.

    THE RULE: a verb is only trusted if its effect can be OBSERVED INDEPENDENTLY
    of the verb itself. Writing then reading with the same broken assumption
    proves nothing -- that is exactly how the IIgs bug survived. Where possible
    the check crosses paths: poke and verify by executing, setreg and verify by
    stepping, load and verify by disassembling.

        .\tools\railcheck.ps1                 platform 3 (Apple IIe Enhanced)
        .\tools\railcheck.ps1 -Platform 5     Apple IIgs
        .\tools\railcheck.ps1 -All            every platform the build supports

    Exit 0 only if every check passes.
#>

[CmdletBinding()]
param(
    [int]$Platform = 3,
    [switch]$All,
    [string]$Dir  = $(if ($env:A2RAIL_DIR) { $env:A2RAIL_DIR } else { Join-Path $env:TEMP 'a2rail' }),
    [string]$Root = "",
    # Empty means "search". Resolved in the body -- see $bootSearch below -- and
    # NOT defaulted to one absolute path on one developer's machine, which is how
    # the widest check in this suite came to be skipped everywhere else.
    [string]$BootDisk = ""
)

$ErrorActionPreference = "Stop"
# $PSScriptRoot is empty inside param() defaults under `pwsh -File`, so anything
# derived from the script's location is resolved here, in the body.
$here = $PSScriptRoot
# $Root is the gssquared checkout, whose corpus/ holds the boot disk. It is NOT
# "two levels above this script": a2engine runs a pulled copy of this file from
# its own tools\ directory (sync-a2rail.ps1), where that derivation lands outside
# any repository and the boot search below would find nothing. Get-GssquaredRoot
# answers the same way session.ps1 finds the emulator: GSSQUARED_ROOT, else this
# script's own checkout when it runs in place.
. (Join-Path $here 'toolpin.ps1')
if (-not $Root) { $Root = Get-GssquaredRoot }

# BOOT DISK RESOLUTION. A candidate LIST, not one absolute path on one
# developer's machine -- that is how the widest check in this suite came to be
# skipped (and scored green) everywhere but here.
#
# ORDER IS BY EVIDENCE, not by convenience. The assertion below is a banner
# match on the text page, so a candidate only counts if it is known to reach
# that banner on BOTH platform 3 and platform 5. DOS3.3_MASTER.dsk is the only
# image that has actually been observed doing so, so it is first. It lives in the
# repo's corpus/ (ignored, not redistributed; corpus/README.md) -- it used to be
# read from a path inside one developer's other checkout, which put that private
# layout in a public repo and made this check a FAIL on every other machine. The
# game-loader disks that followed it as unverified fallbacks are gone: an
# unverified fallback is how a real check turns red for a reason unrelated to
# the rail.
#
# The mount below is s6d1, a 5.25" drive, so .dsk images come before the ProDOS
# .po ones (gsos601.po is a 3.5" image and will not boot from slot 6 at all --
# it is last-resort evidence that SOMETHING was tried, and is expected to fail
# loudly rather than to be skipped silently).
$bootSearch = @(
    $env:A2RAIL_BOOTDISK
    (Join-Path $Root 'corpus\DOS3.3_MASTER.dsk')
    (Join-Path $Root 'corpus\gsos601.po')
)
if (-not $BootDisk) {
    $found = $bootSearch | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if ($found) { $BootDisk = $found }
}

$ctl = Join-Path $PSScriptRoot "ctl.ps1"
$tmp = Join-Path $Dir "railcheck"
if (-not (Test-Path $tmp)) { New-Item -ItemType Directory $tmp -Force | Out-Null }

$PLATFORMS = @{ 0 = "Apple II"; 1 = "Apple II Plus"; 2 = "Apple IIe";
                3 = "Apple IIe Enhanced"; 4 = "IIe Enhanced 65816"; 5 = "Apple IIgs" }

function Say([string]$cmd) {
    return ((& $ctl -Dir $Dir -Cmd $cmd) -join "`n")
}
# THE SESSION IS THIS $Dir, not any GSSquared on the machine. This suite used to
# decide both "is there a stale session to quit" and its own `Check "session"`
# from `Get-Process -Name GSSquared`, which is a machine-wide question being
# asked of a per-directory thing. The consequence was worse than a nuisance: if
# THIS session failed to start while some other agent's emulator happened to be
# running, `session` scored PASS and every later verb then ran against a
# directory nothing was reading -- each one stalling or text-mismatching in a way
# that reads as a rail bug rather than as an absent emulator.
# session.ps1 writes session.json with the pid before it launches, so the pid in
# this $Dir is the only witness that answers the question actually being asked.
function Get-LiveSessionPid([string]$d) {
    $f = Join-Path $d 'session.json'
    if (-not (Test-Path $f)) { return 0 }
    try { $s = Get-Content $f -Raw | ConvertFrom-Json } catch { return 0 }
    if (-not $s.pid) { return 0 }
    if (Get-Process -Id ([int]$s.pid) -ErrorAction SilentlyContinue) { return [int]$s.pid }
    return 0
}
function PeekByte([string]$addr) {
    $o = Join-Path $tmp "p.bin"
    if (Test-Path $o) { Remove-Item $o -Force }
    Say ("read {0} 1 {1}" -f $addr, ($o -replace '\\','/')) | Out-Null
    if (-not (Test-Path $o)) { return -1 }
    $b = [System.IO.File]::ReadAllBytes($o)
    if ($b.Length -lt 1) { return -1 }
    return [int]$b[0]
}

$script:pass = 0
$script:fail = 0
function Check([string]$name, [bool]$ok, [string]$detail = "") {
    if ($ok) { Write-Host ("  PASS  {0,-22} {1}" -f $name, $detail) -ForegroundColor Green; $script:pass++ }
    else     { Write-Host ("  FAIL  {0,-22} {1}" -f $name, $detail) -ForegroundColor Red;   $script:fail++ }
}

function Test-Platform([int]$p) {
    Write-Host ""
    Write-Host ("=== platform {0}: {1} " -f $p, $PLATFORMS[$p]).PadRight(64,'=') -ForegroundColor Cyan

    if ((Get-LiveSessionPid $Dir) -ne 0) { Say "quit" | Out-Null; Start-Sleep -Milliseconds 1200 }
    & (Join-Path $PSScriptRoot "session.ps1") -Dir $Dir -Platform $p | Out-Null
    # Exit 3 is session.ps1's "the rail process was TERMINATED externally". That
    # is somebody else's kill landing on us -- a second harness, a stray sweep, a
    # human closing a window -- and it is not a verdict on any rail verb. Passing
    # it up lets dualsmoke record ABORT instead of filing it under our failures,
    # which is how a reaper's own damage used to get blamed on the emulator.
    if ($LASTEXITCODE -eq 3) {
        Write-Host "railcheck: rail process terminated externally -- ABORTING, this is not a check failure" -ForegroundColor Yellow
        exit 3
    }
    $spid = Get-LiveSessionPid $Dir
    if ($spid -eq 0) {
        Check "session" $false "would not start -- no live pid in $Dir\session.json"; return
    }
    Check "session" $true ("{0}, pid {1}" -f $PLATFORMS[$p], $spid)

    # ---- poke/read round trip, then the SAME address proved by EXECUTION ----
    # A write and a read that share a broken assumption agree with each other.
    # The only witness that cannot be fooled is the CPU: if it executes what was
    # placed, the bytes are genuinely where the machine looks for them.
    Say "poke 00:6000 A9 34 8D 00 70 4C 05 60" | Out-Null
    Check "poke -> read" ((PeekByte "6000") -eq 0xA9) ("`$6000 = {0:X2}" -f (PeekByte "6000"))

    Say "setreg pc 6000" | Out-Null
    Say "step 1" | Out-Null; Say "step 1" | Out-Null
    $exec = PeekByte "7000"
    Check "poke -> EXECUTED" ($exec -eq 0x34) ("`$7000 = {0:X2} after LDA #`$34 / STA `$7000" -f $exec)

    # ---- setreg observed through the CPU, not through setreg -----------------
    Say "setreg a 5A" | Out-Null
    $cpu = Say "cpu"
    Check "setreg -> cpu" ($cpu -match "A=005A|A=5A") "A register reads back"

    # ---- run-until reaches a known address ----------------------------------
    Say "poke 00:6100 EA EA EA 4C 03 61" | Out-Null
    Say "setreg pc 6100" | Out-Null
    $r = Say "run-until 6103 100000"
    Check "run-until" ($r -match "status=OK" -and $r -match "PC=00:6103") "stopped at the requested PC"

    # ---- cycles are an in-command delta and must be plausible ---------------
    Check "run-until cycles" ($r -match "cycles=([1-9]\d*)") "non-zero in-command delta"

    # ---- load: file on disk -> guest memory, verified byte for byte ---------
    $lf = Join-Path $tmp "load.bin"
    [System.IO.File]::WriteAllBytes($lf, [byte[]](0xDE,0xAD,0xBE,0xEF))
    Say ("load 00:6200 {0}" -f ($lf -replace '\\','/')) | Out-Null
    Check "load -> read" ((PeekByte "6200") -eq 0xDE -and (PeekByte "6203") -eq 0xEF) "4 bytes landed"

    # ---- dis MUST refuse a missing filename ---------------------------------
    # It used to accept `dis <addr>`, take the filename from the address text,
    # and create a file called "0400". A verb that invents an output path is
    # worse than one that fails.
    $bad = Say "dis 6200"
    Check "dis refuses bad args" ($bad -match "status=FAIL") "no filename -> FAIL, not a file named after the address"

    $df = Join-Path $tmp "dis.txt"
    $good = Say ("dis 6200 4 {0}" -f ($df -replace '\\','/'))
    Check "dis writes a listing" ($good -match "status=OK" -and (Test-Path $df)) "with addr, count and path"

    # ---- save/restore actually round-trips guest state ----------------------
    $snap = Join-Path $tmp "snap.bin"
    Say "poke 00:6300 11" | Out-Null
    Say ("save {0}" -f ($snap -replace '\\','/')) | Out-Null
    Say "poke 00:6300 99" | Out-Null
    $changed = (PeekByte "6300")
    Say ("restore {0}" -f ($snap -replace '\\','/')) | Out-Null
    $restored = (PeekByte "6300")
    Check "save/restore" ($changed -eq 0x99 -and $restored -eq 0x11) ("11 -> 99 -> {0:X2}" -f $restored)

    # ---- the SCREEN. Only meaningful where there is one of that kind --------
    # Looking at output a human would see is the technique that found every
    # silent bug in this project; on the IIgs it did not exist until `shr` was
    # written. Checked by writing a known pattern into video memory and
    # requiring the renderer to report the right number of non-zero pixels --
    # a dump that always says "OK" while producing a black frame is the exact
    # failure this whole file is about.
    # DIFFERENTIAL, not absolute. The first version of this check required
    # ">= 160 non-zero pixels" and passed with 32,080 -- the boot ROM had
    # already filled the screen, so it would have passed with the poke doing
    # nothing at all. An absolute threshold against a non-empty screen proves
    # only that the screen is non-empty. Clear the row, count, light the row,
    # count again, and require the DIFFERENCE.
    if ($p -eq 5) {
        $sf = Join-Path $tmp "shr.png"
        $zero = (1..80 | ForEach-Object { "00" }) -join " "
        $ones = (1..80 | ForEach-Object { "FF" }) -join " "

        # PIN LINE 0's SCB TO 320 MODE. The SHR mode is per-line (bit 7 of each
        # SCB), and on a fresh boot that area holds uninitialised memory -- so
        # the line this check writes to could be decoded as 640 mode, where the
        # same $FF bytes mean four 2-bit pixels against a split palette instead
        # of two 4-bit ones. Control the state you are not testing.
        Say "poke E1:9D00 00" | Out-Null
        Say "poke E1:2000 $zero" | Out-Null
        $a = Say ("shr {0} 1" -f ($sf -replace '\\','/'))
        $na = if ($a -match "nonzero=(\d+)/") { [int]$Matches[1] } else { -1 }

        Say "poke E1:2000 $ones" | Out-Null
        $b = Say ("shr {0} 1" -f ($sf -replace '\\','/'))
        $nb = if ($b -match "nonzero=(\d+)/") { [int]$Matches[1] } else { -1 }

        Check "shr renders" (($b -match "status=OK") -and (Test-Path $sf) -and (($nb - $na) -eq 160)) `
              ("160 pixels lit changed the count by $($nb - $na)")
    }

    # ---- vram reaches the memory the DISPLAY is generated from -------------
    # Every video verb wanted one flat 128K image with aux at +0x10000, and got
    # it through dynamic_cast<MMU_II*>. MMU_IIgs is not an MMU_II, so on a IIgs
    # vram/png/pngc/dhgr-golden all answered "no-flat-image" and refused -- even
    # though a IIgs displays text, HGR and DHGR perfectly well.
    #
    # The IIgs keeps that image in the Mega II ($E0/$E1), not the FPI, because
    # IIe-style video is generated by the Mega II and SHADOWING is what copies
    # $00/$01 writes into it. So this check is deliberately end to end: poke the
    # HGR page in bank $00, let a frame run, and require the bytes to appear in
    # the vram dump. It passes only if the base pointer is right AND shadowing
    # actually works -- and on a IIe it is a plain regression check of a path
    # that was already good.
    # THE CPU DOES THE WRITING, not `poke`. poke goes through probe_poke, a
    # deliberate side-effect-free debug path -- so on a IIgs it lands in the FPI
    # and never triggers SHADOWING, which is the thing that actually copies
    # bank $00 into the Mega II the display reads. Poking and then reading vram
    # would compare two memories that are not required to agree, and the first
    # version of this check did exactly that and failed on a correct emulator.
    # Executing a real store crosses every layer that matters: poke -> execute
    # -> shadow -> display memory -> vram.
    $vf = Join-Path $tmp "vram.bin"
    if (Test-Path $vf) { Remove-Item $vf -Force }
    Say "poke 00:6000 A9 7E 8D 00 20 8D 01 20 4C 08 60" | Out-Null
    Say "setreg pc 6000" | Out-Null
    Say "run-until 6008 1000" | Out-Null
    $vr = Say ("vram {0} 1" -f ($vf -replace '\\','/'))
    $vok = $false; $vdet = "no dump"
    if (Test-Path $vf) {
        $vb = [System.IO.File]::ReadAllBytes($vf)
        # 8K aux then 8K main, so main $2000 is at offset 8192.
        if ($vb.Length -ge 8196) {
            $vok = ($vb[8192] -eq 0x7E -and $vb[8193] -eq 0x7E)
            $vdet = ("{0} bytes, main `$2000 = {1:X2} {2:X2}" -f $vb.Length, $vb[8192], $vb[8193])
        } else { $vdet = ("short dump: {0} bytes" -f $vb.Length) }
    } elseif ($vr -match "no-flat-image") { $vdet = "no-flat-image (MMU_II cast failed)" }
    Check "vram sees the screen" $vok $vdet

    # ---- DOES THE MACHINE STILL RUN REAL SOFTWARE? -------------------------
    # The widest check here, and the one a rail change is most likely to break
    # without any of the narrow checks noticing: mount a real disk, boot it, and
    # read the text page. It exercises the disk controller, RWTS, the video
    # memory map, the CPU and the MMU together -- none of which the verb tests
    # above touch. Passing every rail check on an emulator that can no longer
    # boot a floppy would be a perfect score on the wrong exam.
    # $BootDisk is "" when the search below found nothing, and `Test-Path ""`
    # throws under ErrorActionPreference=Stop -- which would abort the suite
    # instead of reporting the missing disk. Ask whether we have one first.
    if ($BootDisk -and (Test-Path $BootDisk)) {
        Say ("mount s6d1 {0}" -f ($BootDisk -replace '\\','/')) | Out-Null
        Say "reset" | Out-Null
        Say "run 600" | Out-Null
        $tf = Join-Path $tmp "boot.txt"
        Say ("text {0}" -f ($tf -replace '\\','/')) | Out-Null
        $seen = ""
        if (Test-Path $tf) {
            $by = [System.IO.File]::ReadAllBytes($tf)
            $n  = [Math]::Min(1023, $by.Length - 1)
            $seen = -join ($by[0..$n] | ForEach-Object {
                $c = $_ -band 0x7F; if ($c -ge 32 -and $c -lt 127) { [char]$c } else { ' ' } })
        }
        # Name the image. Which disk made the claim is part of the claim: the
        # list is searched, so the answer differs between machines and a bare
        # PASS would not say which one it stands for.
        Check "boots a real disk" ($seen -match "APPLE II") ("text page shows the ROM banner after a floppy boot -- " + (Split-Path $BootDisk -Leaf))
    } else {
        # NOT a pass. This branch used to hand the suite a green tick and a line
        # of the ALL GREEN tally for a check it never ran -- on every machine
        # without a boot disk in corpus/, which a fresh clone is until one is supplied.
        # The comment directly above calls this the widest check here; a suite
        # that scores it as passed while skipping it is the perfect score on the
        # wrong exam that comment warns about, printed in green.
        Check "boots a real disk" $false ("NO BOOT DISK AVAILABLE -- this suite cannot make its widest claim. Searched: " + (($bootSearch | Where-Object { $_ }) -join '; ') + " (set A2RAIL_BOOTDISK or pass -BootDisk)")
    }

    # ---- reset leaves the machine somewhere sane ---------------------------
    $rr = Say "reset"
    Check "reset" ($rr -match "status=OK") ""
}

Write-Host "=== RAIL SELF-TEST ".PadRight(64,'=') -ForegroundColor Cyan
Write-Host "  every verb challenged by an observation the verb does not control" -ForegroundColor DarkGray

foreach ($p in @(if ($All) { 3; 4; 5 } else { $Platform })) { Test-Platform $p }

Write-Host ""
if ($script:fail -eq 0) {
    Write-Host ("ALL GREEN -- {0} rail checks" -f $script:pass) -ForegroundColor Green
    exit 0
}
Write-Host ("{0} of {1} rail checks FAILED" -f $script:fail, ($script:pass + $script:fail)) -ForegroundColor Red
exit 1
