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

    if ($LASTEXITCODE -ne 0)
    {
        Write-Error "Build failed with exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}

$exe = Join-Path $root "x64\$Configuration\Loom Tests.exe"
if (-not (Test-Path $exe))
{
    Write-Error "Test executable not found at $exe"
    exit 1
}

# So a run stopped at its time limit reports it and still exits with its code.
$ErrorActionPreference = 'Continue'
Invoke-TestSuite $exe $TestArgs $TimeoutMinutes
exit $LASTEXITCODE
