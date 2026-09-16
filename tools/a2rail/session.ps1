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
    [string]$Owner    = ([guid]::NewGuid().ToString('N')),
    # Reproducible pin: fail if gssquared sources look newer than the exe.
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

# The emulator reads A2GSPU_CTRL at spawn, so these only need to be set
# across Start-Process -- and they MUST NOT outlive it. session.ps1 is
# called IN-PROCESS by railcheck/execcheck/altzpcheck/protocolcheck, so a
# value left behind here leaks into the caller shell and into every later
# gate it runs. A leaked A2GSPU_CTRL sends the NEXT emulator to the
# PREVIOUS gate's rail directory, where it replays that directory's
# stale commands and quits -- copilotcheck then reports "did not ack;
# exited=True", which reads as an emulator crash rather than an
# environment bug. Saved and restored around the launch below, the same
# way personalitycheck.ps1 already does it.
$a2railOldCtrl        = $env:A2GSPU_CTRL
$a2railOldCtrlTimeout = $env:A2GSPU_CTRL_TIMEOUT
$a2railOldCtrlAtomic  = $env:A2GSPU_CTRL_ATOMIC
$env:A2GSPU_CTRL         = $Dir
$env:A2GSPU_CTRL_TIMEOUT = "$TimeoutS"
# ctl.ps1 writes cmd.N as a temp file and renames it into place, so the rail can
# skip the re-read guard it keeps for clients that write in place.
$env:A2GSPU_CTRL_ATOMIC  = "1"

# libstdc++-6.dll and friends must be discoverable before launch. Keep this in
# one shared preflight so ctest, personality gates and sessions cannot drift.
. (Join-Path $PSScriptRoot "runtime.ps1")
$null = Initialize-A2RailRuntime

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
$proc = Start-Process -FilePath $Exe -ArgumentList $launchArgs -WorkingDirectory $Dir `
              -RedirectStandardOutput $log `
              -RedirectStandardError (Join-Path $Dir "session.err") `
              -WindowStyle Hidden -PassThru

# The child has inherited them; put the caller's environment back.
$env:A2GSPU_CTRL         = $a2railOldCtrl
$env:A2GSPU_CTRL_TIMEOUT = $a2railOldCtrlTimeout
$env:A2GSPU_CTRL_ATOMIC  = $a2railOldCtrlAtomic

@{
    schema = "a2rail-session-v1"
    protocol = 2
    owner = $Owner
    pid = $proc.Id
    exe = $Exe
    platform = $Platform
    config = $Config
    ctrl = $Dir
    started_utc = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json | Set-Content (Join-Path $Dir 'session.json') -Encoding utf8

Write-Host "CTRL rail: $Dir"
Write-Host "log:       $log"
Write-Host "waiting for the rail to come up..."

# Readiness is proved by a round trip on the rail, not by scraping the log. The
# rail answering a command is the only thing that actually matters, and it is the
# thing being tested; a banner in the log is a proxy for it at best.
#
# It was also, for a long time, not even available: stdout redirected to a file
# is block-buffered by the CRT, so the "rail is up" line could sit in a buffer
# long after the rail was serving commands, and scraping it reported failure on a
# session that was already working. That is fixed at the source now -- gs2.cpp's
# SDL_AppInit sets stdout unbuffered, because the same buffer was swallowing
# every mid-session diagnostic the emulator printed, the snapshot manifest
# included, and losing all of it outright if the session was killed rather than
# quitting. So session.log is live from here on. The round trip stays anyway:
# the log says what the emulator believes, the ack says what it can do.
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
        Write-Host ("  " + (Read-AckText $ack).Trim())
        exit 0
    }
    if ($proc.HasExited) {
        $err = Join-Path $Dir 'session.err'
        $tail = if (Test-Path $err) { (Get-Content $err -Tail 20) -join "`n" } else { '<no stderr>' }
        # "The emulator could not start" and "somebody killed the emulator" are
        # different facts and used to print the same red line. A harness sweeping
        # the machine by process NAME -- which dualsmoke.ps1 did for a long time,
        # and a second concurrent run still can -- kills a healthy session mid
        # handshake, and reporting that as a startup failure sends the reader
        # hunting a bug in the emulator that is not there.
        #
        # Stop-Process -Force surfaces as exit -1; TerminateProcess via a console
        # close surfaces as 0xC000013A (STATUS_CONTROL_C_EXIT, -1073741510 as a
        # signed Int32). Neither is anything the emulator chose to return.
        # Exit 3 is the caller's signal to record 'aborted-externally' rather
        # than to print FAIL beside real check failures.
        $code = $proc.ExitCode
        if ($code -eq -1 -or $code -eq -1073741510) {
            Write-Host ("rail process was TERMINATED externally (pid={0} exit={1}) -- this is not a check failure`n{2}" -f `
                        $proc.Id, $code, $tail) -ForegroundColor Yellow
            exit 3
        }
        Write-Host ("rail process exited before readiness: pid={0} exit={1}`n{2}" -f `
                    $proc.Id, $code, $tail) -ForegroundColor Red
        exit 1
    }
}
Write-Host ("rail did not answer within 30s: pid={0} alive={1} log={2}" -f `
           $proc.Id, (-not $proc.HasExited), $log) -ForegroundColor Yellow
exit 1
