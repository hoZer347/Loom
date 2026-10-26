<#
.SYNOPSIS
    Runs the Performance suite once a day on the latest master, and sets an
    agent on fixing it when it fails.

.DESCRIPTION
    Claims a slot as performance-<date>, which brings it up to master, and runs
    the Performance suite there, on the local build and the web build. A pass
    releases the slot. A failure leaves it claimed and starts Claude Code in it,
    headless, with the failing output, to find the regression and fix it the
    way any change is made here, ending in a demo and the review window. While
    an earlier day's slot is still claimed, a day is skipped rather than
    handing the same regression to a second agent.

    Each run writes a log to %LOCALAPPDATA%\Loom\Performance, and the agent's
    beside it.

.EXAMPLE
    .\daily-performance.ps1               # run it now
    .\daily-performance.ps1 -Register     # run it every day at 3:17
    .\daily-performance.ps1 -Unregister
#>
[CmdletBinding(DefaultParameterSetName = 'Run')]
param(
    [Parameter(ParameterSetName = 'Register', Mandatory = $true)]
    [switch] $Register,

    [Parameter(ParameterSetName = 'Register')]
    [string] $At = '3:17',

    [Parameter(ParameterSetName = 'Unregister', Mandatory = $true)]
    [switch] $Unregister
)

#region Settings

$ErrorActionPreference = 'Stop'

$taskName = 'Loom daily performance tests'
$suiteArgs = @('-ts=Performance', '--no-skip')
$suites = @('run-tests.ps1', 'run-web-tests.ps1')
$logs = Join-Path $env:LOCALAPPDATA 'Loom\Performance'

# How much of a failing suite's output the agent is handed; the whole of it is in the log.
$failureLines = 80

# No limit: a fix waits on the user's review for as long as that takes, and a
# limit would end this run while the agent it started carried on unwatched.
$noTimeLimit = [TimeSpan]::Zero

#endregion

#region Scheduling

if ($Register)
{
    # The task runs the copy in the user's Loom, the repository's main
    # worktree, which only ever holds master.
    $main = (git -C $PSScriptRoot worktree list --porcelain | Select-Object -First 1) -replace '^worktree ', ''
    $script = Join-Path ($main -replace '/', '\') 'daily-performance.ps1'

    # conhost --headless gives the run a console that never shows. It runs
    # only while the user is logged on, so the agent's review window has a
    # desktop to open on, and a day missed while the machine was off runs at
    # the next chance.
    $action = New-ScheduledTaskAction -Execute 'conhost.exe' `
        -Argument "--headless powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$script`""
    $trigger = New-ScheduledTaskTrigger -Daily -At $At
    $principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive
    $settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries `
        -DontStopIfGoingOnBatteries -ExecutionTimeLimit $noTimeLimit

    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
        -Principal $principal -Settings $settings -Force | Out-Null

    Write-Host "Registered '$taskName': $script every day at $At"
    exit 0
}

if ($Unregister)
{
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    Write-Host "Unregistered '$taskName'"
    exit 0
}

#endregion

#region Claim

New-Item -ItemType Directory -Force -Path $logs | Out-Null

# Every step from here is checked by its result. The child processes' stderr,
# where git and doctest write as a matter of course, comes back as error
# records, and under Stop the first of them would end the run unlogged.
$ErrorActionPreference = 'Continue'

$stamp = Get-Date -Format 'yyyy-MM-dd'
$log = Join-Path $logs "$stamp.log"
$feature = "performance-$stamp"
$slots = Join-Path $PSScriptRoot '.claude\slots.ps1'

function Write-Log([string] $text)
{
    Add-Content -Path $log -Value "[$(Get-Date -Format 'HH:mm:ss')] $text"
}

# A regression an earlier day's agent is still on, or still waiting on review
# for, is not handed to a second one.
$held = & powershell -NoProfile -ExecutionPolicy Bypass -File $slots status 2>&1 |
    Where-Object { $_ -match 'feature/performance-\d' }

if ($held)
{
    Write-Log "An earlier run's slot is still claimed, so this one is skipped: $($held -join '; ')"
    exit 0
}

$claimed = & powershell -NoProfile -ExecutionPolicy Bypass -File $slots claim $feature 2>&1 | Out-String
$slot = ($claimed.Trim() -split "`r?`n")[-1]

if (-not (Test-Path (Join-Path $slot '.claude\slots.ps1')))
{
    Write-Log "Could not claim a slot: $claimed"
    exit 1
}

Write-Log "Claimed $slot as feature/$feature"

#endregion

#region Suites

$failures = @()

foreach ($suite in $suites)
{
    # Each in its own process, so a failed build or a missing toolchain ends
    # only the suite.
    $output = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $slot $suite) @suiteArgs *>&1 | Out-String
    $code = $LASTEXITCODE

    Write-Log "$suite exited $code`n$output"

    # A suite renamed or emptied would otherwise pass every day without testing anything.
    if ($code -eq 0 -and $output -match 'test cases:\s*0\b')
    {
        $failures += "$suite ran no Performance cases at all. The end of its output:`n$($output.Trim())"
        continue
    }

    if ($code -ne 0)
    {
        $tail = ($output.Trim() -split "`r?`n" | Select-Object -Last $failureLines) -join "`n"
        $failures += "$suite exited $code. The end of its output:`n$tail"
    }
}

if ($failures.Count -eq 0)
{
    & powershell -NoProfile -ExecutionPolicy Bypass -File $slots release $feature | Out-Null
    Write-Log 'Passed; released the slot'
    exit 0
}

#endregion

#region Agent

# Claude Code on PATH, or else the newest the VS Code extension carries.
function Find-Claude
{
    $onPath = Get-Command claude -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    Get-ChildItem (Join-Path $env:USERPROFILE '.vscode\extensions') -Directory -Filter 'anthropic.claude-code-*' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        ForEach-Object { Join-Path $_.FullName 'resources\native-binary\claude.exe' } |
        Where-Object { Test-Path $_ } |
        Select-Object -First 1
}

$claude = Find-Claude

if (-not $claude)
{
    Write-Log "Failed, and Claude Code is nowhere to be found to fix it. $slot is still claimed as feature/$feature."
    exit 1
}

$prompt = @"
The daily Performance suite failed on master. You are in $slot, claimed as feature/$feature and already up to date with master; work there. Run the suite with .\run-tests.ps1 -TestArgs $(($suiteArgs | ForEach-Object { "'$_'" }) -join ',') and the same for .\run-web-tests.ps1; the full output is in $log.

$($failures -join "`n`n")

Find what made it slower and fix it, the way CLAUDE.md sets out: build, run both test suites, get the code-reviewer's acceptance, then a demo and .claude\review.ps1, and merge and release once the user approves. If the suite passes when you rerun it and nothing in master explains the slowdown, the machine was busy when it ran: release the slot and stop.
"@

$agentLog = Join-Path $logs "$stamp agent.log"
Write-Log "Failed; starting $claude in $slot, logging to $agentLog"

Push-Location $slot
try
{
    & $claude -p $prompt --permission-mode bypassPermissions *> $agentLog
}
finally { Pop-Location }

Write-Log "Agent finished with exit code $LASTEXITCODE"
exit 1

#endregion
