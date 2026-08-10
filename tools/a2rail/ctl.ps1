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
param(
  [Parameter(Mandatory=$true)][string]$Cmd,
  [string]$Dir = $(if ($env:A2RAIL_DIR) { $env:A2RAIL_DIR } else { Join-Path $env:TEMP 'a2rail' }),
  [int]$TimeoutMs = 180000
)

# A dead session cannot ever ack, so waiting the full timeout for one only delays
# the diagnosis. The rail quits itself on A2GSPU_CTRL_TIMEOUT idle seconds, which
# is easy to trip while reading code between commands -- and the symptom was a
# silent 120s stall that looked like a hung emulator rather than an absent one.
function Test-SessionAlive {
  return [bool](Get-Process -Name GSSquared -ErrorAction SilentlyContinue)
}

function Wait-Ack([string]$path, [int]$ms) {
  $w = 0
  while (-not (Test-Path $path) -and $w -lt $ms) {
    Start-Sleep -Milliseconds 100; $w += 100
    if (($w % 2000) -eq 0 -and -not (Test-SessionAlive)) { return $false }
  }
  return (Test-Path $path)
}

if (-not (Test-SessionAlive)) {
  "[--] NO SESSION: GSSquared is not running. Start one with tools\session.ps1"
  "     (the rail self-quits after A2GSPU_CTRL_TIMEOUT idle seconds)."
  exit 2
}

$existing = Get-ChildItem (Join-Path $Dir "cmd.*") -ErrorAction SilentlyContinue |
            ForEach-Object { [int]($_.Name -replace '^cmd\.','') }
$seq = 1
if ($existing) { $seq = ([int]($existing | Measure-Object -Maximum).Maximum) + 1 }

# Drain: the previous command must have completed, or we would be queueing behind
# an unfinished one and reading replies against a state that never existed.
if ($seq -gt 1) {
  $prev = Join-Path $Dir ("ack." + ($seq - 1))
  if (-not (Test-Path $prev)) {
    if (-not (Wait-Ack $prev $TimeoutMs)) {
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
$ack = Join-Path $Dir "ack.$seq"
if (Wait-Ack $ack $TimeoutMs) {
  "[$seq] $Cmd"
  "     -> " + (Get-Content $ack -Raw).Trim()
} else {
  "[$seq] $Cmd"
  "     -> STALL after ${TimeoutMs}ms. Command IS queued and will complete;"
  "        the next invocation drains it before issuing anything new."
}
