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
    discovering this fork cold could enumerate every verb from the running
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
      appear in the doc. That first version was name-presence only, and it let
      MEMVU_LOADVIS pass with no row of its own and 32 rails sit under a Group
      the registry's family contradicted. So the check is now: a table row whose
      first cell names the rail, whose Group cell leads with the registry
      family, and a registry synopsis that is not a fragment. The doc check is
      one direction (registry -> doc): a rail named in the doc but absent from
      the registry is caught by the code<->registry check above only if some
      code reads it.

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

    AND THE CHECK THAT ONLY LOOKED LIKE ONE.

    The record-contract check below asserted, in its own PASS line, that "every
    record is written by the contract". It was a single regex over one spelling
    of one call -- an `fprintf` whose format string literally opens `{"`. Seven
    ways of emitting a record round it were tried and five went straight
    through: fputs, a `%s` indirection, snprintf-then-fputs, a record with a
    leading newline, and -- the one the contract's own rules paragraph forbids
    by name -- a record printed in PIECES, a header call plus a per-byte loop
    plus a closer. src/wiz5_config.hpp writes all three of its streams exactly
    that way. So the green was not evidence; the file-wide exemption was the
    only thing telling the truth. It is now checked on the SINK as well as on
    the text, and the PASS prints its own carve-outs so the green can be read.

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
                    '(?:(?:SDL_)?getenv|a2gspu_rail_arm_env)\s*\(\s*"([A-Z0-9_]+)"')) {
            # a2gspu_rail_arm_env("NAME") is a launch-time read of NAME through the rail's
            # registered apply function (a2gspu_rail_setters.cpp), the same as a getenv here.
            $name = $m.Groups[1].Value
            if ($notRails -contains $name) { continue }
            [void]$codeRails.Add($name)
            if (-not $owner.ContainsKey($name)) {
                $owner[$name] = $file.FullName.Substring($srcDir.Length + 1)
            }
        }
    }

# --- what the registry declares ------------------------------------------
$regRails  = [System.Collections.Generic.HashSet[string]]::new()
$regFamily = @{}          # rail -> family, as `rail list <family>` groups it
$regSynop  = @{}          # rail -> the one-line synopsis `rail find` serves
$inTable   = $false
foreach ($line in [IO.File]::ReadAllLines($registry)) {
    if ($line -match 'static const Rail r\[\]') { $inTable = $true; continue }
    if ($inTable -and $line -match '^\s*\};') { break }
    if ($inTable -and $line -match '^\s*\{"([A-Z0-9_]+)"') {
        [void]$regRails.Add($matches[1])
        if ($line -match '^\s*\{"([A-Z0-9_]+)",\s*"([^"]*)",\s*"((?:[^"\\]|\\.)*)"') {
            $regFamily[$matches[1]] = $matches[2]
            $regSynop[$matches[1]]  = $matches[3]
        }
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
    'wiz5_config.hpp'      = 'title-specific streams (WIZ5_EC19_OUT, WIZ5_DISKWR_OUT, WIZ5_CSP6_OUT) predate the contract, and all three build their records piecemeal; they convert when their readers do'
}

# TWO SCANS, because each is blind where the other sees.
#
# (a) The LITERAL scan: a printf/fputs/fwrite whose format string opens a JSON
#     object. Cheap and file-agnostic, and it catches the obvious shape.
#
#     It catches ONLY that shape, and that is why (b) exists. It matches one
#     spelling of one call: it does not see fputs, it does not see a `%s`
#     indirection, and above all it does not see a record printed in PIECES --
#     a header fprintf, a per-byte loop, a closing fprintf -- because no single
#     call in that sequence has a format string that opens the object. "Never
#     printed in pieces" is the contract's own rule, and until (b) nothing
#     enforced it. src/wiz5_config.hpp writes all three of its streams in
#     exactly that shape and was invisible here for as long as this check has
#     existed; only its file-wide exemption above is honest about it.
#
# (b) The SINK scan: any stdio write aimed at a rail's own FILE* from a file
#     that is not the writer. Keyed on the SINK and not on the text, because
#     text is what (a) already does badly -- and because widening (a) to any
#     literal opening `{"` would flag legitimate NESTED fragments, such as the
#     `{"at":...}` object a2gspu_trap.hpp snprintf's into a std::string that
#     Record::write then emits whole. Those touch no sink and stay silent.
#
# The sink table is the rail record sinks only. g_iigs_itrace_out is
# deliberately absent: ITRACE emits a human-readable trace line, not a record,
# so the contract makes no claim about it. A new rail that opens its own FILE*
# belongs in this table -- a sink missing from it is unchecked, which is the
# failure this whole script was written about.
$railSinks = @(
    'g_gtap\.out', 'g_gtap\.zp_f', 'g_gtap\.pages_f', 'g_gtap\.keys_f', 'g_gtap\.disk_f',
    'g_watch_out', 'g_callstream_out', 'g_snap_out', 'g_liveness_out', 'g_default_out',
    'g_wiz5_diskwr_out', 'g_wiz5_ec19_out', 'g_wiz5_csp6_out'
)
$sink = '(?:' + ($railSinks -join '|') + ')(?![A-Za-z0-9_])'
$contractScans = @(
    @{ why = 'JSON literal printed outside a2record.hpp';
       rx  = '(?<![A-Za-z_])f?printf\s*\((?:[^;"]*?,)?\s*"\{\\"' }
    @{ why = 'JSON literal printed outside a2record.hpp';
       rx  = '(?<![A-Za-z_])fputs\s*\(\s*"\{\\"' }
    @{ why = 'JSON literal printed outside a2record.hpp';
       rx  = '(?<![A-Za-z_])fwrite\s*\(\s*"\{\\"' }
    @{ why = 'stdio write straight to a rail sink (a record built in pieces)';
       rx  = '(?<![A-Za-z_])fprintf\s*\(\s*' + $sink }
    @{ why = 'stdio write straight to a rail sink (a record built in pieces)';
       rx  = '(?<![A-Za-z_])fputs\s*\([^;]*,\s*' + $sink + '\s*\)' }
    @{ why = 'stdio write straight to a rail sink (a record built in pieces)';
       rx  = '(?<![A-Za-z_])fwrite\s*\([^;]*,\s*' + $sink + '\s*\)' }
)
$outside = @()
Get-ChildItem -Path $srcDir -Recurse -Include *.cpp, *.hpp, *.h, *.cc |
    Where-Object { $_.FullName -notmatch '\\imgui\\' -and -not $recordExempt.ContainsKey($_.Name) } |
    ForEach-Object {
        $text = [IO.File]::ReadAllText($_.FullName)
        $rel  = $_.FullName.Substring($srcDir.Length + 1)
        foreach ($scan in $contractScans) {
            foreach ($m in [regex]::Matches($text, $scan.rx)) {
                $line = ($text.Substring(0, $m.Index) -split "`n").Count
                $outside += ('  {0}:{1}  -- {2}' -f $rel, $line, $scan.why)
            }
        }
    }
$outside = @($outside | Sort-Object -Unique)
if ($outside.Count) {
    Write-Host ''
    Write-Host 'OUTSIDE THE RECORD CONTRACT - a rail record not written by a2record.hpp:' -ForegroundColor Red
    $outside | ForEach-Object { Write-Host $_ }
    Write-Host '  (write it with a2record::Record -- one call, whole record -- or name the'
    Write-Host '   exemption and its reason in $recordExempt in railparity.ps1)'
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
        foreach ($m in [regex]::Matches([IO.File]::ReadAllText($file.FullName), '(?<!Setter)Registrar\s+\w+\s*\{\s*"([A-Z0-9_]+)"')) {
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

# --- live arming registration ----------------------------------------------
# A reach-table row whose verb is "rail" promises `rail set NAME VALUE` works:
# the rail must register its apply function (a2rails::SetterRegistrar). A
# registration without the row is a live arm nobody can discover. Both
# directions, offline -- the same contract as readers above.
$railRows = [System.Collections.Generic.HashSet[string]]::new()
$inReach = $false
foreach ($line in [IO.File]::ReadAllLines($registry)) {
    if ($line -match 'static const Reach r\[\]') { $inReach = $true; continue }
    if ($inReach -and $line -match '^\s*\};') { break }
    if ($inReach -and $line -match '^\s*\{"([A-Z0-9_]+)",\s*"rail",') { [void]$railRows.Add($matches[1]) }
}
$setters = @{}
Get-ChildItem -Path $srcDir -Recurse -Include *.cpp, *.hpp, *.h, *.cc |
    Where-Object { $_.FullName -notmatch '\\imgui\\' } |
    ForEach-Object {
        $file = $_
        foreach ($m in [regex]::Matches([IO.File]::ReadAllText($file.FullName), 'SetterRegistrar\s+\w+\s*\{\s*"([A-Z0-9_]+)"')) {
            $setters[$m.Groups[1].Value] = $file.FullName.Substring($srcDir.Length + 1)
        }
    }
$unarmable  = @($railRows | Where-Object { -not $setters.ContainsKey($_) } | Sort-Object)
$undeclared = @($setters.Keys | Where-Object { -not $railRows.Contains($_) } | Sort-Object)
if ($unarmable.Count -or $undeclared.Count) {
    Write-Host ''
    if ($unarmable.Count) {
        Write-Host 'UNARMABLE - the reach table says `rail set`, but no a2rails::SetterRegistrar registers an apply function:' -ForegroundColor Red
        $unarmable | ForEach-Object { Write-Host "  $_" }
    }
    if ($undeclared.Count) {
        Write-Host 'UNDECLARED - an apply function is registered, but the reach table does not say verb rail:' -ForegroundColor Red
        $undeclared | ForEach-Object { Write-Host ("  {0,-24} {1}" -f $_, $setters[$_]) }
    }
    Write-Host ''
    Write-Host ("railparity: FAIL - {0} unarmable, {1} undeclared setter(s)" -f $unarmable.Count, $undeclared.Count) -ForegroundColor Red
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

# --- a ROW of its own, filed under the registry's family ---------------------
# Name presence was the first test and it was not enough twice over. MEMVU_LOADVIS
# passed it with no row at all -- named in two prose sentences, so a reader
# scanning the tables for what to set never met it. And the registry's family
# (what `rail list <family>` groups by) and the catalogue's Group column had
# drifted apart on 32 rails: `rail list trace` did not return CALLTRACE, which
# the catalogue filed under trace. Two surfaces answering one question
# differently is the drift this script exists to stop, so now: every declared
# rail must be named in the FIRST cell of a table row, and that row's Group cell
# must name the registry family (a combined group such as `break/snapshot`
# must lead with it).
$rowGroup = @{}
foreach ($dl in [IO.File]::ReadAllLines($docFile)) {
    if (-not $dl.StartsWith('|')) { continue }
    $cells = [regex]::Split($dl, '(?<!\\)\|')
    if ($cells.Count -lt 5) { continue }             # '' + >=3 cells + ''
    $first = $cells[1]
    $group = $cells[3].Trim()
    foreach ($m in [regex]::Matches($first, '`((?:A2GSPU|MEMVU|WIZ5)_[A-Z0-9_]+)')) {
        if (-not $rowGroup.ContainsKey($m.Groups[1].Value)) { $rowGroup[$m.Groups[1].Value] = $group }
    }
}
$noRow = @(); $wrongGroup = @()
foreach ($r in ($regRails | Sort-Object)) {
    if ($docExempt.ContainsKey($r)) { continue }
    if (-not $rowGroup.ContainsKey($r)) { $noRow += $r; continue }
    $lead = ($rowGroup[$r] -split '[/ ]')[0]
    if ($regFamily.ContainsKey($r) -and $lead -ne $regFamily[$r]) {
        $wrongGroup += ("{0,-30} registry={1,-12} catalogue={2}" -f $r, $regFamily[$r], $rowGroup[$r])
    }
}
# A synopsis is what `rail find` and `manifest` print -- the surface this fork
# tells agents to trust over markdown. Five of them were fragments cut at a `|`
# when copied from a table ("2`", "strict`"), and nothing noticed.
$badSynop = @()
foreach ($r in ($regRails | Sort-Object)) {
    $syn = $regSynop[$r]
    if ($null -eq $syn) { continue }
    $ticks = ([regex]::Matches($syn, '`')).Count
    if ($syn.Length -lt 20 -or ($ticks % 2) -ne 0) { $badSynop += ("{0,-30} '{1}'" -f $r, $syn) }
}
if ($noRow.Count -or $wrongGroup.Count -or $badSynop.Count) {
    Write-Host ''
    if ($noRow.Count) {
        Write-Host 'ROWLESS - named in Docs/A2GSPU_RAILS.md, but in no table row of its own:' -ForegroundColor Red
        $noRow | ForEach-Object { Write-Host "  $_" }
    }
    if ($wrongGroup.Count) {
        Write-Host 'MISFILED - the catalogue Group and the registry family disagree:' -ForegroundColor Red
        $wrongGroup | ForEach-Object { Write-Host "  $_" }
    }
    if ($badSynop.Count) {
        Write-Host 'GARBLED - a registry synopsis that is a fragment (under 20 chars or unbalanced backticks):' -ForegroundColor Red
        $badSynop | ForEach-Object { Write-Host "  $_" }
    }
    Write-Host ''
    Write-Host ("railparity: FAIL - {0} rowless, {1} misfiled, {2} garbled" -f $noRow.Count, $wrongGroup.Count, $badSynop.Count) -ForegroundColor Red
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
#
# WHERE it looks is declared by the source, not by this script: only between
# `// A2CTRL-DISPATCH-BEGIN` and `// A2CTRL-DISPATCH-END`, in any file under src/.
# It used to scan two named files, which meant moving the dispatcher out of
# gs2.cpp would have silently emptied the verb set -- and it also counted the
# co-pilot allow-list's literals as CTRL verbs. Unbalanced markers, or none at
# all, fail the gate rather than scanning nothing.
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
$regions = 0
foreach ($file in (Get-ChildItem -Path $srcDir -Recurse -Include *.cpp, *.hpp | Where-Object { $_.FullName -notmatch '[\\/]imgui[\\/]' })) {
    $inRegion = $false
    $n = 0
    foreach ($ln in [IO.File]::ReadAllLines($file.FullName)) {
        $n++
        if ($ln -match '//\s*A2CTRL-DISPATCH-BEGIN') {
            if ($inRegion) { Write-Host "railparity: FAIL - nested A2CTRL-DISPATCH-BEGIN at $($file.Name):$n" -ForegroundColor Red; exit 1 }
            $inRegion = $true; $regions++; continue
        }
        if ($ln -match '//\s*A2CTRL-DISPATCH-END') {
            if (-not $inRegion) { Write-Host "railparity: FAIL - A2CTRL-DISPATCH-END without BEGIN at $($file.Name):$n" -ForegroundColor Red; exit 1 }
            $inRegion = $false; continue
        }
        if (-not $inRegion) { continue }
        foreach ($m in [regex]::Matches($ln, 'strn?cmp\s*\(\s*line\s*,\s*"([^"]+)"')) {
            $w = ($m.Groups[1].Value -split '\s+')[0]
            if ($w -match '^[a-z][a-z0-9-]*$') { [void]$dispatched.Add($w) }
        }
    }
    if ($inRegion) { Write-Host "railparity: FAIL - A2CTRL-DISPATCH-BEGIN never closed in $($file.Name)" -ForegroundColor Red; exit 1 }
}
if ($regions -eq 0 -or $dispatched.Count -lt 2) {
    Write-Host "railparity: FAIL - no A2CTRL-DISPATCH regions found under src/ ($regions regions, $($dispatched.Count) words) -- the verb check would check nothing" -ForegroundColor Red
    exit 1
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

# --- what the green does NOT cover ----------------------------------------
# A PASS is only as honest as its carve-outs, and a carve-out nobody reads is
# the same silence this script exists to break. So print them: whoever reads
# the green sees exactly which files the record contract is not checking, and
# which rails the catalogue is not describing.
Write-Host ''
Write-Host 'carve-outs this PASS does not cover:'
foreach ($k in ($recordExempt.Keys | Sort-Object)) {
    Write-Host ("  record contract  {0,-22} {1}" -f $k, $recordExempt[$k])
}
if ($docExempt.Count) {
    foreach ($k in ($docExempt.Keys | Sort-Object)) {
        Write-Host ("  rail catalogue   {0,-22} {1}" -f $k, $docExempt[$k])
    }
} else {
    Write-Host '  rail catalogue   (none)                every declared rail is described'
}
Write-Host ''
Write-Host ("railparity: PASS - every rail the code reads is discoverable, every declared rail")
Write-Host ("                   exists and has a row of its own under its registry family in")
Write-Host ("                   Docs/A2GSPU_RAILS.md with a whole synopsis, no non-exempt file")
Write-Host ("                   prints a JSON record or writes to any of the {0} rail sinks itself," -f $railSinks.Count)
Write-Host ("                   all {0} declared readers and all {1} live-settable rails are registered" -f $fnRows.Count, $railRows.Count)
Write-Host ("                   by their rails, and all {0} CTRL verbs are both dispatched and listed." -f $verbTable.Count)
exit 0
