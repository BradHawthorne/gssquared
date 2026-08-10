<#
    altzpcheck.ps1 -- ALTZP, the stack, and the language card.

    Chosen because gssquared's own compatibility notes blame this corner twice:
    "AppleWorks 4.3 ... works now after I fixed the issue with ALTZP not
    switching in LC Bank 1" and "A2Desktop 1.5 crashes ... Test correct behavior
    of: ALTZP with LC enabled". A place that has already produced two bugs is
    worth a standing check.

    It is also the exact fact a2engine got wrong three ways in one routine:
    RAMRD/RAMWRT redirect $0200-$BFFF, while $0000-$01FF follows ALTZP INSTEAD.
    Three things must move together when ALTZP flips, and they are easy to
    implement independently:

      1. zero page          $0000-$00FF
      2. THE STACK          $0100-$01FF  -- the one most often forgotten
      3. language card RAM  $D000-$FFFF banking follows ALTZP too

    Every check writes DIFFERENT values either side of the switch and reads both
    back, so "the switch does nothing" and "the switch works" cannot look alike.
    A test that wrote the same value to both banks would pass on a machine with
    no aux memory at all.
#>
[CmdletBinding()]
param(
    [int]$Platform = 5,
    [string]$Dir = $(if ($env:A2RAIL_DIR) { $env:A2RAIL_DIR } else { Join-Path $env:TEMP 'a2rail' })
)
$ErrorActionPreference = "Stop"
$ctl = Join-Path (Split-Path $PSScriptRoot -Parent) "ctl.ps1"
$tmp = Join-Path $Dir "gs816"
if (-not (Test-Path $tmp)) { New-Item -ItemType Directory $tmp -Force | Out-Null }
function Say([string]$c) { return ((& $ctl -Dir $Dir -Cmd $c) -join "`n") }

if (Get-Process -Name GSSquared -ErrorAction SilentlyContinue) { Say "quit" | Out-Null; Start-Sleep -Milliseconds 1300 }
& (Join-Path (Split-Path $PSScriptRoot -Parent) "session.ps1") -Dir $Dir -Platform $Platform | Out-Null

Write-Host ""
Write-Host ("=== ALTZP / STACK / LANGUAGE CARD -- platform {0} " -f $Platform).PadRight(64,'=') -ForegroundColor Cyan

$pass = 0; $fail = 0
function Check([string]$n, [bool]$ok, [string]$d="") {
    if ($ok) { Write-Host ("  PASS  {0,-26} {1}" -f $n,$d) -ForegroundColor Green; $script:pass++ }
    else     { Write-Host ("  FAIL  {0,-26} {1}" -f $n,$d) -ForegroundColor Red;   $script:fail++ }
}
function Grab([string]$addr,[int]$len) {
    $f = Join-Path $tmp "az.bin"
    if (Test-Path $f) { Remove-Item $f -Force }
    Say ("read {0} {1} {2}" -f $addr,$len,($f.Replace('\','/'))) | Out-Null
    if (Test-Path $f) { return [System.IO.File]::ReadAllBytes($f) }
    return @()
}
function RunProg([string]$bytes,[string]$stop) {
    Say "poke 00:6000 $bytes" | Out-Null
    Say "setreg pc 6000" | Out-Null
    Say "run-until $stop 100000" | Out-Null
}

# --- 1. zero page follows ALTZP -------------------------------------------
#   ALTZP off -> $50 = $11 ; ALTZP on -> $50 = $22 ; read both back
RunProg ("8D 08 C0 A9 11 85 50 8D 09 C0 A9 22 85 50 " +
         "8D 08 C0 A5 50 8D 00 70 8D 09 C0 A5 50 8D 01 70 " +
         "8D 08 C0 4C 21 60") "6021"
$r = Grab "7000" 2
Check "zero page follows ALTZP" ($r.Length -eq 2 -and $r[0] -eq 0x11 -and $r[1] -eq 0x22) `
      ("main `$50={0:X2}  aux `$50={1:X2}  (want 11 / 22)" -f $r[0], $r[1])

# --- 2. THE STACK follows ALTZP -------------------------------------------
# Push in main, push a different byte in aux, then pull each back in its own
# bank. If the stack did NOT switch, the second pull returns the first value.
#
# SP MUST BE RE-SET BEFORE EACH PULL. The stack pointer is a CPU register, not
# per-bank state: it does not switch with ALTZP. The first version of this check
# pulled twice without resetting it, so after the main PLA left SP at $FF the
# aux PLA incremented to $00 and read $0100 -- uninitialised aux memory, which
# came back $00 and looked exactly like "the stack does not follow ALTZP".
# The emulator was right and the test was wrong.
#
#   ALTZPOFF, SP=$FF, LDA #$AA, PHA      -> main $01FF = AA
#   ALTZPON,  SP=$FF, LDA #$BB, PHA      -> aux  $01FF = BB
#   ALTZPOFF, SP=$FE, PLA -> $7002       (want AA, from main $01FF)
#   ALTZPON,  SP=$FE, PLA -> $7003       (want BB, from aux  $01FF)
RunProg ("8D 08 C0 A2 FF 9A A9 AA 48 " +
         "8D 09 C0 A2 FF 9A A9 BB 48 " +
         "8D 08 C0 A2 FE 9A 68 8D 02 70 " +
         "8D 09 C0 A2 FE 9A 68 8D 03 70 " +
         "8D 08 C0 4C 29 60") "6029"
$r = Grab "7002" 2
Check "stack follows ALTZP" ($r.Length -eq 2 -and $r[0] -eq 0xAA -and $r[1] -eq 0xBB) `
      ("main pull={0:X2}  aux pull={1:X2}  (want AA / BB)" -f $r[0], $r[1])

# --- 3. language card RAM banking follows ALTZP ---------------------------
# Read $C08B twice to select LC bank 1 read+write RAM, write $D000 in main,
# switch ALTZP, write a different value, then read both back.
#   LDA $C08B / LDA $C08B   (write-enable LC bank 1)
#   ALTZPOFF, LDA #$C3, STA $D000
#   ALTZPON,  LDA $C08B x2, LDA #$D4, STA $D000
#   ALTZPOFF, LDA $D000 -> $7004     (want C3)
#   ALTZPON,  LDA $D000 -> $7005     (want D4)
RunProg ("AD 8B C0 AD 8B C0 " +
         "8D 08 C0 A9 C3 8D 00 D0 " +
         "8D 09 C0 AD 8B C0 AD 8B C0 A9 D4 8D 00 D0 " +
         "8D 08 C0 AD 00 D0 8D 04 70 " +
         "8D 09 C0 AD 00 D0 8D 05 70 " +
         "8D 08 C0 4C 3B 60") "603B"
$r = Grab "7004" 2
Check "LC RAM follows ALTZP" ($r.Length -eq 2 -and $r[0] -eq 0xC3 -and $r[1] -eq 0xD4) `
      ("main `$D000={0:X2}  aux `$D000={1:X2}  (want C3 / D4)" -f $r[0], $r[1])

# --- 4. NEGATIVE CONTROL: $0200-$BFFF must NOT follow ALTZP ---------------
# ALTZP covers $0000-$01FF only; everything above follows RAMRD/RAMWRT. If a
# write at $0300 tracked ALTZP, checks 1-3 could be passing for the wrong
# reason -- namely a switch that redirects far more than it should.
RunProg ("8D 08 C0 A9 5E 8D 00 03 " +
         "8D 09 C0 AD 00 03 8D 06 70 " +
         "8D 08 C0 4C 15 60") "6015"
$r = Grab "7006" 1
Check "ALTZP does NOT move `$0300" ($r.Length -eq 1 -and $r[0] -eq 0x5E) `
      ("`$0300 across the switch = {0:X2} (want 5E -- unchanged)" -f $r[0])

# --- 5a. RAMWRT splits the write banks ------------------------------------
# ALTZP moves $0000-$01FF; RAMRD/RAMWRT move $0200-$BFFF, as SEPARATE read and
# write switches. That split is the part that bites: a2engine once had a loop
# whose counter lived above $01FF, so with aux selected `DEC` wrote aux while
# `LDA` read main and the counter never decremented -- 900,000 instructions
# into ROM.
#
# RAMWRT is safe to throw from here because it only redirects WRITES; the CPU
# keeps fetching from main. Verified by reading both banks through the rail
# rather than through the guest, so the check does not depend on RAMRD as well.
RunProg ("8D 05 C0 A9 77 8D 00 40 " +
         "8D 04 C0 A9 88 8D 00 40 " +
         "4C 10 60") "6010"
$m = Grab "00:4000" 1
$x = Grab "01:4000" 1
Check "RAMWRT splits write banks" ($m.Length -and $x.Length -and $m[0] -eq 0x88 -and $x[0] -eq 0x77) `
      ("main `$4000={0:X2}  aux `$4000={1:X2}  (want 88 / 77)" -f $m[0], $x[0])

# --- 5b. RAMRD, thrown from PAGE 1 because it cannot be thrown from here ---
# RAMRD redirects $0200-$BFFF for READS -- and an instruction fetch is a read.
# Code living at $6000 that turns RAMRD on pulls itself out from under its own
# PC and continues into whatever aux happens to hold. The first version of this
# check did exactly that and ran away into ROM at $FA4D, which looked like a
# RAMRD bug and was the emulator being correct.
#
# The fix is the one a2engine already had to find for its aux copier: run the
# switch from PAGE 1, which follows ALTZP rather than RAMRD and therefore stays
# put. $0140 is below the stack and above the vectors.
#
#   $0140  RAMRD on ; LDA $4000 (now aux) ; RAMRD off ; STA $7009 ; spin
Say "poke 00:0140 8D 03 C0 AD 00 40 8D 02 C0 8D 09 70 4C 4C 01" | Out-Null
Say "poke 00:7009 00" | Out-Null
Say "setreg pc 0140" | Out-Null
Say "run-until 014C 10000" | Out-Null
$r = Grab "7009" 1
Check "RAMRD redirects reads" ($r.Length -eq 1 -and $r[0] -eq 0x77) `
      ("read of `$4000 with RAMRD on = {0:X2} (want 77, the aux value)" -f $r[0])

# --- 6. NEGATIVE CONTROL: RAMWRT must NOT move zero page ------------------
# The mirror of check 4. $0000-$01FF follows ALTZP, NOT RAMRD/RAMWRT, and an
# emulator that redirected everything on one switch would pass checks 1-5.
RunProg ("8D 04 C0 8D 08 C0 A9 3C 85 60 " +
         "8D 05 C0 A5 60 8D 04 C0 8D 0A 70 " +
         "4C 15 60") "6015"
$r = Grab "700A" 1
Check "RAMWRT does NOT move ZP" ($r.Length -eq 1 -and $r[0] -eq 0x3C) `
      ("`$60 with RAMWRT on = {0:X2} (want 3C -- unchanged)" -f $r[0])

# --- 7. IIgs SHADOWING, and that it can be turned OFF ---------------------
# Shadowing is what copies bank-$00 writes into the Mega II the display actually
# reads, so on a IIgs it sits between every program and the screen. That it
# WORKS is already covered by railcheck's vram check; what is untested is
# whether $C035 can INHIBIT it, which is the half a program relies on when it
# wants bank $00 as plain RAM.
#
# Differential by construction: write with shadowing on, write again with HGR
# page 1 inhibited (bit 1), then compare the two memories. The FPI must show
# both writes and the Mega II only the first. A machine that ignored $C035
# would show the second value in both, and one that never shadowed at all would
# show neither -- so both failure directions are distinguishable.
if ($Platform -eq 5) {
    RunProg ("A9 00 8D 35 C0 A9 AA 8D 00 20 " +
             "A9 02 8D 35 C0 A9 BB 8D 00 20 " +
             "A9 00 8D 35 C0 4C 19 60") "6019"
    $f = Grab "00:2000" 1
    $m = Grab "E0:2000" 1
    Check "`$C035 inhibits shadowing" ($f.Length -and $m.Length -and $f[0] -eq 0xBB -and $m[0] -eq 0xAA) `
          ("FPI `$00:2000={0:X2}  MegaII `$E0:2000={1:X2}  (want BB / AA)" -f $f[0], $m[0])
}

Write-Host ""
if ($fail -eq 0) { Write-Host ("ALL GREEN -- {0} ALTZP/LC checks" -f $pass) -ForegroundColor Green; exit 0 }
Write-Host ("{0} of {1} ALTZP/LC checks FAILED" -f $fail, ($pass+$fail)) -ForegroundColor Red
exit 1
