<#
.SYNOPSIS
    Builds a Loom project into a program of its own and runs it. The editor's Build menu runs this.

.DESCRIPTION
    Loom Player\Loom Player.vcxproj compiles the project's scripts with the
    engine, in the configuration asked for, into Build\<Configuration> beside
    the scene the editor wrote there, rebuilding the engine's own libraries
    first when they are out of date. The build says nothing unless it fails.

    The program runs from the project folder, as the editor does, and this
    waits for it to end, so whatever ends this ends the game with it.

.EXAMPLE
    .\play-native.ps1 -Project "C:\Games\MyGame" -Scene "C:\Games\MyGame\Build\Release\Scene.loomscene" -Configuration Release

.EXAMPLE
    .\play-native.ps1 -Project "C:\Games\MyGame" -Scene "C:\Games\MyGame\Build\Final\Scene.loomscene" -Configuration Final -GameArgs '--screenshot','C:\Temp\game.bmp','300'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Project,

    [Parameter(Mandatory = $true)]
    [string] $Scene,

    [ValidateSet('Debug', 'Release', 'Final')]
    [string] $Configuration = 'Release',

    # Handed to the game after the scene.
    [string[]] $GameArgs = @()
)

# Native commands are judged by their exit codes: MSBuild's stderr would
# otherwise be raised as a terminating NativeCommandError.
$ErrorActionPreference = 'Continue'
$root = $PSScriptRoot

. (Join-Path $root 'MSBuild.ps1')

$Project = (Resolve-Path -LiteralPath $Project -ErrorAction Stop).Path
$Scene = (Resolve-Path -LiteralPath $Scene -ErrorAction Stop).Path

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

$game = Join-Path $build "$Configuration\$name.exe"

# Start-Process joins its arguments with spaces and quotes none of them.
$arguments = @($Scene) + $GameArgs | ForEach-Object { '"' + $_ + '"' }

$process = Start-Process -FilePath $game -ArgumentList $arguments -WorkingDirectory $Project -PassThru -Wait

exit $process.ExitCode
