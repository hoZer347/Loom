<#
.SYNOPSIS
    Builds and runs the Loom test suite.

.EXAMPLE
    .\run-tests.ps1
    .\run-tests.ps1 -Configuration Release
    .\run-tests.ps1 -TestArgs '-ts=State','-s'

.NOTES
    Exits with the test runner's exit code, so CI can gate on it directly.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'Final')]
    [string] $Configuration = 'Debug',

    [switch] $NoBuild,

    # Many times what the suite takes on a loaded machine; past it the run has deadlocked.
    [int] $TimeoutMinutes = 20,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $TestArgs = @()
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

. (Join-Path $root 'MSBuild.ps1')
. (Join-Path $root 'TestRun.ps1')

# Logs to this tree's stage times, on a machine that has the tool.
function Write-StageTime([string] $Stage, [datetime] $Started)
{
    $tool = Join-Path $env:USERPROFILE '.claude\tools\stage-timing.ps1'
    if (Test-Path -LiteralPath $tool) { & $tool -Stage $Stage -Root $PSScriptRoot -Started $Started }
}

$started = Get-Date

if (-not $NoBuild)
{
    $msbuild = Find-MSBuild
    Write-Host "Building Loom Tests ($Configuration) with $msbuild"

    # Built through the solution so the engine and ImGui projects are
    # rebuilt first and everything lands in the shared x64\<config> directory.
    # See the note in run-web-tests.ps1: a native command's stderr would
    # otherwise be raised as a terminating NativeCommandError.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try
    {
        & $msbuild (Join-Path $root 'Loom.sln') /t:"Loom Tests" `
            /p:Configuration=$Configuration /p:Platform=x64 /nologo /v:minimal '/clp:ErrorsOnly;WarningsOnly;Summary' /m /nodeReuse:false
    }
    finally { $ErrorActionPreference = $previous }

    $code = $LASTEXITCODE
    Write-StageTime 'build' $started
    $started = Get-Date

    if ($code -ne 0)
    {
        Write-Error "Build failed with exit code $code"
        exit $code
    }
}

$exe = Join-Path $root "x64\$Configuration\Loom Tests.exe"
if (-not (Test-Path $exe))
{
    Write-Error "Test executable not found at $exe"
    exit 1
}

# Claude Code sets CLAUDECODE, so an agent's run can be watched case by case.
$progress = if ($env:CLAUDECODE) { Join-Path (Split-Path $exe) 'Loom Tests Progress.log' }

# So a run stopped at its time limit reports it and still exits with its code.
$ErrorActionPreference = 'Continue'
Invoke-TestSuite $exe $TestArgs $TimeoutMinutes $progress
$result = $LASTEXITCODE

$common = & git -C $root rev-parse --path-format=absolute --git-common-dir
$slot = [IO.Path]::GetFullPath((Split-Path $common -Parent)) -ne [IO.Path]::GetFullPath($root)

if ($slot -and $result -eq 0 -and -not $TestArgs)
{
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'Loom Tests\update-editor-library.ps1')
    $result = $LASTEXITCODE

    foreach ($script in 'extension-reload.ps1', 'project-extensions.ps1')
    {
        if ($result -ne 0) { break }

        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "Loom Tests\$script")
        $result = $LASTEXITCODE
    }
}

Write-StageTime 'test' $started
exit $result
