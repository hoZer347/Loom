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
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',

    [switch] $NoBuild,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $TestArgs = @()
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Find-MSBuild
{
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere)
    {
        $found = & $vswhere -latest -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        if ($found) { return $found }
    }

    $fallback = Get-ChildItem `
        -Path "$env:ProgramFiles\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio" `
        -Filter MSBuild.exe -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\Bin\MSBuild\.exe$' } |
        Select-Object -First 1

    if ($fallback) { return $fallback.FullName }

    throw 'Could not locate MSBuild.exe. Run this from a Developer PowerShell, or install the C++ build tools.'
}

if (-not $NoBuild)
{
    $msbuild = Find-MSBuild
    Write-Host "Building Loom Tests ($Configuration) with $msbuild"

    # Built through the solution so the engine, ImGui and math projects are
    # rebuilt first and everything lands in the shared x64\<config> directory.
    # See the note in run-web-tests.ps1: a native command's stderr would
    # otherwise be raised as a terminating NativeCommandError.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try
    {
        & $msbuild (Join-Path $root 'Loom.sln') /t:"Loom Tests" `
            /p:Configuration=$Configuration /p:Platform=x64 /nologo /v:minimal /m
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
