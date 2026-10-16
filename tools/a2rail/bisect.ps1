<# P2.4: scratch-only local-history witness search. See bisect.example.json. #>
[CmdletBinding()]
param(
    [string]$Repo = (Join-Path $PSScriptRoot '..\..'),
    [string]$Before,
    [string]$After,
    [string]$Scenario,
    [string]$Scratch,
    [ValidateSet('change','none')][string]$Expect,
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'runtime.ps1')
$null = Initialize-A2RailRuntime
$arguments = @('-I', (Join-Path $PSScriptRoot 'revisionbisect.py'))
if ($SelfTest) { $arguments += '--selftest' }
else {
    if (-not ($Before -and $After -and $Scenario -and $Scratch)) {
        throw 'Before, After, Scenario and Scratch are required; Scratch must be new and outside Repo.'
    }
    $arguments += @('--repo', $Repo, '--before', $Before, '--after', $After,
                    '--scenario', $Scenario, '--scratch', $Scratch)
    if ($Expect) { $arguments += @('--expect', $Expect) }
}
& python @arguments
exit $LASTEXITCODE
