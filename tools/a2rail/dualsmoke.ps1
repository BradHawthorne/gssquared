<#
    dualsmoke.ps1 -- short dual-platform gate for shared rail/instrument work.

    FOUNDATION_MILESTONES M6: IIe-green is not ship-green for shared gssquared
    code. This is the command agents and humans run after a rail/MMU/device
    change that is not sealed to one platform.

        .\tools\dualsmoke.ps1
        .\tools\dualsmoke.ps1 -SkipOffline

    Portable copy (no a2engine required) lives in gssquared-fork:

        <gssquared>\tools\a2rail\dualsmoke.ps1

    Refresh that copy after editing this stack: .\tools\sync-a2rail.ps1

    Suites (intentionally small -- full gscheck remains the deep net):

      offline     gssquared ctest (no emulator)
      rail IIe    railcheck -Platform 3
      rail IIgs   railcheck -Platform 5
      exec IIe    execcheck -Platform 3
      exec IIgs   execcheck -Platform 5
      altzp IIe   altzpcheck -Platform 3
      altzp IIgs  altzpcheck -Platform 5

    Exit 0 only if every suite returns 0.
#>
[CmdletBinding()]
param(
    [switch]$SkipOffline,
    [string]$Dir = $(if ($env:A2RAIL_DIR) { $env:A2RAIL_DIR } else { Join-Path $env:TEMP 'a2rail' }),
    [string]$Report = (Join-Path $(if ($env:A2RAIL_DIR) { $env:A2RAIL_DIR } else { Join-Path $env:TEMP 'a2rail' }) 'dualsmoke.ndjson')
)
$ErrorActionPreference = "Continue"
$here = $PSScriptRoot
$results = @()

function Write-SuiteEvent([hashtable]$Event) {
    $Event.schema='a2suite-event-v1'; $Event.ts=[DateTime]::UtcNow.ToString('o')
    [IO.File]::AppendAllText($Report, (($Event | ConvertTo-Json -Compress -Depth 4) + [Environment]::NewLine))
    Write-Host ("SUITE {0} {1}" -f $Event.event.ToUpperInvariant(),$Event.suite) -ForegroundColor DarkCyan
}

function Run-One([string]$name, [scriptblock]$body) {
    Write-Host ""
    Write-Host ("##### {0} " -f $name).PadRight(64, '#') -ForegroundColor Cyan
    $started=[DateTime]::UtcNow
    $before=@(Get-Process GSSquared -ErrorAction SilentlyContinue | ForEach-Object Id)
    Write-SuiteEvent @{event='start';suite=$name;wrapper_pid=$PID}
    & $body
    $suiteExit=$LASTEXITCODE; $ok = ($suiteExit -eq 0)
    $leaked=@(Get-Process GSSquared -ErrorAction SilentlyContinue | Where-Object { $before -notcontains $_.Id })
    foreach($proc in $leaked){ Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    $ms=[int]([DateTime]::UtcNow-$started).TotalMilliseconds
    $script:results += [pscustomobject]@{ Suite = $name; Pass = $ok; DurationMs=$ms }
    Write-SuiteEvent @{event='end';suite=$name;pass=$ok;exit_code=$suiteExit;duration_ms=$ms;leaked_processes=@($leaked|ForEach-Object Id)}
    if (-not $ok) {
        Write-Host ("##### FAILED: {0}" -f $name) -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "=== DUALSMOKE (IIe + IIgs instrument gate) ".PadRight(64, '=') -ForegroundColor Cyan
Write-Host "  FOUNDATION_MILESTONES M6 -- shared rail changes need both platforms" -ForegroundColor DarkGray

. (Join-Path $here "toolpin.ps1")
. (Join-Path $here "runtime.ps1")
$null = Initialize-A2RailRuntime
$reportDir=Split-Path $Report -Parent
if($reportDir -and -not(Test-Path $reportDir)){New-Item -ItemType Directory $reportDir -Force|Out-Null}
if(Test-Path $Report){Remove-Item $Report -Force}
Write-SuiteEvent @{event='run-start';suite='dualsmoke';report=$Report;wrapper_pid=$PID}
try { $null = Write-ToolPin -Label "GSSquared" -Exe (Get-GSSquaredExe) -RepoHint "gssquared" } catch {}

if (-not $SkipOffline) {
    $gsBuild = Join-Path (Get-GssquaredRoot) "build"
    Run-One "gssquared ctest (offline)" {
        Push-Location $gsBuild
        & ctest --output-on-failure
        Pop-Location
    }
}

Run-One "railcheck (IIe / platform 3)"  { & (Join-Path $here "railcheck.ps1") -Platform 3 -Dir $Dir }
Run-One "railcheck (IIgs / platform 5)" { & (Join-Path $here "railcheck.ps1") -Platform 5 -Dir $Dir }
Run-One "exec control (IIe)"            { & (Join-Path $here "gs816\execcheck.ps1") -Platform 3 -Dir $Dir }
Run-One "exec control (IIgs)"           { & (Join-Path $here "gs816\execcheck.ps1") -Platform 5 -Dir $Dir }
Run-One "ALTZP/LC (IIe)"                { & (Join-Path $here "gs816\altzpcheck.ps1") -Platform 3 -Dir $Dir }
Run-One "ALTZP/LC (IIgs)"               { & (Join-Path $here "gs816\altzpcheck.ps1") -Platform 5 -Dir $Dir }

Write-Host ""
Write-Host "=== DUALSMOKE MATRIX ".PadRight(64, '=') -ForegroundColor Cyan
$fail = 0
foreach ($r in $results) {
    $tag = if ($r.Pass) { "PASS" } else { "FAIL"; $fail++ }
    $col = if ($r.Pass) { "Green" } else { "Red" }
    Write-Host ("  {0}  {1}" -f $tag, $r.Suite) -ForegroundColor $col
}
Write-Host ""
if ($fail -eq 0) {
    Write-SuiteEvent @{event='run-end';suite='dualsmoke';pass=$true;total=$results.Count;failed=0}
    Write-Host ("ALL GREEN -- {0} dual-platform suites" -f $results.Count) -ForegroundColor Green
    exit 0
}
Write-SuiteEvent @{event='run-end';suite='dualsmoke';pass=$false;total=$results.Count;failed=$fail}
Write-Host ("{0} of {1} dual-platform suites FAILED" -f $fail, $results.Count) -ForegroundColor Red
exit 1
