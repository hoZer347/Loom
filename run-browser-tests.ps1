<#
.SYNOPSIS
    Builds the browser half of the Loom test suite and runs it in headless Chrome.

.DESCRIPTION
    The cases that need a real page and not node: input sent through Chrome's
    own pipeline to the canvas, and threads in a cross-origin isolated page.
    They are built with the engine library Play Web uses, together with the
    Input and Threading suites, which run here a third time against the
    browser's own threads.

    Loom Tests\Browser\run.mjs serves the build, drives Chrome over the
    DevTools protocol, and answers each case's request for input. Chrome runs
    headless with a profile of its own, so nothing shows on screen.

.EXAMPLE
    .\run-browser-tests.ps1

.NOTES
    Needs an installed emsdk (see WebBuild.ps1), node on PATH, and Chrome or
    Edge. Exits with the suite's exit code.
#>
[CmdletBinding()]
param()

# Native commands are judged by their exit codes: emcc writes ordinary notes to
# stderr, which PowerShell 5.1 would otherwise turn into terminating errors.
$ErrorActionPreference = 'Continue'
$root = $PSScriptRoot

. (Join-Path $root 'WebBuild.ps1')

$tests = Join-Path $root 'Loom Tests'
$browser = Join-Path $tests 'Browser'
$output = Join-Path $root 'x64\WebTests\Browser'
$intermediate = Join-Path $output 'Intermediate'

$sources = @(
    (Join-Path $browser 'main.cpp')
    (Join-Path $tests 'Input Tests.cpp')
    (Join-Path $tests 'Threading Tests.cpp')
    (Join-Path $browser 'Browser Tests.cpp')
)

$chrome = @(
    (Join-Path $env:ProgramFiles 'Google\Chrome\Application\chrome.exe')
    (Join-Path ${env:ProgramFiles(x86)} 'Google\Chrome\Application\chrome.exe')
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe')
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

if (-not $chrome)
{
    Write-Host 'Neither Chrome nor Edge was found.'
    exit 1
}

$node = Get-Command node -ErrorAction SilentlyContinue

if (-not $node)
{
    Write-Host 'node was not found on PATH; it is what drives the browser.'
    exit 1
}

$emcc = Find-Emcc
$library = Build-WebEngine $emcc

if (-not $library)
{
    exit 1
}

New-Item -ItemType Directory -Force -Path $intermediate | Out-Null

$jobs = @(Get-CompileJobs $sources $root $intermediate)
$flags = @($webCompileFlags) + @($webIncludes) + @(
    "-I$(Join-Path $root 'External Libraries\doctest')"
    "-I$tests"
)

Write-Host "Building the browser suite ($($sources.Count) files)"

if (-not (Invoke-Compile $emcc $jobs $flags))
{
    Write-Host 'The browser suite failed to build.'
    exit 1
}

# The whole archive goes in because components register themselves from
# static initialisers nothing else refers to.
& $emcc @webCompileFlags @webLinkFlags '-sEXIT_RUNTIME=1' @($jobs | ForEach-Object { $_.Object }) `
    '-Wl,--whole-archive' $library '-Wl,--no-whole-archive' `
    -o (Join-Path $output 'tests.js')

if ($LASTEXITCODE -ne 0)
{
    Write-Host "emcc failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

Copy-Item -LiteralPath (Join-Path $browser 'index.html') -Destination $output

& $node.Source (Join-Path $browser 'run.mjs') $output $chrome
exit $LASTEXITCODE
