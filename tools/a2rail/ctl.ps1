# ONE command against an already-running A2GSPU_CTRL session.
#
# Replaces the earlier -Cmds array driver, whose batch shape made the closed loop
# impossible: it forced every decision to be made before any reply was seen. The
# cmd.N/ack.N protocol is a PERSISTENT session by design -- the emulator keeps
# running between calls -- so the correct usage is one command, look, decide, next.
#
# Sequence is derived from the directory, so this works across separate shell
# invocations (each tool call gets a fresh shell).
#
# ORDERING GUARANTEE (added after a real desync): the emulator services cmd.1,
# cmd.2, ... strictly in order. If a long `run` outlives our ack wait, the earlier
# driver simply gave up and let the caller issue the NEXT command -- so commands
# piled into a queue while the caller believed each had completed, and every reply
# after that point was being read against the wrong mental state. One `run 2100`
# that overran a 30s wait produced exactly that, and a later verb came back
# "unknown-cmd" for no visible reason.
#
# Fix: never issue cmd.N until ack.(N-1) exists. A timeout is then a *stall*, not a
# corruption -- the session stays consistent and the next call resumes waiting.
#
# EXIT CODES. Every path out of this script sets one, so a caller can branch on
# the outcome instead of regex-matching the text. The stall path used to fall off
# the end of the script without an exit, which leaves $LASTEXITCODE holding
# whatever the CALLER's previous command set -- so a stall could be read as
# success, or as some unrelated failure code, depending on what ran before it.
# The only reason no in-tree caller tripped on that is that railcheck/execcheck/
# altzpcheck's Say ignores the code entirely and protocolcheck zeroes it first.
#
#   0  acked -- the emulator replied and the reply is on stdout
#   1  a PREVIOUS command has not acked; this one was NOT issued
#   2  no session (no session.json, or its pid is gone), or invalid metadata
#   3  owner mismatch; command not issued
#   4  issued but not yet acked -- queued, still consistent, retry to keep waiting
#
# 4 is deliberately distinct from 1: 'issued and pending' and 'refused because
# something else is pending' need different handling, and both differ from a
# refusal. A caller that retries drains the queue either way.
param(
  [Parameter(Mandatory=$true)][string]$Cmd,
  [string]$Dir = $(if ($env:A2RAIL_DIR) { $env:A2RAIL_DIR } else { Join-Path $env:TEMP 'a2rail' }),
  [string]$Owner = "",
  [string]$Report = $(if ($env:A2RAIL_REPORT) { $env:A2RAIL_REPORT } else { "" }),
  [int]$TimeoutMs = 180000
)

$started = [DateTime]::UtcNow
function Write-RailEvent([string]$event, [hashtable]$fields = @{}) {
  if (-not $Report) { return }
  $row = [ordered]@{
    schema = 'a2rail-event-v1'; event = $event
    timestamp_utc = [DateTime]::UtcNow.ToString('o')
    duration_ms = [int]([DateTime]::UtcNow - $started).TotalMilliseconds
    ctrl = $Dir; owner = $Owner
  }
  foreach ($key in $fields.Keys) { $row[$key] = $fields[$key] }
  $parent = Split-Path $Report -Parent
  if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
  Add-Content -LiteralPath $Report -Value ($row | ConvertTo-Json -Compress -Depth 5) -Encoding utf8
}

$sessionFile = Join-Path $Dir 'session.json'
$session = $null
if (Test-Path $sessionFile) {
  try { $session = Get-Content $sessionFile -Raw | ConvertFrom-Json } catch {
    "[--] INVALID SESSION METADATA: $sessionFile"
    exit 2
  }
}
if ($Owner -and $session -and $session.owner -ne $Owner) {
  Write-RailEvent 'refused' @{ reason='owner-mismatch'; session_owner=$session.owner; command=$Cmd }
  "[--] OWNER MISMATCH: requested=$Owner session=$($session.owner); command not issued."
  exit 3
}

# A dead session cannot ever ack, so waiting the full timeout for one only delays
# the diagnosis. The rail quits itself on A2GSPU_CTRL_TIMEOUT idle seconds, which
# is easy to trip while reading code between commands -- and the symptom was a
# silent 120s stall that looked like a hung emulator rather than an absent one.
#
# THE SESSION IS THE DIRECTORY, not the process name. This used to fall back to
# `Get-Process -Name GSSquared` whenever $Dir held no session.json, which made
# ANY emulator anywhere on the machine -- another agent's, a human's -- count as
# proof that THIS rail was alive. Against an empty directory that turned an
# instant "NO SESSION" into a full-timeout STALL with a cmd.1 left behind for
# nobody to read. session.ps1 always writes session.json before it launches
# anything, so an absent file means no session here, full stop; there is no case
# where the name fallback was right and the pid was not available.
# A PID IS NOT A SESSION. Windows reuses process ids, and a gate run launches
# emulators back to back: a session.json left by an earlier run named a pid that
# another process -- often the next suite's emulator -- now held, so the suite
# "quit" a session that did not exist and waited out the 180 s ack timeout
# (measured: IIgs railcheck 184 s, every command in its last 2 s). The pid counts
# only if it is a GSSquared that started within 30 s of this session's
# started_utc, which session.ps1 writes just before it launches.
function Test-A2RailSessionProcess($s) {
    if (-not $s -or -not $s.pid) { return $false }
    $p = Get-Process -Id ([int]$s.pid) -ErrorAction SilentlyContinue
    if (-not $p -or $p.ProcessName -ne 'GSSquared') { return $false }
    if ($s.started_utc) {
        try {
            # ConvertFrom-Json (PowerShell 7) already made it a DateTime; a [string]
            # cast would drop its UTC kind and shift it by the zone offset.
            $t0 = if ($s.started_utc -is [DateTime]) { $s.started_utc.ToUniversalTime() }
                  else { [DateTime]::Parse([string]$s.started_utc, $null, [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime() }
            if ([Math]::Abs(($p.StartTime.ToUniversalTime() - $t0).TotalSeconds) -gt 30) { return $false }
        } catch { }
    }
    return $true
}
function Test-SessionAlive {
  return (Test-A2RailSessionProcess $session)
}

# WAIT ON THE FILESYSTEM, NOT ON A TICK. This slept 100 ms between Test-Path
# checks, so no ack could be seen sooner than ~100 ms after the command was
# issued, whatever the emulator did: with the rail's own poll it made a command
# cost ~125 ms from PowerShell (medians measured across the gate runs). A watcher
# on the one ack file wakes when the emulator renames it into place. The existence
# check at the top of every slice covers the ack that lands between two waits, and
# the 250 ms slice bounds how late that rare case can be seen; the session is
# still checked for life about every two seconds, as before.
function Wait-Ack([string]$path, [int]$ms) {
  if ([IO.File]::Exists($path)) { return $true }
  $watcher = New-Object IO.FileSystemWatcher ([IO.Path]::GetDirectoryName($path)), ([IO.Path]::GetFileName($path))
  try {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $lastLife = 0
    while ($clock.ElapsedMilliseconds -lt $ms) {
      if ([IO.File]::Exists($path)) { return $true }
      $null = $watcher.WaitForChanged([IO.WatcherChangeTypes]::All, 250)
      if ($clock.ElapsedMilliseconds - $lastLife -ge 2000) {
        $lastLife = $clock.ElapsedMilliseconds
        if (-not [IO.File]::Exists($path) -and -not (Test-SessionAlive)) { return $false }
      }
    }
    return [IO.File]::Exists($path)
  } finally {
    $watcher.Dispose()
  }
}

# READ THE ACK THROUGH A SHARING VIOLATION. The emulator renames ack.N into place
# atomically and closes it first, yet a gate run on 2026-09-16 still failed with
# "The process cannot access the file 'ack.21' because it is being used by another
# process" -- something else on the host (a file scanner inspecting the freshly
# created file is the usual suspect) held it open for an instant. Rerun, the same
# suite was green: a race, not a rail defect. Retry that one IOException briefly
# instead of reporting a harness hiccup as a failed check; any other error, or a
# lock that outlasts ~3 s, still surfaces. runtime.ps1 has the twin for the scripts
# that load it.
function Read-AckText([string]$path) {
  for ($i = 0; ; $i++) {
    try {
      return [IO.File]::ReadAllText($path)
    } catch [IO.IOException] {
      if ($i -ge 30) { throw }
      Start-Sleep -Milliseconds 100
    }
  }
}

if (-not (Test-SessionAlive)) {
  # Say WHICH of the two it is. "GSSquared is not running" was a guess dressed as
  # a fact -- it was printed both when this directory had never had a session and
  # when one had died, and those need different actions (start one here; or look
  # at session.log for why the last one went away).
  if (-not $session) {
    Write-RailEvent 'refused' @{ reason='no-session'; detail='no-session-json'; command=$Cmd }
    "[--] NO SESSION: no session.json in $Dir. Start one with tools\session.ps1"
  } elseif (-not $session.pid) {
    Write-RailEvent 'refused' @{ reason='no-session'; detail='session-json-has-no-pid'; command=$Cmd }
    "[--] NO SESSION: $sessionFile records no pid. Start one with tools\session.ps1"
  } else {
    Write-RailEvent 'refused' @{ reason='no-session'; detail='pid-gone'; pid=$session.pid; command=$Cmd }
    "[--] NO SESSION: session pid $($session.pid) is gone. Start one with tools\session.ps1"
    "     (see $(Join-Path $Dir 'session.log') for how the last one ended)."
  }
  "     (the rail self-quits after A2GSPU_CTRL_TIMEOUT idle seconds)."
  exit 2
}

# cmd.N only: a client's cmd.N.tmp caught mid-rename would otherwise fail the [int]
# cast and abort this invocation.
$existing = Get-ChildItem (Join-Path $Dir "cmd.*") -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^cmd\.\d+$' } |
            ForEach-Object { [int]($_.Name -replace '^cmd\.','') }
$seq = 1
if ($existing) { $seq = ([int]($existing | Measure-Object -Maximum).Maximum) + 1 }

# Drain: the previous command must have completed, or we would be queueing behind
# an unfinished one and reading replies against a state that never existed.
if ($seq -gt 1) {
  $prev = Join-Path $Dir ("ack." + ($seq - 1))
  if (-not (Test-Path $prev)) {
    if (-not (Wait-Ack $prev $TimeoutMs)) {
      Write-RailEvent 'stalled' @{ sequence=($seq-1); reason='previous-command'; command=$Cmd }
      "[--] STALLED: cmd.$($seq-1) has not acked; not issuing '$Cmd'."
      "     Session is still consistent -- re-run this command to keep waiting."
      exit 1
    }
  }
}

# ATOMIC HANDOFF. The emulator polls for cmd.N with a plain fopen/fread and has
# no way to tell a finished file from one still being written. Writing in place
# let it observe a TRUNCATED command: `read <addr> <len> <long path>` is the
# longest command the validation harness issues, and it intermittently arrived
# cut short, matched no handler, and acked "unknown-cmd" for a command that was
# perfectly valid. Re-issuing it verbatim then succeeded, which is the signature
# of a race rather than a syntax error.
#
# The emulator has written its ACKS atomically since the same hazard was found
# in the reply direction. This is the other half of that protocol: rename is
# atomic on NTFS, so the reader sees the whole command or no file at all.
$tmp = Join-Path $Dir "cmd.$seq.tmp"
Set-Content -Path $tmp -Value $Cmd -NoNewline
Move-Item -Path $tmp -Destination (Join-Path $Dir "cmd.$seq") -Force
Write-RailEvent 'issued' @{ sequence=$seq; pid=$(if($session){$session.pid}else{$null}); command=$Cmd }
$ack = Join-Path $Dir "ack.$seq"
if (Wait-Ack $ack $TimeoutMs) {
  $reply = (Read-AckText $ack).Trim()
  Write-RailEvent 'ack' @{ sequence=$seq; pid=$(if($session){$session.pid}else{$null}); command=$Cmd; reply=$reply }
  "[$seq] $Cmd"
  "     -> " + $reply
} elseif (-not (Test-SessionAlive)) {
  # THE EMULATOR DIED WITH THE COMMAND PENDING. Wait-Ack gives up within ~2s when
  # the session pid is gone, and this used to fall into the stall text below:
  # "STALL after 180000ms. Command IS queued and will complete" -- three claims,
  # all false, printed about a process that had crashed in the first two seconds
  # (a heap corruption on a breakpoint hit, found exactly this way). Nothing will
  # ever ack it; say that, and exit 2 like any other absent session.
  Write-RailEvent 'died' @{ sequence=$seq; pid=$(if($session){$session.pid}else{$null}); reason='pid-gone-while-pending'; command=$Cmd }
  "[$seq] $Cmd"
  "     -> SESSION DIED: pid $(if($session){$session.pid}else{'?'}) exited with cmd.$seq pending; it will never ack."
  "        See $Dir\session.log and session.err for how it ended."
  exit 2
} else {
  Write-RailEvent 'stalled' @{ sequence=$seq; pid=$(if($session){$session.pid}else{$null}); reason='ack-timeout'; command=$Cmd }
  "[$seq] $Cmd"
  "     -> STALL after ${TimeoutMs}ms. Command IS queued and will complete;"
  "        the next invocation drains it before issuing anything new."
  # Exit 4, not 0 and not "fall off the end". Falling off the end leaves
  # $LASTEXITCODE at whatever the caller's previous command set, so a stall was
  # indistinguishable from success -- and from any other code -- to anyone who
  # checked the status rather than the text. The command really is queued, so
  # this is not a refusal: 4 says 'issued, pending' so the caller can retry and
  # let the drain logic above finish the wait.
  exit 4
}
