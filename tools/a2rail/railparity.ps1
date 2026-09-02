<#
    railparity.ps1 -- is every rail the code reads actually discoverable?

    WHY THIS EXISTS

    AGENTS.md rule 6 says new verbs *and rails* must be discoverable via
    `help` / `manifest`. For verbs that was true and enforced by their being in
    a table the process itself prints. For rails it was true only by habit, and
    habit lost: a scan found the code reading 128 env rails while
    Docs/A2GSPU_RAILS.md described 96.

    The 32 missing ones were not obscure. They included MEMVU_LOADVIS and
    MEMVU_BLAME, the whole A2GSPU_TAP_* family (8 rails), A2GSPU_COVERAGE --
    which feeds the deasmiigs code/data split -- and every WIZ5_* rail. An agent
    discovering this fork cold could enumerate all 124 verbs from the running
    process and had no way to learn those 32 existed short of reading C++.

    That is the black box the oracle exists to remove, sitting inside the
    oracle. It happened silently because nothing checked, so this checks.

    THE RULE: src/a2gspu_rails_registry.hpp is the source of truth. Every
    getenv() in src/ must have an entry, and every entry must name a rail some
    code reads. Both directions matter -- an entry for a deleted rail is a
    documented capability that does not exist, which is worse than an
    undocumented one, because it will be believed.

    Run after any rail change, alongside dualsmoke.ps1.
#>

[CmdletBinding()]
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

$srcDir   = Join-Path $Root 'src'
$registry = Join-Path $srcDir 'a2gspu_rails_registry.hpp'

if (-not (Test-Path $registry)) {
    Write-Host "railparity: FAIL - registry not found at $registry" -ForegroundColor Red
    exit 1
}

# Environment names the process reads that are NOT instrumentation rails.
$notRails = @('HOME')

# --- what the code reads -------------------------------------------------
$codeRails = [System.Collections.Generic.HashSet[string]]::new()
$owner     = @{}

Get-ChildItem -Path $srcDir -Recurse -Include *.cpp, *.hpp, *.h, *.cc |
    Where-Object { $_.FullName -notmatch '\\imgui\\' } |
    ForEach-Object {
        $file = $_
        foreach ($m in [regex]::Matches(
                    [IO.File]::ReadAllText($file.FullName),
                    '(?:SDL_)?getenv\s*\(\s*"([A-Z0-9_]+)"')) {
            $name = $m.Groups[1].Value
            if ($notRails -contains $name) { continue }
            [void]$codeRails.Add($name)
            if (-not $owner.ContainsKey($name)) {
                $owner[$name] = $file.FullName.Substring($srcDir.Length + 1)
            }
        }
    }

# --- what the registry declares ------------------------------------------
$regRails = [System.Collections.Generic.HashSet[string]]::new()
$inTable  = $false
foreach ($line in [IO.File]::ReadAllLines($registry)) {
    if ($line -match 'static const Rail r\[\]') { $inTable = $true; continue }
    if ($inTable -and $line -match '^\s*\};') { break }
    if ($inTable -and $line -match '^\s*\{"([A-Z0-9_]+)"') {
        [void]$regRails.Add($matches[1])
    }
}

# --- the two directions ---------------------------------------------------
$undocumented = @($codeRails | Where-Object { -not $regRails.Contains($_) } | Sort-Object)
$phantom      = @($regRails  | Where-Object { -not $codeRails.Contains($_) } | Sort-Object)

Write-Host ''
Write-Host ("railparity: {0} rail(s) read by src/, {1} declared in the registry" -f `
            $codeRails.Count, $regRails.Count)

if ($undocumented.Count) {
    Write-Host ''
    Write-Host "UNDISCOVERABLE - read by the code, absent from the registry:" -ForegroundColor Red
    foreach ($n in $undocumented) {
        Write-Host ("  {0,-28} {1}" -f $n, $owner[$n])
    }
}

if ($phantom.Count) {
    Write-Host ''
    Write-Host "PHANTOM - declared in the registry, read by no code:" -ForegroundColor Red
    foreach ($n in $phantom) { Write-Host "  $n" }
    Write-Host '  (a documented capability that does not exist will be believed)'
}

if ($undocumented.Count -or $phantom.Count) {
    Write-Host ''
    Write-Host ("railparity: FAIL - {0} undiscoverable, {1} phantom" -f `
                $undocumented.Count, $phantom.Count) -ForegroundColor Red
    Write-Host 'Fix: add or remove the entry in src/a2gspu_rails_registry.hpp.'
    exit 1
}

Write-Host 'railparity: PASS - every rail the code reads is discoverable, and'
Write-Host '                   every declared rail exists.'
exit 0
