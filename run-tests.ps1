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

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $TestArgs = @()
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

. (Join-Path $root 'MSBuild.ps1')

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

# Same NativeCommandError guard as above: doctest writes to stderr, and so does
# the engine (one test deliberately makes a queued task throw). The exit code is
# what says whether the run passed.
$ErrorActionPreference = 'Continue'
& $exe @TestArgs
exit $LASTEXITCODE
