<#
.SYNOPSIS
    Checks that run-on-agent-desktop.ps1 leaves nothing of a run behind.

.DESCRIPTION
    Runs a program that starts a grandchild sharing its output, once past the
    timeout and once ending on its own. Each time the grandchild has to be
    gone when the script returns, and the caller's pipe closed on time. Then a
    run keeps the exit code of a program that ends on its own. Throws naming
    what failed.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File 'C:\Users\3hoze\Desktop\Loom 1\Loom Tests\agent-desktop.ps1'
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$Wrapper = Join-Path (Split-Path $PSScriptRoot) 'run-on-agent-desktop.ps1'
$TimeoutSeconds = 3
$GrandchildSeconds = 60
$GrandchildStartMilliseconds = 2000
$QuickSeconds = 25
$StoppedExitCode = -1
$OwnExitCode = 7

function Get-Encoded([string] $Code)
{
    [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($Code))
}

# A process running the encoded command.
function Get-Running([string] $Encoded)
{
    @(Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe'" | Where-Object { $_.CommandLine -like "*$Encoded*" })
}

function Invoke-Run([string] $Code, [int] $Timeout)
{
    $line = "& '$Wrapper' -Exe 'powershell.exe' -TimeoutSeconds $Timeout -- -NoProfile -EncodedCommand $(Get-Encoded $Code); exit `$LASTEXITCODE"
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $output = (& powershell -NoProfile -ExecutionPolicy Bypass -OutputFormat Text -Command $line) -join "`n"
    [pscustomobject] @{ Output = $output; ExitCode = $LASTEXITCODE; Seconds = $clock.Elapsed.TotalSeconds }
}

$grandchild = Get-Encoded "Start-Sleep $GrandchildSeconds # $([Guid]::NewGuid())"
$start = "Start-Process powershell -NoNewWindow -ArgumentList '-NoProfile','-EncodedCommand','$grandchild'; Start-Sleep -Milliseconds $GrandchildStartMilliseconds"

try
{
    $capped = Invoke-Run "$start; Start-Sleep $GrandchildSeconds" $TimeoutSeconds
    if ($capped.ExitCode -ne $StoppedExitCode) { throw "A run stopped at its timeout exited $($capped.ExitCode): $($capped.Output)" }
    if ($capped.Seconds -gt $QuickSeconds) { throw "A run stopped at its timeout took $($capped.Seconds) seconds, so something held the caller's pipe." }
    if ((Get-Running $grandchild).Count) { throw 'A grandchild outlived the timeout.' }

    $ended = Invoke-Run "$start; exit $OwnExitCode" $GrandchildSeconds
    if ($ended.ExitCode -ne $OwnExitCode) { throw "A run lost its program's exit code, $($ended.ExitCode): $($ended.Output)" }
    if ($ended.Seconds -gt $QuickSeconds) { throw "A run that ended on its own took $($ended.Seconds) seconds, so something held the caller's pipe." }
    if ((Get-Running $grandchild).Count) { throw 'A grandchild outlived its program.' }

    Write-Output 'agent-desktop passed.'
}
finally
{
    Get-Running $grandchild | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}
