<# Prove windowed human-input mode and read-only agent telemetry coexist. #>
[CmdletBinding()]
param([string]$Dir=(Join-Path $env:TEMP ('a2rail-copilot-'+[guid]::NewGuid().ToString('N'))))
$ErrorActionPreference='Stop'; $here=$PSScriptRoot
. (Join-Path $here 'toolpin.ps1'); . (Join-Path $here 'runtime.ps1'); $null=Initialize-A2RailRuntime
New-Item -ItemType Directory $Dir -Force|Out-Null
$env:A2GSPU_COPILOT=$Dir
$p=Start-Process -FilePath (Get-GSSquaredExe) -ArgumentList @('-p','3') -WorkingDirectory $Dir `
  -RedirectStandardOutput (Join-Path $Dir 'stdout.log') -RedirectStandardError (Join-Path $Dir 'stderr.log') -PassThru
$env:A2GSPU_COPILOT=$null
function Ask([int]$n,[string]$cmd){
  Set-Content (Join-Path $Dir "cmd.$n.tmp") $cmd -NoNewline; Move-Item (Join-Path $Dir "cmd.$n.tmp") (Join-Path $Dir "cmd.$n") -Force
  for($i=0;$i-lt 200;$i++){Start-Sleep -Milliseconds 50;$a=Join-Path $Dir "ack.$n";if(Test-Path $a){return(Get-Content $a -Raw).Trim()};if($p.HasExited){break}}
  throw "copilot command $n did not ack; exited=$($p.HasExited)"
}
try {
  $cpu=Ask 1 'cpu'; $contract=Ask 2 'copilot'; $deny=Ask 3 'poke 0400 00'
  if($cpu-notmatch '^status=OK cpu '){throw "cpu failed: $cpu"}
  if($contract-notmatch 'mode=windowed-read-only'){throw "contract failed: $contract"}
  if($deny-notmatch '^status=FAIL copilot-read-only'){throw "write was not refused: $deny"}
  Write-Host 'ALL GREEN -- windowed co-pilot telemetry + write refusal' -ForegroundColor Green
} finally { if(-not $p.HasExited){Stop-Process -Id $p.Id -Force} }
