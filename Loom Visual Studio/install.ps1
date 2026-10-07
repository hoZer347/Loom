<#
.SYNOPSIS
    Builds the Loom Visual Studio extension and installs it, adding
    Add > Loom Script... to a Loom project's scripts in Solution Explorer.

.NOTES
    Takes effect the next time Visual Studio starts.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$instance = & $vswhere -latest -prerelease -requires Microsoft.Component.MSBuild -format json | ConvertFrom-Json | Select-Object -First 1

if (-not $instance) { throw 'No Visual Studio with MSBuild is installed.' }

$msbuild = Join-Path $instance.installationPath 'MSBuild\Current\Bin\MSBuild.exe'
$installer = Join-Path $instance.installationPath 'Common7\IDE\VSIXInstaller.exe'
$project = Join-Path $PSScriptRoot 'Loom Visual Studio.csproj'

& $msbuild $project /restore /p:Configuration=Release /nologo /v:minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$vsix = Join-Path $PSScriptRoot 'bin\Release\net472\LoomVisualStudio.vsix'
$install = Start-Process $installer -ArgumentList @('/quiet', "/instanceIds:$($instance.instanceId)", "`"$vsix`"") -PassThru -Wait

exit $install.ExitCode
