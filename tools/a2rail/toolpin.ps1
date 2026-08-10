<#
    toolpin.ps1 -- report which foundation binary a build or session is using,
    and resolve portable foundation roots (FOUNDATION_MILESTONES M1 + M2).

    Product-side only -- no hashes baked into the tools.

    Dot-source from build.ps1 / session.ps1 / harnesses:

        . (Join-Path $PSScriptRoot "toolpin.ps1")
        $Asm = Get-AsmiigsExe
        Write-ToolPin -Label "asmiigs" -Exe $Asm -RepoHint "rosetta" -Strict:$StrictTools
        $Exe = Get-GSSquaredExe
        Write-ToolPin -Label "GSSquared" -Exe $Exe -RepoHint "gssquared" -Strict:$StrictTools

    Env (any one is enough):
        ROSETTA_ROOT       -> <root>/build/apps/asmiigs/asmiigs.exe, diskiigs, ...
        A2_ASMIIGS         -> full path to asmiigs.exe
        GSSQUARED_ROOT     -> <root>/build/GSSquared.exe
        A2_GSSQUARED       -> full path to GSSquared.exe

    Defaults keep the historical D:\projects\... paths when env is unset.

    -Strict: if source under the repo looks newer than the exe, throw
    (or warn when -WarnOnly). Heuristic, not a full dependency graph.
#>

function Get-RosettaRoot {
    if ($env:ROSETTA_ROOT) { return $env:ROSETTA_ROOT }
    return "D:\projects\toolchain\rosetta_v3"
}

function Get-GssquaredRoot {
    if ($env:GSSQUARED_ROOT) { return $env:GSSQUARED_ROOT }
    # Mirror of gssquared/tools/a2rail layout (M9 instrument home).
    if ($PSScriptRoot -and ($PSScriptRoot -match '[\\/]tools[\\/]a2rail$')) {
        $repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
        if (Test-Path (Join-Path $repo "CMakeLists.txt")) { return $repo }
    }
    return "D:\projects\gssquared-fork"
}

function Get-AsmiigsExe {
    if ($env:A2_ASMIIGS) { return $env:A2_ASMIIGS }
    return (Join-Path (Get-RosettaRoot) "build\apps\asmiigs\asmiigs.exe")
}

function Get-DiskiigsExe {
    if ($env:A2_DISKIIGS) { return $env:A2_DISKIIGS }
    return (Join-Path (Get-RosettaRoot) "build\apps\diskiigs\diskiigs.exe")
}

function Get-GSSquaredExe {
    if ($env:A2_GSSQUARED) { return $env:A2_GSSQUARED }
    return (Join-Path (Get-GssquaredRoot) "build\GSSquared.exe")
}

function Write-ToolPinFile([string]$Path) {
    if (-not $Path -or -not (Test-Path $Path)) { return }
    Get-Content $Path -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host $_ -ForegroundColor DarkCyan
    }
}

function Resolve-ToolRepo([string]$Exe) {
    if (-not $Exe) { return $null }
    $full = [System.IO.Path]::GetFullPath($Exe)
    $dir = Split-Path $full -Parent
    # Prefer the directory that owns build\ for known layouts.
    $cursor = $dir
    for ($i = 0; $i -lt 8 -and $cursor; $i++) {
        $git = Join-Path $cursor ".git"
        $build = Join-Path $cursor "build"
        if ((Test-Path $git) -and (Test-Path $build)) { return $cursor }
        if (Test-Path $git) { return $cursor }
        $parent = Split-Path $cursor -Parent
        if ($parent -eq $cursor) { break }
        $cursor = $parent
    }
    return $null
}

function Get-GitShort([string]$Repo) {
    if (-not $Repo) { return "?" }
    try {
        $h = & git -C $Repo rev-parse --short HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $h) { return ($h | Select-Object -First 1).ToString().Trim() }
    } catch {}
    return "?"
}

function Get-GitDirty([string]$Repo) {
    if (-not $Repo) { return "" }
    try {
        $s = & git -C $Repo status --porcelain 2>$null
        if ($LASTEXITCODE -eq 0 -and $s) { return " dirty" }
    } catch {}
    return ""
}

function Test-ExeStale([string]$Exe, [string]$Repo, [string]$RepoHint) {
    if (-not (Test-Path $Exe) -or -not $Repo) { return $null }
    $exeTime = (Get-Item $Exe).LastWriteTimeUtc
    $globs = @()
    switch -Regex ($RepoHint) {
        'rosetta|asmiigs' {
            $globs = @(
                (Join-Path $Repo "apps\asmiigs\src\*"),
                (Join-Path $Repo "apps\asmiigs\include\*"),
                (Join-Path $Repo "libs\lib65816\src\*"),
                (Join-Path $Repo "libs\lib65816\include\*")
            )
        }
        'gssquared|GSSquared' {
            $globs = @(
                (Join-Path $Repo "src\*.cpp"),
                (Join-Path $Repo "src\*.hpp"),
                (Join-Path $Repo "src\**\*.cpp"),
                (Join-Path $Repo "src\**\*.hpp")
            )
        }
        default { return $null }
    }
    $newest = $null
    foreach ($g in $globs) {
        Get-ChildItem -Path $g -File -ErrorAction SilentlyContinue | ForEach-Object {
            if (-not $newest -or $_.LastWriteTimeUtc -gt $newest.LastWriteTimeUtc) {
                $newest = $_
            }
        }
    }
    # Recursive fallback for gssquared src tree (PowerShell ** can be flaky).
    if ($RepoHint -match 'gssquared|GSSquared') {
        Get-ChildItem -Path (Join-Path $Repo "src") -Recurse -Include *.cpp,*.hpp,*.h -File -ErrorAction SilentlyContinue |
            ForEach-Object {
                if (-not $newest -or $_.LastWriteTimeUtc -gt $newest.LastWriteTimeUtc) {
                    $newest = $_
                }
            }
    }
    if (-not $newest) { return $null }
    if ($newest.LastWriteTimeUtc -gt $exeTime) {
        return $newest
    }
    return $null
}

function Write-ToolPin {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string]$Label,
        [Parameter(Mandatory)] [string]$Exe,
        [string]$RepoHint = "",
        [switch]$Strict,
        [switch]$WarnOnly,
        [string]$ReportFile = ""
    )
    if (-not (Test-Path $Exe)) {
        throw ("{0} not found: {1}" -f $Label, $Exe)
    }
    $item = Get-Item $Exe
    $repo = Resolve-ToolRepo $Exe
    $hash = Get-GitShort $repo
    $dirty = Get-GitDirty $repo
    $ver = ""
    if ($Label -match 'asmiigs|Asm') {
        try {
            $vo = & $Exe --version 2>&1 | Select-Object -First 2
            $ver = ($vo -join " / ").Trim()
        } catch { $ver = "" }
    }
    $mtime = $item.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")
    $line = if ($ver) {
        "toolpin {0}: {1} | {2} | git {3}{4} | {5}" -f $Label, $Exe, $ver, $hash, $dirty, $mtime
    } else {
        "toolpin {0}: {1} | git {2}{3} | mtime {4}" -f $Label, $Exe, $hash, $dirty, $mtime
    }
    Write-Host $line -ForegroundColor DarkCyan
    if ($ReportFile) {
        Add-Content -Path $ReportFile -Value $line
    }
    $stale = Test-ExeStale -Exe $Exe -Repo $repo -RepoHint $RepoHint
    if ($stale) {
        $msg = ("{0} may be STALE: source {1} is newer than the binary ({2})" -f `
                $Label, $stale.FullName, $Exe)
        if ($Strict -and -not $WarnOnly) {
            throw $msg
        }
        Write-Host ("  WARN  {0}" -f $msg) -ForegroundColor Yellow
    }
    return [pscustomobject]@{
        Label = $Label
        Exe   = $Exe
        Repo  = $repo
        Hash  = $hash
        Dirty = [bool]$dirty
        Line  = $line
    }
}
