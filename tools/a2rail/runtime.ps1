<# Shared host-runtime discovery for every a2rail entry point. #>

function Initialize-A2RailRuntime {
    [CmdletBinding()]
    param([switch]$Quiet)

    $candidates = @()
    if ($env:A2GSPU_UCRT_BIN) { $candidates += $env:A2GSPU_UCRT_BIN }
    if ($env:MSYS2_ROOT) { $candidates += (Join-Path $env:MSYS2_ROOT 'ucrt64\bin') }
    $candidates += 'C:\msys64\ucrt64\bin'

    $runtime = $candidates | Where-Object {
        $_ -and (Test-Path (Join-Path $_ 'libstdc++-6.dll'))
    } | Select-Object -First 1

    if (-not $runtime) {
        throw ('GSSquared UCRT runtime not found. Set A2GSPU_UCRT_BIN to the directory ' +
               'containing libstdc++-6.dll (normally <MSYS2>\ucrt64\bin). ' +
               'Without it Windows exits with 0xC0000135 before the rail can start.')
    }

    $parts = $env:PATH -split ';'
    if ($parts -notcontains $runtime) { $env:PATH = "$runtime;$env:PATH" }
    if (-not $Quiet) { Write-Host "runtime ucrt: $runtime" -ForegroundColor DarkCyan }
    return $runtime
}

# Count the entries of a C array in a source file, refusing every way the scrape
# can quietly stop scraping.
#
# WHY IT LIVES HERE. personalitycheck.ps1 iterated a HAND-TRANSCRIBED list of 15
# machine personalities and printed a hard-coded "ALL GREEN -- 15", while
# src/systemconfig.cpp held 16 and the live rail reported configs=16. Config 15,
# "Apple IIgs + A2GSPU + Mouse", had never been started by any gate, and nothing
# could ever have said so: the suite compared its own transcription against
# itself. protocolcheck.ps1 had the identical defect and closed it with this
# function -- one file away in the same directory, so the suite contained both
# the cure and the disease.
#
# Every refusal below is a real way this has failed elsewhere in the tree: a file
# that moved, an array that was renamed, a shape that changed so the entry regex
# matches nothing. A count of 0 is never a legitimate answer here, so it throws
# rather than reporting that everything passed.
function Get-CArrayCount([string]$file, [string]$open, [string]$entry, [string]$what) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "${what}: $file is gone; this check can no longer be made"
    }
    $L = Get-Content -LiteralPath $file
    $s = -1; for ($i = 0; $i -lt $L.Count; $i++) { if ($L[$i] -match $open) { $s = $i; break } }
    if ($s -lt 0) {
        throw "${what}: no array opening matching '$open' in $file -- the scrape has stopped scraping"
    }
    $e = -1; for ($i = $s + 1; $i -lt $L.Count; $i++) { if ($L[$i] -match '^\};') { $e = $i; break } }
    if ($e -lt 0) {
        throw "${what}: the array opened at $file line $($s+1) never closes with '};' -- the scrape has stopped scraping"
    }
    $n = 0; for ($i = $s + 1; $i -lt $e; $i++) { if ($L[$i] -match $entry) { $n++ } }
    if ($n -lt 1) {
        throw "${what}: counted 0 entries between $file lines $($s+2) and $e -- '$entry' no longer matches the array's shape, so this check has stopped checking"
    }
    return $n
}

# Read a CTRL reply file through a transient sharing violation. The emulator
# renames ack.N into place already closed, but another process on the host can
# hold a freshly created file open for an instant; a gate run failed on exactly
# that and was green on rerun. ctl.ps1 carries the same function (it does not
# load this file). Any other error, or a lock that outlasts ~3 s, still throws.
function Read-AckText([string]$path) {
    for ($i = 0; ; $i++) {
        try {
            return [IO.File]::ReadAllText($path)
        } catch [IO.IOException] {
            if ($i -ge 30) { throw }
            Start-Sleep -Milliseconds 100
        }
    }
}
