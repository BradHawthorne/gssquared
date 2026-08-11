<# Focused gate for discovery, ownership, JSON framing and live golden families. #>
[CmdletBinding()]param([string]$Dir=(Join-Path $env:TEMP ('a2rail-protocol-'+[guid]::NewGuid().ToString('N'))))
$ErrorActionPreference='Stop';$h=$PSScriptRoot;$owner='protocolcheck'
& 'C:\Program Files\PowerShell\7\pwsh.exe' -NoProfile -File (Join-Path $h 'session.ps1') -Dir $Dir -Platform 5 -Owner $owner -TimeoutS 60
function Q([string]$c){$global:LASTEXITCODE=0;$o=& (Join-Path $h 'ctl.ps1') -Dir $Dir -Owner $owner -Cmd $c; if($LASTEXITCODE){throw "$c failed: $o"};return($o-join"`n")}
$p=Q 'protocol';if($p-notmatch 'version=2'){throw $p}
$s=Q 'systems';if($s-notmatch 'platforms=6 configs=15'){throw $s}
$j=Q 'json cpu';$last=(Get-ChildItem $Dir -Filter 'ack.*'|Sort-Object {[int]($_.Name-replace'ack\.','')}|Select-Object -Last 1);$ack=Get-Content $last.FullName -Raw|ConvertFrom-Json;if(-not$ack.ok-or$ack.schema-ne'a2ctrl-reply-v2'){throw $j}
$g=Join-Path $Dir 'shr.golden';$null=Q("shr-golden $g bless");$m=Q("shr-golden $g");if($m-notmatch 'MATCH'){throw$m}
$bad=& (Join-Path $h 'ctl.ps1') -Dir $Dir -Owner wrong -Cmd cpu;if($LASTEXITCODE-ne3){throw'owner mismatch was not refused'};$global:LASTEXITCODE=0
$null=Q 'quit';Write-Host 'ALL GREEN -- protocol/discovery/json/ownership/SHR families' -ForegroundColor Green
