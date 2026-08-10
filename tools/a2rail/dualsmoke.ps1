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
    [string]$Dir = $(if ($env:A2RAIL_DIR) { $env:A2RAIL_DIR } else { Join-Path $env:TEMP 'a2rail' })
)
$ErrorActionPreference = "Continue"
$here = $PSScriptRoot
$results = @()

function Run-One([string]$name, [scriptblock]$body) {
    Write-Host ""
    Write-Host ("##### {0} " -f $name).PadRight(64, '#') -ForegroundColor Cyan
    & $body
    $ok = ($LASTEXITCODE -eq 0)
    $script:results += [pscustomobject]@{ Suite = $name; Pass = $ok }
    if (-not $ok) {
        Write-Host ("##### FAILED: {0}" -f $name) -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "=== DUALSMOKE (IIe + IIgs instrument gate) ".PadRight(64, '=') -ForegroundColor Cyan
Write-Host "  FOUNDATION_MILESTONES M6 -- shared rail changes need both platforms" -ForegroundColor DarkGray

. (Join-Path $here "toolpin.ps1")
try { $null = Write-ToolPin -Label "GSSquared" -Exe (Get-GSSquaredExe) -RepoHint "gssquared" } catch {}

if (-not $SkipOffline) {
    $gsBuild = Join-Path (Get-GssquaredRoot) "build"
    Run-One "gssquared ctest (offline)" {
        Push-Location $gsBuild
        $env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
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
    Write-Host ("ALL GREEN -- {0} dual-platform suites" -f $results.Count) -ForegroundColor Green
    exit 0
}
Write-Host ("{0} of {1} dual-platform suites FAILED" -f $fail, $results.Count) -ForegroundColor Red
exit 1
