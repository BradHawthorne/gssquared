<# Prove windowed agent observe+control, live observation arming (rail set, trap), and that splices and halting rails are still refused. #>
[CmdletBinding()]
param([string]$Dir=(Join-Path $env:TEMP ('a2rail-copilot-'+[guid]::NewGuid().ToString('N'))))
$ErrorActionPreference='Stop'; $here=$PSScriptRoot
. (Join-Path $here 'toolpin.ps1'); . (Join-Path $here 'runtime.ps1'); $null=Initialize-A2RailRuntime
New-Item -ItemType Directory $Dir -Force|Out-Null
$env:A2GSPU_COPILOT=$Dir; $env:A2GSPU_BACKGROUND_WINDOW='1'   # a gate's window must not take the user's focus
$p=Start-Process -FilePath (Get-GSSquaredExe) -ArgumentList @('-p','3') -WorkingDirectory $Dir `
  -RedirectStandardOutput (Join-Path $Dir 'stdout.log') -RedirectStandardError (Join-Path $Dir 'stderr.log') -PassThru
$env:A2GSPU_COPILOT=$null; $env:A2GSPU_BACKGROUND_WINDOW=$null
function Ask([int]$n,[string]$cmd){
  Set-Content (Join-Path $Dir "cmd.$n.tmp") $cmd -NoNewline; Move-Item (Join-Path $Dir "cmd.$n.tmp") (Join-Path $Dir "cmd.$n") -Force
  for($i=0;$i-lt 200;$i++){Start-Sleep -Milliseconds 50;$a=Join-Path $Dir "ack.$n";if(Test-Path $a){return(Get-Content $a -Raw).Trim()};if($p.HasExited){break}}
  throw "copilot command $n did not ack; exited=$($p.HasExited)"
}
try {
  Start-Sleep -Milliseconds 3000
  $cpu=Ask 1 'cpu'; $contract=Ask 2 'copilot'; $deny=Ask 3 'poke 0400 00'; $press=Ask 4 'press 41 0'
  if($cpu-notmatch '^status=OK cpu '){throw "cpu failed: $cpu"}
  if($contract-notmatch 'mode=windowed-agent'){throw "contract failed: $contract"}
  if($deny-notmatch '^status=FAIL copilot-command-not-allowed'){throw "instrumentation poke was not refused: $deny"}
  if($press-notmatch '^status=OK press'){throw "press failed: $press"}
  # Observation arms live in a windowed session; a rail that halts does not.
  $live=Ask 5 'rail live'; $set=Ask 6 'rail set A2GSPU_INTLOG 1'; $halt=Ask 7 'rail set A2GSPU_STOP_ON_FAULT 1'
  $trap=Ask 8 'trap add name=cp at=FA62 once'; $unset=Ask 9 'rail unset A2GSPU_INTLOG'
  if($live-notmatch '^status=OK rails='){throw "rail live failed: $live"}
  if($set-notmatch '^status=OK rail set A2GSPU_INTLOG'){throw "windowed rail set failed: $set"}
  if($halt-notmatch '^status=FAIL copilot-rail-not-windowed'){throw "a halting rail was not refused windowed: $halt"}
  if($trap-notmatch '^status=OK trap added cp'){throw "windowed trap add failed: $trap"}
  if($unset-notmatch '^status=OK rail unset A2GSPU_INTLOG'){throw "windowed rail unset failed: $unset"}
  Write-Host 'ALL GREEN -- windowed agent observe+control and live observation arming; poke and halting rails refused' -ForegroundColor Green
} finally {
  if(-not $p.HasExited){
    try {
      Set-Content (Join-Path $Dir 'cmd.10.tmp') 'quit' -NoNewline
      Move-Item (Join-Path $Dir 'cmd.10.tmp') (Join-Path $Dir 'cmd.10') -Force
      $p.WaitForExit(5000)|Out-Null
    } catch {}
    if(-not $p.HasExited){Stop-Process -Id $p.Id -Force}
  }
}
