<#
.SYNOPSIS
    Builds a Loom project for the web and serves it, or writes it to a folder of its own. The editor's Play Web runs this.

.DESCRIPTION
    The engine is compiled into a static library under x64\WebPlayer, rebuilt
    only when an engine source is newer than it. The project's scripts and the
    player entry point are linked against it beside the scene the editor wrote,
    unless nothing has changed since the last link.

    The page is served from the project folder by emrun, which sends the
    cross-origin isolation headers a pthreads build needs. emrun is not asked
    to open a browser: the editor opens the page once emrun says it is
    listening, so the browser is not left a child of this script.

    Given -Output, nothing is served. The build goes under the project's
    Build\Web, and Output is mirrored to the page, the player and the files
    of the project it reads, leaving out what build-game.ps1 leaves out. Its
    URLs are relative, so any web server can host the folder at any path,
    as long as it sends the same cross-origin isolation headers. A folder
    that is not empty and holds no earlier build is left alone.

.EXAMPLE
    .\play-web.ps1 -Project "C:\Games\MyGame" -Scene "C:\Games\MyGame\Build\Web\Scene.loomscene"

.EXAMPLE
    .\play-web.ps1 -Project "C:\Games\MyGame" -Scene "C:\Games\MyGame\Scenes\Main.loomscene" -Output "C:\Site\MyGame"

.NOTES
    Needs an installed emsdk; see WebBuild.ps1. Without -Output, serves until it is killed.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Project,

    # Inside the project folder. Served, the build goes beside it.
    [Parameter(Mandatory = $true)]
    [string] $Scene,

    [string] $Output
)

#region Setup

# Native commands are judged by their exit codes: emcc writes ordinary notes to
# stderr, which PowerShell 5.1 would otherwise turn into terminating errors.
$ErrorActionPreference = 'Continue'
$root = $PSScriptRoot

. (Join-Path $root 'WebBuild.ps1')
. (Join-Path $root 'ProjectFiles.ps1')

$Project = (Resolve-Path -LiteralPath $Project -ErrorAction Stop).Path
$Scene = (Resolve-Path -LiteralPath $Scene -ErrorAction Stop).Path
$web = if ($Output) { Join-Path $Project 'Build\Web' } else { Split-Path -Parent $Scene }

if (-not $Scene.StartsWith("$Project\", [StringComparison]::OrdinalIgnoreCase))
{
    Write-Host "$Scene is not in the project."
    exit 1
}

# The folder the page's base is: the project, or its copy in Output.
$served = $Project

if ($Output)
{
    $Output = [IO.Path]::GetFullPath($Output).TrimEnd('\')
    $served = $Output

    if ("$Project\".StartsWith("$Output\", [StringComparison]::OrdinalIgnoreCase))
    {
        Write-Host "The project is in $Output, which a build would empty."
        exit 1
    }
}

$emcc = Find-Emcc
$emrun = Get-EmscriptenTool $emcc 'emrun'

# A path under the project as the page addresses it: from its base, the project folder.
function Get-UrlPath([string] $Path)
{
    $Path.Substring($Project.Length).TrimStart('\').Replace('\', '/')
}

$library = Build-WebEngine $emcc

if (-not $library)
{
    exit 1
}

#endregion

#region Project

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
$module = Join-Path $web 'player.js'
$intermediate = Join-Path $web 'Intermediate'

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
        # The engine's header is precompiled and forced into each script, as
        # the Visual Studio project does. Only what changed since the last link
        # is compiled again, the emscripten that built it counting as a flag.
        $version = (Get-Content -LiteralPath (Join-Path (Split-Path -Parent $emcc) 'emscripten-version.txt') -Raw).Trim()

        $header = [pscustomobject] @{
            Source = Join-Path $webWasm 'Loom.h'
            Object = Join-Path $intermediate 'Loom.h.pch'
            Depends = Join-Path $intermediate 'Loom.h.d'
        }

        $headerFlags = @($webCompileFlags) + @($webIncludes) + @('-x', 'c++-header')
        $headerStamp = Join-Path $intermediate 'Loom.h.flags'
        $headerStamped = @($headerFlags) + @($emcc, $version)

        if (@(Get-StaleJobs @($header) $headerStamped $headerStamp).Count -gt 0)
        {
            if (-not (Invoke-Compile $emcc @($header) $headerFlags))
            {
                Write-Host 'The engine header failed to precompile.'
                exit 1
            }

            [IO.File]::WriteAllText($headerStamp, $headerStamped -join "`n")
        }

        $scriptJobs = @(Get-CompileJobs $scripts $scriptDir $intermediate)
        $scriptFlags = @($webCompileFlags) + @($webIncludes) + @("-I$scriptDir", '-include-pch', $header.Object, '-Wno-pragma-once-outside-header', '-x', 'c++')
        $scriptStamp = Join-Path $intermediate 'scripts.flags'
        $scriptStamped = @($scriptFlags) + @($emcc, $version)
        $stale = @(Get-StaleJobs $scriptJobs $scriptStamped $scriptStamp)

        if ($stale.Count -gt 0)
        {
            if (-not (Invoke-Compile $emcc $stale $scriptFlags))
            {
                Write-Host 'The scripts failed to build.'
                exit 1
            }

            [IO.File]::WriteAllText($scriptStamp, $scriptStamped -join "`n")
        }
    }

    # The whole archive goes in because components register themselves from
    # static initialisers nothing else refers to. The page preloads the scene
    # through FS, and edits shaders through ccall.
    & (Get-EmscriptenTool $emcc 'em++') @webCompileFlags @webLinkFlags @webIncludes '-sEXPORTED_RUNTIME_METHODS=FS,ccall' $player @($scriptJobs | ForEach-Object { $_.Object }) `
        '-Wl,--whole-archive' $library '-Wl,--no-whole-archive' `
        -o $module

    if ($LASTEXITCODE -ne 0)
    {
        Write-Host "emcc failed with exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}

#endregion

#region Serving

# Served, the page sits beside the build and its base is the root emrun
# serves the project from. Written out, the page and the player sit at the top
# of Output, with the project's files under it as they are under the project.
if ($Output)
{
    $page = Join-Path $Output 'index.html'
    $base = './'
    $script = Split-Path -Leaf $module

    if ((Test-Path -LiteralPath $Output) -and -not (Test-Path -LiteralPath (Join-Path $Output $script)) -and
        (Get-ChildItem -LiteralPath $Output -Force | Select-Object -First 1))
    {
        Write-Host "$Output holds something other than a web build."
        exit 1
    }

    if (-not (Copy-ProjectFiles $Project $Output $scriptDir))
    {
        Write-Host "Could not copy the project into $Output."
        exit 1
    }

    if (-not (Test-Path -LiteralPath (Join-Path $Output (Get-UrlPath $Scene))))
    {
        Write-Host "$Scene is in a folder the web build leaves out."
        exit 1
    }

    Copy-Item -Path (Join-Path $web 'player.*') -Destination $Output
}
else
{
    $page = Join-Path $web 'index.html'
    $base = '/'
    $script = Get-UrlPath $module
}

# Every font and image served but the builds, by its path under the base, for
# the page to preload where a UiText or UiRect opens it.
# -Include is ignored beside -LiteralPath, so the extensions are matched here.
$preloadedTypes = @('.ttf', '.otf', '.ttc', '.png', '.jpg', '.jpeg', '.bmp', '.tga')
$preloaded = @(Get-ChildItem -LiteralPath $served -Recurse -File |
    Where-Object { $preloadedTypes -contains $_.Extension.ToLowerInvariant() -and
        -not $_.FullName.StartsWith((Join-Path $served 'Build\'), [StringComparison]::OrdinalIgnoreCase) } |
    ForEach-Object { $_.FullName.Substring($served.Length).TrimStart('\').Replace('\', '/') } |
    Sort-Object)

# The project's key bindings, beside its .loomproject, or none.
$bindings = if ($projectFile) { [IO.Path]::ChangeExtension($projectFile.FullName, '.loominput') } else { $null }
$bindingsUrl = if ($bindings -and (Test-Path -LiteralPath (Join-Path $served (Get-UrlPath $bindings)))) { Get-UrlPath $bindings } else { '' }

# The project's fixed rate, from its build settings, or the default.
$buildSettings = if ($projectFile) { [IO.Path]::ChangeExtension($projectFile.FullName, '.loombuild') } else { $null }
$fixedRate = if ($buildSettings -and (Test-Path -LiteralPath $buildSettings))
{
    Get-Content -LiteralPath $buildSettings | Where-Object { $_ -like 'fixed_rate=*' } |
        ForEach-Object { $_.Substring('fixed_rate='.Length) } | Select-Object -First 1
}

(Get-Content -LiteralPath (Join-Path $root 'Loom Web\Player.html') -Raw).
    Replace('{TITLE}', (Split-Path -Leaf $Project)).
    Replace('{BASE}', $base).
    Replace('{SCRIPT}', $script).
    Replace('{SCENE}', (Get-UrlPath $Scene)).
    Replace('{FILES}', (ConvertTo-Json -InputObject $preloaded -Compress)).
    Replace('{INPUT}', $bindingsUrl).
    Replace('{FIXED_RATE}', $(if ($fixedRate) { $fixedRate } else { '60' })) |
    Set-Content -LiteralPath $page -Encoding UTF8 -NoNewline

if ($Output)
{
    exit 0
}

# --- Serving ----------------------------------------------------------------

# Port 0 takes whatever is free; emrun prints the one it got.
& $emrun --no_browser --no_emrun_detect --serve_after_close --hostname localhost --port 0 --serve_root $Project $page
exit $LASTEXITCODE

#endregion

