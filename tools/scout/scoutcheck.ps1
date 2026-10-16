[CmdletBinding()]
param(
    [string]$Exe = $env:A2_GSSQUARED,
    [string]$Image = '',
    [string]$Dir = (Join-Path ([IO.Path]::GetTempPath()) ('scout-check-' + [guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$here=$PSScriptRoot
$root=Split-Path (Split-Path $here -Parent) -Parent
New-Item -ItemType Directory -Path $Dir | Out-Null
if (-not $Image) {
    # Synthetic raw block volume: tests staging/ownership and bounded ROM execution,
    # never GS/OS boot acceptance. No private corpus dependency for the gate.
    $Image=Join-Path $Dir 'input-fixture.po'
    [IO.File]::WriteAllBytes($Image, [byte[]]::new(8192))
}
$symbols=Join-Path $Dir 'fixture.symbols'
$milestones=Join-Path $Dir 'fixture.milestones'
Set-Content -LiteralPath $symbols -Value '00FA62 reset_fixture'
Set-Content -LiteralPath $milestones -Value 'FFFFFF unreachable_fixture'
$before=(Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash
$savedSeed=$env:A2GSPU_SEED
$env:A2GSPU_SEED='999'
$checks=0
function Require([bool]$value,[string]$label) {
    if (-not $value) { throw $label }
    $script:checks++
    Write-Host ('PASS '+$label)
}
try {
    $roots=@()
    foreach ($attempt in 1,2) {
        $answer=& (Join-Path $here 'scout.ps1') -Gss $Exe -Img $Image -Symbols $symbols -Milestones $milestones `
            -OutRoot (Join-Path $Dir 'runs') -Frames 2 -Repeat 1 -TimeoutSec 30
        Require ($LASTEXITCODE -eq 3) 'synthetic unreachable milestones report no observed advance'
        Require ($env:A2GSPU_SEED -eq '999') 'caller instrumentation environment is restored'
        $line=@($answer | Where-Object { $_ -match '^SCOUT OUTPUT:' })[0]
        $output=$line.Substring('SCOUT OUTPUT: '.Length)
        $roots+=$output
        $receipt=Get-Content -LiteralPath (Join-Path $output 'scout-report.json') -Raw | ConvertFrom-Json
        Require ($receipt.schema -eq 'a2scout-run-v1' -and $receipt.outcome -eq 'no-observed-advance') 'closed receipt declares its outcome/model'
        Require ($receipt.runs.Count -eq 1 -and $receipt.runs[0].valid_capture) 'bounded baseline retains one native pins/diagnostic pair'
        $run=$receipt.runs[0]
        Require ($run.directory -eq (Join-Path $output 'scout_base')) 'child owns a separate working/output directory'
        Require ($run.requested_rails.A2GSPU_SEED -eq '1' -and $run.requested_rails.A2GSPU_RAMDISK -eq '1') 'child pins are independent from inherited rails'
        Require ((Test-Path -LiteralPath (Join-Path $run.directory 'input.po')) -and (Test-Path -LiteralPath $run.stdout)) 'staged media and producer logs are retained'
        $snapshot=(Get-FileHash -LiteralPath (Join-Path $output 'scout-report.json') -Algorithm SHA256).Hash
        $refused=$false
        try {
            & (Join-Path $here 'scout.ps1') -Gss $Exe -Img $Image -Symbols $symbols -Milestones $milestones -OutDir $output -Frames 2 | Out-Null
        } catch { $refused=$true }
        Require ($refused -and (Get-FileHash -LiteralPath (Join-Path $output 'scout-report.json') -Algorithm SHA256).Hash -eq $snapshot) 'existing output refuses without replacing evidence'
    }
    Require ($roots[0] -ne $roots[1]) 'repeated invocations never share a run root'
    Require ((Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash -eq $before) 'source image remains byte-identical'
    $provider=Join-Path $Dir 'fixture-provider.ps1'
    @'
$env:MEMVU_FIXTURE_POLLUTION='unexpected'
function Set-OracleEnv {
  param($Mode,$Frames,$Ndev,$Allow,[switch]$Quiet)
  $env:A2GSPU_SEED='1'; $env:A2GSPU_FAKETIME='1000000000'; $env:A2GSPU_BRAM='default'
  $env:A2GSPU_SPIKE_FRAMES="$Frames"; $env:A2GSPU_SP_NDEV="$Ndev"
}
'@ | Set-Content -LiteralPath $provider
    & (Join-Path $here 'scout.ps1') -Gss $Exe -Img $Image -Symbols $symbols -Milestones $milestones `
        -Manifest $provider -OutDir (Join-Path $Dir 'provider-run') -Frames 2 -TimeoutSec 30 | Out-Null
    Require ($LASTEXITCODE -eq 3 -and $env:A2GSPU_SEED -eq '999' -and -not $env:MEMVU_FIXTURE_POLLUTION) 'explicit provider setup cannot leak instrumentation environment to caller'
    $refused=$false
    try {
        & (Join-Path $here 'scout.ps1') -Gss $Exe -Img $Image -Symbols (Join-Path $Dir 'missing') -Milestones $milestones -OutDir (Join-Path $Dir 'invalid') | Out-Null
    } catch { $refused=$true }
    Require ($refused -and -not (Test-Path -LiteralPath (Join-Path $Dir 'invalid'))) 'missing declared input refuses before output or execution'
} finally {
    $env:A2GSPU_SEED=$savedSeed
}
Write-Host "ALL GREEN -- isolated scout checks=$checks evidence=$Dir"
exit 0
