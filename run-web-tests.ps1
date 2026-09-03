<#
.SYNOPSIS
    Builds the Loom test suite for the web with emcc and runs it under node.

.DESCRIPTION
    The same test sources and the same engine sources the desktop build uses,
    compiled to WebAssembly. The source list is read straight out of
    "Loom Tests\Loom Tests.vcxproj" so the two builds cannot drift apart.

    Nothing in the suite creates a window or a GL context, so the module runs
    headless under node -- no canvas and no browser needed. The cases that
    genuinely cannot work there (shader loading, which goes through
    emscripten_fetch on this target) are compiled out with __EMSCRIPTEN__.

.EXAMPLE
    .\run-web-tests.ps1
    .\run-web-tests.ps1 -Configuration Release
    .\run-web-tests.ps1 -NoBuild -- -ts=State

.NOTES
    Needs an installed emsdk. Set EMSDK, or have it at C:\emsdk or under
    "External Libraries\emsdk". Exits with the test runner's exit code.
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

function Find-Emcc
{
    $candidates = @()

    if ($env:EMSDK) { $candidates += (Join-Path $env:EMSDK 'upstream\emscripten\emcc.bat') }

    $candidates += 'C:\emsdk\upstream\emscripten\emcc.bat'
    $candidates += (Join-Path $root 'External Libraries\emsdk\upstream\emscripten\emcc.bat')

    foreach ($candidate in $candidates)
    {
        if (Test-Path $candidate) { return $candidate }
    }

    $onPath = Get-Command emcc.bat -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    throw @"
Could not find emcc. Install the emscripten SDK and either set EMSDK, put it at
C:\emsdk, or install the copy vendored at "External Libraries\emsdk":

    cd "External Libraries\emsdk"
    .\emsdk install latest
    .\emsdk activate latest
"@
}

# The desktop project is the single source of truth for what gets compiled.
# Reading it here means adding a test file, or the engine gaining a source file,
# updates both builds at once.
function Get-SourcesFromProject
{
    param([string] $ProjectPath)

    [xml] $project = Get-Content -LiteralPath $ProjectPath
    $projectDir = Split-Path -Parent $ProjectPath

    $sources = New-Object System.Collections.Generic.List[string]

    foreach ($node in $project.Project.ItemGroup.ClCompile)
    {
        if (-not $node.Include) { continue }

        $include = $node.Include

        if ($include -match '[*?]')
        {
            # MSBuild wildcard. Resolve it the same way, honouring Exclude.
            $matched = Get-ChildItem -Path (Join-Path $projectDir $include) -File -ErrorAction SilentlyContinue

            $excluded = @()
            if ($node.Exclude)
            {
                $excluded = Get-ChildItem -Path (Join-Path $projectDir $node.Exclude) -File -ErrorAction SilentlyContinue |
                    ForEach-Object { $_.FullName }
            }

            foreach ($file in $matched)
            {
                if ($excluded -notcontains $file.FullName) { $sources.Add($file.FullName) }
            }
        }
        else
        {
            $resolved = Join-Path $projectDir $include
            if (Test-Path $resolved) { $sources.Add((Resolve-Path $resolved).Path) }
        }
    }

    return $sources
}

$outputDir = Join-Path $root "x64\Web$Configuration"
$output    = Join-Path $outputDir 'Loom Tests.js'

if (-not $NoBuild)
{
    $emcc = Find-Emcc
    Write-Host "Building the web test suite ($Configuration) with $emcc"

    $sources = Get-SourcesFromProject (Join-Path $root 'Loom Tests\Loom Tests.vcxproj')

    # ImGui is compiled in rather than linked: the desktop build links a .lib,
    # and there is no equivalent prebuilt artefact for wasm. The GLFW and
    # OpenGL3 backends come along because Engine.cpp calls into them.
    $imgui = Join-Path $root 'Loom ImGui'
    $sources += @(
        (Join-Path $imgui 'imgui.cpp')
        (Join-Path $imgui 'imgui_draw.cpp')
        (Join-Path $imgui 'imgui_tables.cpp')
        (Join-Path $imgui 'imgui_widgets.cpp')
        (Join-Path $imgui 'imgui_impl_glfw.cpp')
        (Join-Path $imgui 'imgui_impl_opengl3.cpp')
    )

    $includes = @(
        (Join-Path $root 'External Libraries\doctest')
        (Join-Path $root 'Loom Tests')
        (Join-Path $root 'Loom WASM')
        (Join-Path $root 'Loom Math')
        (Join-Path $root 'Loom Networking')
        $imgui
    ) | ForEach-Object { "-I$_" }

    $optimisation = if ($Configuration -eq 'Release') { '-O2' } else { '-O0' }

    $flags = @(
        '-std=c++20'
        $optimisation
        '-fexceptions'                  # doctest reports throwing tests, and Shader throws.
        '-sUSE_GLFW=3'                  # Engine and the ImGui backend link against GLFW.
        '-sFULL_ES3'
        '-sFETCH'                       # Shader::Request fetches shader sources over HTTP.
        '-sASYNCIFY'                    # ...and waits on the fetch with emscripten_sleep.
        '-sALLOW_MEMORY_GROWTH=1'
        '-sEXIT_RUNTIME=1'              # So the process exit code is the test result.
        '-sENVIRONMENT=node'
        '-sNODERAWFS=1'                 # Gives the suite the real filesystem under node.
    )

    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

    # PowerShell 5.1 wraps a native command's stderr in a NativeCommandError,
    # which the 'Stop' preference above would treat as fatal -- and emcc writes
    # ordinary notes there. Judge it by its exit code instead.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $emcc @flags @includes @sources -o $output }
    finally { $ErrorActionPreference = $previous }

    if ($LASTEXITCODE -ne 0)
    {
        Write-Error "emcc failed with exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}

if (-not (Test-Path $output))
{
    Write-Error "Web test module not found at $output"
    exit 1
}

$node = Get-Command node -ErrorAction SilentlyContinue
if (-not $node)
{
    Write-Error 'node was not found on PATH; it is what runs the WebAssembly test module.'
    exit 1
}

# Same NativeCommandError guard as above: doctest writes to stderr, and so does
# the engine (one test deliberately makes a queued task throw). The exit code is
# what says whether the run passed.
$ErrorActionPreference = 'Continue'
& $node.Source $output @TestArgs
exit $LASTEXITCODE
