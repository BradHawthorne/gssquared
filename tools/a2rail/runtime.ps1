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
