<#
.SYNOPSIS
    Builds a Loom project for the web and serves it. The editor's Play Web runs this.

.DESCRIPTION
    The engine is compiled into a static library under x64\WebPlayer, rebuilt
    only when an engine source is newer than it. The project's scripts and the
    player entry point are linked against it beside the scene the editor wrote,
    unless nothing has changed since the last link.

    The page is served from the project folder by emrun, which sends the
    cross-origin isolation headers a pthreads build needs. emrun is not asked
    to open a browser: the editor opens the page once emrun says it is
    listening, so the browser is not left a child of this script.

.EXAMPLE
    .\play-web.ps1 -Project "C:\Games\MyGame" -Scene "C:\Games\MyGame\Build\Web\Scene.loomscene"

.NOTES
    Needs an installed emsdk; see WebBuild.ps1. Serves until it is killed.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Project,

    # Inside the project folder. The build goes beside it.
    [Parameter(Mandatory = $true)]
    [string] $Scene
)

# Native commands are judged by their exit codes: emcc writes ordinary notes to
# stderr, which PowerShell 5.1 would otherwise turn into terminating errors.
$ErrorActionPreference = 'Continue'
$root = $PSScriptRoot

. (Join-Path $root 'WebBuild.ps1')

$Project = (Resolve-Path -LiteralPath $Project -ErrorAction Stop).Path
$Scene = (Resolve-Path -LiteralPath $Scene -ErrorAction Stop).Path
$output = Split-Path -Parent $Scene

$emcc = Find-Emcc
$emrun = Join-Path (Split-Path -Parent $emcc) 'emrun.bat'

# A path under the project as the page addresses it: from the root it is served from.
function Get-UrlPath([string] $Path)
{
    '/' + $Path.Substring($Project.Length).TrimStart('\').Replace('\', '/')
}

$library = Build-WebEngine $emcc

if (-not $library)
{
    exit 1
}

# --- The project ------------------------------------------------------------

# Every header beside the script project the .loomproject names, as the
# Visual Studio project compiles them.
$scripts = @()
$scriptDir = $null
$projectFile = Get-ChildItem -Path (Join-Path $Project '*.loomproject') | Select-Object -First 1

if ($projectFile)
{
    $scriptsLine = Get-Content -LiteralPath $projectFile.FullName | Where-Object { $_ -like 'scripts=*' } | Select-Object -First 1

    if ($scriptsLine)
    {
        $scriptDir = Split-Path -Parent (Join-Path $Project $scriptsLine.Substring('scripts='.Length))

        if (Test-Path -LiteralPath $scriptDir)
        {
            $scripts = @(Get-ChildItem -Path $scriptDir -Recurse -File -Filter '*.hpp' | ForEach-Object { $_.FullName } | Sort-Object)
        }
    }
}

$player = Join-Path $root 'Loom Web\Player.cpp'
$module = Join-Path $output 'player.js'
$intermediate = Join-Path $output 'Intermediate'

New-Item -ItemType Directory -Force -Path $intermediate | Out-Null

# Rewritten only when the set of scripts changes, so a deleted script, which
# leaves every remaining input older than the module, still forces a relink.
$scriptList = Join-Path $intermediate 'scripts.txt'
$listed = ($scripts -join "`n")

if (-not (Test-Path -LiteralPath $scriptList) -or [string](Get-Content -LiteralPath $scriptList -Raw) -ne $listed)
{
    [IO.File]::WriteAllText($scriptList, $listed)
}

$linkInputs = @(
    Get-Item -LiteralPath $library, $player, $PSCommandPath, (Join-Path $root 'WebBuild.ps1'), $scriptList
    $scripts | ForEach-Object { Get-Item -LiteralPath $_ }
)

if (Test-Stale $module $linkInputs)
{
    Write-Host "Linking $($scripts.Count) script file(s) into $module"

    # Scripts are headers compiled as their own translation units, as the
    # Visual Studio project does. emcc will not take a header beside other
    # inputs, so each one is compiled on its own first.
    $scriptJobs = @()

    if ($scripts.Count -gt 0)
    {
        $scriptJobs = @(Get-CompileJobs $scripts $scriptDir $intermediate)
        $scriptFlags = @($webCompileFlags) + @($webIncludes) + @("-I$scriptDir", '-Wno-pragma-once-outside-header', '-x', 'c++')

        if (-not (Invoke-Compile $emcc $scriptJobs $scriptFlags))
        {
            Write-Host 'The scripts failed to build.'
            exit 1
        }
    }

    # The whole archive goes in because components register themselves from
    # static initialisers nothing else refers to. The page preloads the scene
    # through FS.
    & $emcc @webCompileFlags @webLinkFlags @webIncludes '-sEXPORTED_RUNTIME_METHODS=FS' $player @($scriptJobs | ForEach-Object { $_.Object }) `
        '-Wl,--whole-archive' $library '-Wl,--no-whole-archive' `
        -o $module

    if ($LASTEXITCODE -ne 0)
    {
        Write-Host "emcc failed with exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}

$page = Join-Path $output 'index.html'

(Get-Content -LiteralPath (Join-Path $root 'Loom Web\Player.html') -Raw).
    Replace('{TITLE}', (Split-Path -Leaf $Project)).
    Replace('{SCRIPT}', (Get-UrlPath $module)).
    Replace('{SCENE}', (Get-UrlPath $Scene)) |
    Set-Content -LiteralPath $page -Encoding UTF8 -NoNewline

# --- Serving ----------------------------------------------------------------

# Port 0 takes whatever is free; emrun prints the one it got.
& $emrun --no_browser --no_emrun_detect --serve_after_close --hostname localhost --port 0 --serve_root $Project $page
exit $LASTEXITCODE
