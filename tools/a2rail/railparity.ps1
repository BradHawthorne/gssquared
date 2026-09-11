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

    WHAT THIS DID NOT CHECK, AND NOW DOES.

    Two surfaces were still held together by habit alone, and habit lost twice
    more while every gate stayed green:

    * The DOC. The registry fixed the set of rail NAMES; it does not carry their
      semantics -- the invariants, the worked combinations, the gotchas that a
      one-line synopsis cannot hold. Those live in Docs/A2GSPU_RAILS.md, which
      AGENTS.md hands a new agent and which `manifest <file>` points at. At the
      142-rail mark that file had no mention of 24 of them, including every
      WIZ5_* rail and A2GSPU_COVERAGE itself -- the rail the header above names
      as the reason this script exists. So: every declared rail's name must
      appear in the doc. Name-presence only, deliberately; demanding a table row
      per rail would be noisy and would not have caught anything this does not.

    * The VERB DISPATCHER. The header above says verb discoverability "was true
      and enforced by their being in a table the process itself prints". It was
      true only of verbs someone remembered to add to the table. `pause` and
      `pause-status` were dispatched in the CTRL loop and in NEITHER `help` nor
      `manifest`, while `resume` -- dispatched on the same line of gs2.cpp -- had
      a row. The cost was not just a hidden verb: an unrecognised word is
      answered "not a verb. `help` lists all of them", so the process stated a
      falsehood about itself. So: every command word the dispatcher matches must
      have a verbs[] row, and every row must be dispatched. Both directions, the
      same shape as the readback check.

    Both are offline: no running process, no build.

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

# --- the doc carries the semantics (Docs/A2GSPU_RAILS.md) ------------------
# The registry answers WHICH rails exist; the doc answers what they mean. A
# rail absent from the doc is discoverable by name and a black box by
# behaviour -- the same black box the registry removed, moved one layer out.
# Name presence is the whole test: cheap, and exactly the check that would have
# caught all 24 of the misses. A rail deliberately left out of the doc must be
# named here with its reason, the way the record contract's exemptions are.
$docFile   = Join-Path $Root 'Docs\A2GSPU_RAILS.md'
$docExempt = @{}          # rail -> reason. Empty on purpose: every rail is documented.
if (-not (Test-Path $docFile)) {
    Write-Host "railparity: FAIL - rail catalogue not found at $docFile" -ForegroundColor Red
    exit 1
}
$docText = [IO.File]::ReadAllText($docFile)
$undocInDoc = @($regRails | Where-Object {
        -not $docExempt.ContainsKey($_) -and
        $docText -notmatch ('(?<![A-Za-z0-9_])' + [regex]::Escape($_) + '(?![A-Za-z0-9_])')
    } | Sort-Object)
if ($undocInDoc.Count) {
    Write-Host ''
    Write-Host 'UNDESCRIBED - declared in the registry, named nowhere in Docs/A2GSPU_RAILS.md:' -ForegroundColor Red
    foreach ($n in $undocInDoc) { Write-Host "  $n" }
    Write-Host '  (write the rail into the catalogue with its FULL name -- a short form such as'
    Write-Host '   `TAP_PCS` for A2GSPU_TAP_PCS is invisible to a reader grepping for what to set --'
    Write-Host '   or name it and its reason in $docExempt in railparity.ps1)'
    Write-Host ''
    Write-Host ("railparity: FAIL - {0} rail(s) the catalogue does not describe" -f $undocInDoc.Count) -ForegroundColor Red
    exit 1
}

# --- the CTRL dispatcher vs the verb registry (AGENTS.md rule 6) -----------
# verbs[] in a2gspu_manifest.hpp is what `help`, `help <file>` and
# `manifest <file>` all print -- all three of the discovery surfaces rule 6
# names. The dispatcher is what actually answers. A word in one and not the
# other is either a verb nobody can find or a catalogue entry nobody can call,
# and the process says "`help` lists all of them" either way.
#
# A dispatcher literal counts as a command word only when the comparison is
# against `line` (the whole command). Comparisons against `arg`, `sub` and the
# like are sub-commands (`on`, `off`, `status`, `bus`) and are not verbs; the
# first word of the literal is taken so `strncmp(line,"read ",5)` reads as
# `read`.
$manifestFile = Join-Path $srcDir 'a2gspu_manifest.hpp'
$verbTable = [System.Collections.Generic.HashSet[string]]::new()
$inVerbs   = $false
foreach ($line in [IO.File]::ReadAllLines($manifestFile)) {
    if ($line -match 'static const Verb v\[\]') { $inVerbs = $true; continue }
    if ($inVerbs -and $line -match '^\s*\};') { break }
    if ($inVerbs -and $line -match '^\s*\{"([a-z0-9-]+)"') { [void]$verbTable.Add($matches[1]) }
}
if ($verbTable.Count -lt 2) {
    Write-Host "railparity: FAIL - could not read verbs[] from $manifestFile" -ForegroundColor Red
    exit 1
}
$dispatched = [System.Collections.Generic.HashSet[string]]::new()
foreach ($f in @('gs2.cpp', 'a2gspu_ctrl_cmds.hpp')) {
    $p = Join-Path $srcDir $f
    if (-not (Test-Path $p)) {
        Write-Host "railparity: FAIL - CTRL dispatcher source not found at $p" -ForegroundColor Red
        exit 1
    }
    foreach ($m in [regex]::Matches([IO.File]::ReadAllText($p), 'strn?cmp\s*\(\s*line\s*,\s*"([^"]+)"')) {
        $w = ($m.Groups[1].Value -split '\s+')[0]
        if ($w -match '^[a-z][a-z0-9-]*$') { [void]$dispatched.Add($w) }
    }
}
$hidden  = @($dispatched | Where-Object { -not $verbTable.Contains($_) } | Sort-Object)
$unreach = @($verbTable  | Where-Object { -not $dispatched.Contains($_) } | Sort-Object)
if ($hidden.Count -or $unreach.Count) {
    Write-Host ''
    if ($hidden.Count) {
        Write-Host 'UNDISCOVERABLE VERB - the CTRL loop answers it, verbs[] does not list it:' -ForegroundColor Red
        $hidden | ForEach-Object { Write-Host "  $_" }
        Write-Host '  (help/manifest omit it, and `unknown-cmd` tells callers `help` lists all of them)'
    }
    if ($unreach.Count) {
        Write-Host 'UNCALLABLE VERB - verbs[] lists it, no dispatcher literal matches it:' -ForegroundColor Red
        $unreach | ForEach-Object { Write-Host "  $_" }
        Write-Host '  (a declared capability that does not exist will be believed)'
    }
    Write-Host ''
    Write-Host ("railparity: FAIL - {0} undiscoverable, {1} uncallable verb(s)" -f `
                $hidden.Count, $unreach.Count) -ForegroundColor Red
    Write-Host 'Fix: add or remove the row in src/a2gspu_manifest.hpp verbs[].'
    exit 1
}

Write-Host ("railparity: PASS - every rail the code reads is discoverable, every declared rail")
Write-Host ("                   exists and is described in Docs/A2GSPU_RAILS.md, every record is")
Write-Host ("                   written by the contract, all {0} declared readers are registered by" -f $fnRows.Count)
Write-Host ("                   their rails, and all {0} CTRL verbs are both dispatched and listed." -f $verbTable.Count)
exit 0
