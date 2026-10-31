<#
.SYNOPSIS
    Rebuilds the fixture and installs it, as update-editor.ps1 asks of an extension.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $LoomRoot
)

$ErrorActionPreference = 'Stop'

$folder = Join-Path $env:LOCALAPPDATA 'Loom\FixtureBuild'

. (Join-Path $LoomRoot 'MSBuild.ps1')
$msbuild = Find-MSBuild

& $msbuild (Join-Path $PSScriptRoot 'Reload Fixture.vcxproj') "-p:OutDir=$folder\" "-p:IntDir=$folder\Intermediate\" -p:Configuration=Release -p:Platform=x64 -nologo -v:m -m -nr:false
if ($LASTEXITCODE -ne 0) { throw 'Building the fixture failed.' }

Copy-Item -LiteralPath (Join-Path $folder 'Reload Fixture.dll') -Destination (Join-Path $env:LOCALAPPDATA 'Loom\Extensions') -Force
