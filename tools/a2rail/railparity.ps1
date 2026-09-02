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

# --- the record contract (Docs/INSTRUMENTATION_SHAPE.md, M1) ---------------
# A rail's machine-readable output goes through src/a2record.hpp, so every
# record has one shape and is written whole. A JSON literal printed anywhere
# else is a record outside the contract. Exemptions are named, with a reason.
$recordExempt = @{
    'a2record.hpp'         = 'the writer itself'
    'a2gspu_ctrl_json.hpp' = 'the CTRL reply envelope is protocol, not a rail record'
    'wiz5_config.hpp'      = 'title-specific streams (WIZ5_EC19_OUT, WIZ5_CSP6_OUT) predate the contract; they convert when their readers do'
}
$outside = @()
Get-ChildItem -Path $srcDir -Recurse -Include *.cpp, *.hpp, *.h, *.cc |
    Where-Object { $_.FullName -notmatch '\\imgui\\' -and -not $recordExempt.ContainsKey($_.Name) } |
    ForEach-Object {
        $text = [IO.File]::ReadAllText($_.FullName)
        foreach ($m in [regex]::Matches($text, '(?<![A-Za-z_])f?printf\s*\((?:[^;"]*?,)?\s*"\{\\"')) {
            $line = ($text.Substring(0, $m.Index) -split "`n").Count
            $outside += ('  {0}:{1}' -f $_.FullName.Substring($srcDir.Length + 1), $line)
        }
    }
if ($outside.Count) {
    Write-Host ''
    Write-Host 'OUTSIDE THE RECORD CONTRACT - JSON printed by a rail without a2record.hpp:' -ForegroundColor Red
    $outside | ForEach-Object { Write-Host $_ }
    Write-Host '  (write it with a2record::Record, or name the exemption and its reason in railparity.ps1)'
    Write-Host ''
    Write-Host ("railparity: FAIL - {0} record(s) outside the contract" -f $outside.Count) -ForegroundColor Red
    exit 1
}

# --- readback registration (Docs/INSTRUMENTATION_SHAPE.md, M8) -------------
# A reach-table row that says "fn" is a promise the rail keeps by registering
# its reader (a2rails::Registrar) in its own header; a registration without
# the row is a reader nobody can find. Both directions, offline, so the gate
# needs no running process to catch either.
$fnRows  = [System.Collections.Generic.HashSet[string]]::new()
$inReach = $false
foreach ($line in [IO.File]::ReadAllLines($registry)) {
    if ($line -match 'static const Reach r\[\]') { $inReach = $true; continue }
    if ($inReach -and $line -match '^\s*\};') { break }
    if ($inReach -and $line -match '^\s*\{"([A-Z0-9_]+)",\s*"[^"]*",\s*"fn"\}') { [void]$fnRows.Add($matches[1]) }
}
$registered = @{}
Get-ChildItem -Path $srcDir -Recurse -Include *.cpp, *.hpp, *.h, *.cc |
    Where-Object { $_.FullName -notmatch '\\imgui\\' } |
    ForEach-Object {
        $file = $_
        foreach ($m in [regex]::Matches([IO.File]::ReadAllText($file.FullName), 'Registrar\s+\w+\s*\{\s*"([A-Z0-9_]+)"')) {
            $registered[$m.Groups[1].Value] = $file.FullName.Substring($srcDir.Length + 1)
        }
    }
$unkept  = @($fnRows | Where-Object { -not $registered.ContainsKey($_) } | Sort-Object)
$unfound = @($registered.Keys | Where-Object { -not $fnRows.Contains($_) } | Sort-Object)
if ($unkept.Count -or $unfound.Count) {
    Write-Host ''
    if ($unkept.Count) {
        Write-Host 'UNKEPT - the reach table says fn, but no a2rails::Registrar registers a reader:' -ForegroundColor Red
        $unkept | ForEach-Object { Write-Host "  $_" }
    }
    if ($unfound.Count) {
        Write-Host 'UNFINDABLE - a reader is registered, but the reach table does not say fn:' -ForegroundColor Red
        $unfound | ForEach-Object { Write-Host ("  {0,-24} {1}" -f $_, $registered[$_]) }
    }
    Write-Host ''
    Write-Host ("railparity: FAIL - {0} unkept, {1} unfindable reader(s)" -f $unkept.Count, $unfound.Count) -ForegroundColor Red
    exit 1
}

Write-Host ("railparity: PASS - every rail the code reads is discoverable, every declared rail")
Write-Host ("                   exists, every record is written by the contract, and all {0}" -f $fnRows.Count)
Write-Host ("                   declared readers are registered by their rails.")
exit 0
