<#
    Cold-start and interrogate every built-in Apple II machine personality.
    Produces a durable JSON result so an agent does not need console history.
#>
[CmdletBinding()]
param(
    [string]$Dir = (Join-Path $env:TEMP 'a2rail-personalities'),
    [string]$Report = (Join-Path $Dir 'personality-results.json'),
    [int]$StartupTimeoutMs = 30000
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
. (Join-Path $here 'toolpin.ps1')
. (Join-Path $here 'runtime.ps1')
$null = Initialize-A2RailRuntime
$exe = Get-GSSquaredExe

$personalities = @(
    @{ id=0;  platform=0; name='Apple II'; cpu='6502' },
    @{ id=1;  platform=1; name='Apple II Plus'; cpu='6502' },
    @{ id=2;  platform=2; name='Apple IIe'; cpu='6502' },
    @{ id=3;  platform=3; name='Apple IIe Enhanced'; cpu='65C02' },
    @{ id=4;  platform=3; name='Apple IIe Enhanced w/Mouse'; cpu='65C02' },
    @{ id=5;  platform=3; name='Apple IIe Enhanced Dual Mockingboard'; cpu='65C02' },
    @{ id=6;  platform=4; name='Apple IIe 65816 w/Mouse'; cpu='65816' },
    @{ id=7;  platform=3; name='Apple IIe Enhanced PAL'; cpu='65C02' },
    @{ id=8;  platform=5; name='Apple IIgs'; cpu='65816' },
    @{ id=9;  platform=5; name='Apple IIgs / Disk II'; cpu='65816' },
    @{ id=10; platform=5; name='Apple IIgs + A2GSPU'; cpu='65816' },
    @{ id=11; platform=5; name='Apple IIgs + Uthernet II'; cpu='65816' },
    @{ id=12; platform=5; name='Apple IIgs ROM 4 Mark Twain'; cpu='65816' },
    @{ id=13; platform=5; name='Apple IIgs ROM 3'; cpu='65816' },
    @{ id=14; platform=3; name='Apple IIe Enhanced + Thunderclock'; cpu='65C02' }
)

function Wait-File([string]$Path, [int]$TimeoutMs, $Process) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.ElapsedMilliseconds -lt $TimeoutMs) {
        if (Test-Path $Path) { return $true }
        if ($Process.HasExited) { return $false }
        Start-Sleep -Milliseconds 50
    }
    return (Test-Path $Path)
}

if (-not (Test-Path $Dir)) { New-Item -ItemType Directory $Dir -Force | Out-Null }
$results = @()
foreach ($p in $personalities) {
    $pd = Join-Path $Dir ("p{0}" -f $p.id)
    if (-not (Test-Path $pd)) { New-Item -ItemType Directory $pd -Force | Out-Null }
    Get-ChildItem $pd -Filter 'cmd.*' -File -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem $pd -Filter 'ack.*' -File -ErrorAction SilentlyContinue | Remove-Item -Force
    $out = Join-Path $pd 'stdout.log'; $err = Join-Path $pd 'stderr.log'
    $oldCtrl=$env:A2GSPU_CTRL; $oldTimeout=$env:A2GSPU_CTRL_TIMEOUT
    $env:A2GSPU_CTRL=$pd; $env:A2GSPU_CTRL_TIMEOUT='60'
    $sw=[Diagnostics.Stopwatch]::StartNew()
    $proc=Start-Process -FilePath $exe -ArgumentList @('-c',"$($p.id)") `
        -WorkingDirectory $pd -RedirectStandardOutput $out -RedirectStandardError $err `
        -WindowStyle Hidden -PassThru
    $env:A2GSPU_CTRL=$oldCtrl; $env:A2GSPU_CTRL_TIMEOUT=$oldTimeout
    Set-Content (Join-Path $pd 'cmd.1.tmp') 'cpu' -NoNewline
    Move-Item (Join-Path $pd 'cmd.1.tmp') (Join-Path $pd 'cmd.1') -Force
    $ready=Wait-File (Join-Path $pd 'ack.1') $StartupTimeoutMs $proc
    $ack=if ($ready) { (Get-Content (Join-Path $pd 'ack.1') -Raw).Trim() } else { '' }
    if ($ready) {
        Set-Content (Join-Path $pd 'cmd.2.tmp') 'quit' -NoNewline
        Move-Item (Join-Path $pd 'cmd.2.tmp') (Join-Path $pd 'cmd.2') -Force
        $null=Wait-File (Join-Path $pd 'ack.2') 5000 $proc
    }
    if (-not $proc.HasExited) { try { $proc.WaitForExit(5000) | Out-Null } catch {} }
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    $stderr=if (Test-Path $err) { (Get-Content $err -Tail 20) -join "`n" } else { '' }
    $ok=$ready -and ($ack -match '^status=OK cpu ') -and ($stderr -notmatch 'Failed to stat|system_failure')
    $row=[pscustomobject]@{ config=$p.id; platform=$p.platform; name=$p.name; cpu=$p.cpu; pass=$ok;
        startup_ms=$sw.ElapsedMilliseconds; ack=$ack; stderr_tail=$stderr; dir=$pd }
    $results += $row
    $tag=if($ok){'PASS'}else{'FAIL'}
    Write-Host ("  {0} c{1,-2} p{2} {3,-39} {4}ms" -f `
        $tag,$p.id,$p.platform,$p.name,$sw.ElapsedMilliseconds) `
        -ForegroundColor $(if($ok){'Green'}else{'Red'})
}
$results | ConvertTo-Json -Depth 4 | Set-Content $Report -Encoding utf8
$failed=@($results | Where-Object { -not $_.pass })
Write-Host "report: $Report"
if ($failed.Count) { Write-Host "$($failed.Count) personalities failed" -ForegroundColor Red; exit 1 }
Write-Host 'ALL GREEN -- 15 built-in Apple II machine personalities' -ForegroundColor Green
exit 0
