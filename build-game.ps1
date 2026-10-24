<#
.SYNOPSIS
    Builds a Loom project into a game of its own, in a folder of its own. The editor's Build Settings tab runs this.

.DESCRIPTION
    Loom Player\Loom Player.vcxproj compiles the project's scripts with the
    engine, in the configuration asked for, under the project's Build folder,
    rebuilding the engine's own libraries first when they are out of date.
    The program and what it needs to run go in the output folder, beside a
    copy of the project's files that leaves out its scripts, its Build folder
    and what only the editor reads, and a <Game>.loomgame naming the scene it
    starts on. The build says nothing unless it fails.

    The output folder is mirrored, so whatever else is in it goes. A folder
    that is not empty and holds no earlier build is left alone, and so is
    one the project is in. An earlier build still running from it is closed,
    since it holds its program open.

.EXAMPLE
    .\build-game.ps1 -Project "C:\Games\MyGame" -Scene "C:\Games\MyGame\Scenes\Start.loomscene" -Configuration Release -Output "C:\Games\MyGame Build"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Project,

    [Parameter(Mandatory = $true)]
    [string] $Scene,

    [ValidateSet('Debug', 'Release', 'Final')]
    [string] $Configuration = 'Release',

    [Parameter(Mandatory = $true)]
    [string] $Output
)

# Native commands are judged by their exit codes: MSBuild's stderr would
# otherwise be raised as a terminating NativeCommandError.
$ErrorActionPreference = 'Continue'
$root = $PSScriptRoot

. (Join-Path $root 'MSBuild.ps1')

$Project = (Resolve-Path -LiteralPath $Project -ErrorAction Stop).Path.TrimEnd('\')
$Scene = (Resolve-Path -LiteralPath $Scene -ErrorAction Stop).Path
$Output = [System.IO.Path]::GetFullPath($Output).TrimEnd('\')

if (-not $Scene.StartsWith("$Project\", [System.StringComparison]::OrdinalIgnoreCase))
{
    Write-Output "$Scene is not in the project."
    exit 1
}

if ("$Project\".StartsWith("$Output\", [System.StringComparison]::OrdinalIgnoreCase))
{
    Write-Output "The project is in $Output, which a build would empty."
    exit 1
}

# Named for the project, and built from the headers beside the script project
# its .loomproject names.
$name = Split-Path -Leaf $Project
$scripts = ''
$projectFile = Get-ChildItem -Path (Join-Path $Project '*.loomproject') | Select-Object -First 1

if ($projectFile)
{
    $lines = Get-Content -LiteralPath $projectFile.FullName
    $nameLine = $lines | Where-Object { $_ -like 'name=*' } | Select-Object -First 1
    $scriptsLine = $lines | Where-Object { $_ -like 'scripts=*' } | Select-Object -First 1

    if ($nameLine)
    {
        $name = $nameLine.Substring('name='.Length)
    }

    if ($scriptsLine)
    {
        $scripts = Split-Path -Parent (Join-Path $Project $scriptsLine.Substring('scripts='.Length))
    }
}

$marker = Join-Path $Output "$name.loomgame"

if ((Test-Path -LiteralPath $Output) -and -not (Test-Path -LiteralPath $marker) -and
    (Get-ChildItem -LiteralPath $Output -Force | Select-Object -First 1))
{
    Write-Output "$Output holds something other than a build of $name."
    exit 1
}

$build = Join-Path $Project 'Build'
$msbuild = Find-MSBuild

# A trailing backslash would escape the quote PowerShell puts round a path
# with a space in it, and a forward slash works as well.
$said = & $msbuild (Join-Path $root 'Loom Player\Loom Player.vcxproj') `
    "/p:Configuration=$Configuration" /p:Platform=x64 "/p:SolutionDir=$root/" `
    "/p:GameScripts=$scripts" "/p:GameBuild=$build" "/p:GameName=$name" `
    /nologo /v:quiet /m /nodeReuse:false 2>&1

if ($LASTEXITCODE -ne 0)
{
    $said | ForEach-Object { Write-Output "$_" }
    Write-Output "The $Configuration build of $name failed."
    exit 1
}

# The scripts are in the program, and the rest is the editor's.
$folders = @($build, '.*')
$files = @('*.loomproject', '*.loomscripts', '*.loomsounds', '*.loomfonts', '*.loombuild', '*.loomgame',
    '*.ps1', '*.py', '*.md', '*.ini', '*.log', '.*', '*.exe', '*.dll', '*.pdb')

if ($scripts)
{
    $folders += $scripts
}

if ($Output.StartsWith("$Project\", [System.StringComparison]::OrdinalIgnoreCase))
{
    $folders += $Output
}

$game = Join-Path $Output "$name.exe"
$running = Get-Process -Name $name -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $game }

$closeSeconds = 10

if ($running)
{
    $running | Stop-Process -Force -ErrorAction SilentlyContinue
    $running | Wait-Process -Timeout $closeSeconds -ErrorAction SilentlyContinue

    if ($running | Where-Object { -not $_.HasExited })
    {
        Write-Output "$game is running and could not be closed."
        exit 1
    }
}

# Below 8, robocopy's exit code says what it copied rather than that it failed.
$robocopyFailed = 8

$said = robocopy $Project $Output /MIR /XD $folders /XF $files /NJH /NJS /NP /NFL /NDL 2>&1

if ($LASTEXITCODE -ge $robocopyFailed)
{
    $said | ForEach-Object { Write-Output "$_" }
    Write-Output "Could not copy $name into $Output."
    exit 1
}

$program = Join-Path $build "Player\$Configuration"

try
{
    Copy-Item -LiteralPath (Join-Path $program "$name.exe"), (Join-Path $program 'glew32.dll') -Destination $Output -ErrorAction Stop

    # The player reads it as key=value lines, so a path with a space keeps it.
    $start = $Scene.Substring($Project.Length + 1).Replace('\', '/')
    [System.IO.File]::WriteAllText($marker, "scene=$start`n")
}
catch
{
    Write-Output "Could not put $name in ${Output}: $($_.Exception.Message)"
    exit 1
}

exit 0
