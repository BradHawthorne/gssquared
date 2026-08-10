<#
    session.ps1 -- start a headless gssquared session on the CTRL rail.

    The emulator's CPU only advances on an explicit `run N`, so a session starts
    paused. That is what makes injecting an engine build practical: load the code
    and content while nothing is executing, point PC at the entry, then run.

    Without this, bringing up a change means building a bootable disk image and
    letting DOS load it -- which puts a filesystem between the edit and the
    measurement, and the whole reason for this rail is to remove that.

    Drive it afterwards ONE command at a time with ctl.ps1. Not a batch: the
    replies are the only way to know what the machine actually did, and a queued
    list of commands forces every decision to be made before any of them is seen.
#>

[CmdletBinding()]
param(
    [string]$Dir      = $(if ($env:A2RAIL_DIR) { $env:A2RAIL_DIR } else { Join-Path $env:TEMP 'a2rail' }),
    [string]$Exe      = "",
    [int]   $TimeoutS = 3600,
    # 3 = Apple IIe Enhanced (the baseline), 4 = IIe Enhanced 65816, 5 = IIgs.
    # Parameterised so the engine can be RUN on the machines it claims to
    # support rather than assumed compatible with them. The IIc and IIc+ are
    # commented out of the emulator's platform table, so they stay a paper
    # argument until something can execute them.
    [int]   $Platform = 3,
    # -c N selects a builtin system CONFIG by index, where -p selects only a
    # platform and takes the FIRST config matching it. The two are different
    # things and it matters: a IIgs with an Uthernet II card, or with a Disk II
    # instead of the 3.5 drive, are separate configs on the same platform, and
    # -p alone can never reach any of them but the first. -1 keeps the old
    # behaviour exactly.
    [int]   $Config   = -1,
    # FOUNDATION_MILESTONES M1: fail if gssquared sources look newer than the exe.
    [switch]$StrictTools
)

$ErrorActionPreference = "Stop"

# A stale command/ack pair would be picked up as if it belonged to this session, so
# the PROTOCOL files are cleared -- but only those.
#
# This used to delete everything in the directory, which silently destroyed every
# artifact captured there: PNGs, vram dumps, memory reads. Comparing two builds
# meant starting a second session, and starting it deleted the first build's dump,
# so the comparison quietly had nothing to compare. Captured evidence must outlive
# the session that produced it.
if (Test-Path $Dir) {
    Get-ChildItem -Path $Dir -Filter "cmd.*" -File -EA SilentlyContinue | Remove-Item -Force -EA SilentlyContinue
    Get-ChildItem -Path $Dir -Filter "ack.*" -File -EA SilentlyContinue | Remove-Item -Force -EA SilentlyContinue
} else {
    New-Item -ItemType Directory $Dir -Force | Out-Null
}

$env:A2GSPU_CTRL         = $Dir
$env:A2GSPU_CTRL_TIMEOUT = "$TimeoutS"

# libstdc++-6.dll and friends live here; without it the process dies with
# STATUS_DLL_NOT_FOUND before printing anything at all.
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"

. (Join-Path $PSScriptRoot "toolpin.ps1")
if (-not $Exe) { $Exe = Get-GSSquaredExe }
if (-not (Test-Path $Exe)) { throw "GSSquared not found: $Exe" }
$pinFile = Join-Path $Dir "toolpin.txt"
# One pin line per session start (overwrite). cmd/ack were already cleared above.
if (Test-Path $pinFile) { Remove-Item $pinFile -Force -ErrorAction SilentlyContinue }
$null = Write-ToolPin -Label "GSSquared" -Exe $Exe -RepoHint "gssquared" -Strict:$StrictTools -ReportFile $pinFile

$log = Join-Path $Dir "session.log"
                             # -p 3 = Apple IIe Enhanced: the floor tier a2engine
                             # targets. No disk is mounted -- the CTRL rail
                             # injects the build directly, and a disk in slot 6
                             # would have its boot sector read straight over the
                             # code image at $0800.
$launchArgs = @("-p", "$Platform")
if ($Config -ge 0) { $launchArgs += @("-c", "$Config") }
# WORKING DIRECTORY, set explicitly. Files the emulator writes relative to its
# own CWD -- parallel.out from the printer card, speaker_event_log.txt,
# iigsmmutest's test.asm -- otherwise land wherever the launching shell happened
# to be standing, which differs between a suite run and a hand-run and is not
# discoverable from the rail at all. Pinning it to the session directory puts
# every emulator-produced artifact next to the session log, with the rest of the
# evidence.
Start-Process -FilePath $Exe -ArgumentList $launchArgs -WorkingDirectory $Dir `
              -RedirectStandardOutput $log `
              -RedirectStandardError (Join-Path $Dir "session.err") `
              -WindowStyle Hidden

Write-Host "CTRL rail: $Dir"
Write-Host "log:       $log"
Write-Host "waiting for the rail to come up..."

# Readiness is proved by a round trip on the rail, not by scraping the log. The
# emulator's stdout is redirected to a file and block-buffered, so the "rail is
# up" line can sit in a buffer for a long time after the rail is in fact serving
# commands -- scraping it reported failure on a session that was already working.
# The rail answering a command is the only thing that actually matters, and it is
# the thing being tested.
$probe = Join-Path $Dir "cmd.1"
# Renamed into place for the same reason ctl.ps1 does it: the emulator polls for
# cmd.N with a plain fopen and cannot tell a finished file from one still being
# written. Short commands rarely lose that race, but "rarely" is the property
# that makes a race expensive to diagnose later.
Set-Content -Path "$probe.tmp" -Value "cpu" -NoNewline
Move-Item -Path "$probe.tmp" -Destination $probe -Force
$ack = Join-Path $Dir "ack.1"
for ($i = 0; $i -lt 120; $i++) {          # 30s: ROM load and video init take a moment
    Start-Sleep -Milliseconds 250
    if (Test-Path $ack) {
        Write-Host "rail is up, CPU paused at:" -ForegroundColor Green
        Write-Host ("  " + (Get-Content $ack -Raw).Trim())
        exit 0
    }
}
Write-Host "rail did not answer within 30s -- check $log" -ForegroundColor Yellow
exit 1
