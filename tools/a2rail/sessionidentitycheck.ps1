[CmdletBinding()]
param(
    [string]$Exe = $env:A2_GSSQUARED,
    [string]$Dir = (Join-Path ([IO.Path]::GetTempPath()) ('a2rail-identity-' + [guid]::NewGuid().ToString('N'))),
    [switch]$Baseline
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Exe) { . (Join-Path $here 'toolpin.ps1'); $Exe = Get-GSSquaredExe }
$Exe = (Resolve-Path -LiteralPath $Exe).Path
New-Item -ItemType Directory -Path $Dir -ErrorAction Stop | Out-Null
$alias = Join-Path (Split-Path $Exe -Parent) ('Agent identity ' + [guid]::NewGuid().ToString('N') + '.exe')
Copy-Item -LiteralPath $Exe -Destination $alias -ErrorAction Stop
$checks = 0
function Require([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
    $script:checks++
    Write-Host ('PASS ' + $message)
}
function Receipt($value, [string]$path) {
    $tmp = $path + '.test.tmp'
    $value | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $tmp -Encoding utf8
    Move-Item -LiteralPath $tmp -Destination $path -Force
}
try {
    foreach ($platform in $(if ($Baseline) { @(3) } else { @(3,5) })) {
        $sessionDir = Join-Path $Dir ('p' + $platform)
        $original = $null
        try {
            & (Join-Path $here 'session.ps1') -Exe $alias -Dir $sessionDir -Platform $platform -TimeoutS 120 | Out-Null
            Require ($LASTEXITCODE -eq 0) "renamed executable starts on p$platform"
            $receiptPath = Join-Path $sessionDir 'session.json'
            $original = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
            $process = Get-Process -Id $original.pid -ErrorAction Stop
            $answer = & (Join-Path $here 'ctl.ps1') -Dir $sessionDir -Cmd 'cpu'
            if ($Baseline) {
                Require ($LASTEXITCODE -eq 2 -and -not $process.HasExited) 'baseline reproduces false dead-session report for a live alias'
                Write-Host ($answer -join "`n")
                continue
            }
            Require ($LASTEXITCODE -eq 0 -and ($answer -join "`n") -match 'status=OK cpu') "renamed executable is driven on p$platform"
            $count = @(Get-ChildItem -LiteralPath $sessionDir -Filter 'cmd.*' | Where-Object Name -Match '^cmd\.\d+$').Count
            $receiptHash=(Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash
            $refused=$false
            try { & (Join-Path $here 'session.ps1') -Exe $alias -Dir $sessionDir -Platform $platform | Out-Null }
            catch { $refused=$true }
            Require ($refused -and (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash -eq $receiptHash) "a live session cannot be replaced on p$platform"
            foreach ($case in @('wrong-path','wrong-start','unrelated-pid','missing-start','invalid-start')) {
                $forged = $original | ConvertTo-Json -Depth 6 | ConvertFrom-Json
                switch ($case) {
                    'wrong-path' { $forged.exe = $Exe }
                    'wrong-start' { $forged.process_start_utc_ticks = ([long]$forged.process_start_utc_ticks + 1).ToString() }
                    'unrelated-pid' { $forged.pid = $PID }
                    'missing-start' { $forged.PSObject.Properties.Remove('process_start_utc_ticks') }
                    'invalid-start' { $forged.process_start_utc_ticks = 'not-a-clock' }
                }
                Receipt $forged $receiptPath
                $answer = & (Join-Path $here 'ctl.ps1') -Dir $sessionDir -Cmd 'cpu'
                Require ($LASTEXITCODE -eq 2) "$case refuses on p$platform"
                Require ((@($answer) -join "`n") -match 'IDENTITY|METADATA') "$case names identity/metadata instead of claiming process exit"
                Require (@(Get-ChildItem -LiteralPath $sessionDir -Filter 'cmd.*' | Where-Object Name -Match '^cmd\.\d+$').Count -eq $count) "$case publishes no command"
            }
            Receipt $original $receiptPath
            $answer = & (Join-Path $here 'ctl.ps1') -Dir $sessionDir -Cmd 'cpu'
            Require ($LASTEXITCODE -eq 0) "restoring the real receipt restores control on p$platform"
        } finally {
            if ($original) {
                Receipt $original (Join-Path $sessionDir 'session.json')
                # The fixture owns this child; direct atomic QUIT also cleans up
                # the pre-fix baseline whose ctl liveness check refuses aliases.
                $commands = @(Get-ChildItem -LiteralPath $sessionDir -Filter 'cmd.*' | Where-Object Name -Match '^cmd\.\d+$')
                $n = 1 + [int](($commands | ForEach-Object { [int]($_.Name.Substring(4)) } | Measure-Object -Maximum).Maximum)
                $command = Join-Path $sessionDir ('cmd.' + $n)
                Set-Content -LiteralPath ($command + '.tmp') -Value 'quit' -NoNewline
                Move-Item -LiteralPath ($command + '.tmp') -Destination $command
                if (-not $process.WaitForExit(5000)) { throw "owned identity fixture did not quit: $($original.pid)" }
            }
        }
        if (-not $Baseline) {
            $answer = & (Join-Path $here 'ctl.ps1') -Dir $sessionDir -Cmd 'cpu'
            Require ($LASTEXITCODE -eq 2 -and ($answer -join "`n") -match 'NO SESSION') "a genuinely exited process is reported as gone on p$platform"
        }
    }
} finally {
    # Only this fixture's unique copied file, never a directory or consumer exe.
    if (Test-Path -LiteralPath $alias) { Remove-Item -LiteralPath $alias -ErrorAction Continue }
}
Write-Host "ALL GREEN -- session identity checks=$checks baseline=$Baseline evidence=$Dir"
exit 0
