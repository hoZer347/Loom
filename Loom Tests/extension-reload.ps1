<#
.SYNOPSIS
    Checks that the extensions in the user's folder are still there after an update.

.DESCRIPTION
    Opens this slot's Release editor on a Win32 desktop of its own, with
    LOCALAPPDATA and TEMP moved to a scratch folder, on a scratch project. The
    folder holds a fixture built against this editor. Its panel opens and works.
    The fixture is then replaced by the same source as an extension built
    before the editor's API last changed has it, which is what a merge leaves in
    the folder.
    An extension that cannot rebuild must fail update-editor.ps1 and leave the
    editor open, and an editor restarted on it anyway must say it is out of date.
    One for another Loom must be left alone. update-editor.ps1 then rebuilds both
    and restarts the editor, keeping --extensions. Every panel must come back and
    work, and the console must not say an extension was left out. Ctrl+Shift+R's
    refresh must do the same for extensions older than the editor.
    Throws naming what failed.
    A slot only: the user's Loom refuses, since its settings hold what they
    left open.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File 'C:\Users\3hoze\Desktop\Loom 1\Loom Tests\extension-reload.ps1'
#>
$ErrorActionPreference = 'Stop'

#region Setup

$Root = Split-Path $PSScriptRoot
$Editor = Join-Path $Root 'x64\Release\Loom Editor.exe'
$UpdateScript = Join-Path $Root 'update-editor.ps1'
$FixtureSource = Join-Path $PSScriptRoot 'Extension Fixture'
$Fixture = Join-Path $FixtureSource 'Reload Fixture.vcxproj'
$Scratch = Join-Path ([IO.Path]::GetTempPath()) "loom-extension-reload-$PID"
$AppData = Join-Path $Scratch 'AppData'
$TempFolder = Join-Path $Scratch 'Temp'
$Extensions = Join-Path $AppData 'Loom\Extensions'
$Project = Join-Path $Scratch 'Project'
$Refuses = Join-Path $Scratch 'Refuses'
$ManifestExtension = '.extension.json'
$StartSeconds = 60
$CompileSeconds = 240
$PollMilliseconds = 250
$SettleSeconds = 2
$ToolSeconds = 15
$Utf8 = [Text.Encoding]::UTF8
$ReplyEncoding = [Text.Encoding]::GetEncoding('iso-8859-1')
$OpenWindowMenu = 'click "##MainMenuBar" "Window"'
$MenuWindow = '##Menu_00'
$CloseMenu = 'key escape'
$FixturePanel = 'F Reload Fixture###Reload Fixture'
$FixtureWaiting = 'Fixture Waiting'
$FixtureClicked = 'Fixture Clicked'
$FixtureFile = 'Reload Fixture.dll'
$LeftOut = 'Could not (load|copy) the extension|Skipped the extension'
$OutOfDate = 'Could not load the extension Reload Fixture\.dll \(it was built against another version of the editor\)'
$KeepsExtensions = '*--extensions*'

$common = & git -C $Root rev-parse --path-format=absolute --git-common-dir
if ([IO.Path]::GetFullPath((Split-Path $common -Parent)) -eq [IO.Path]::GetFullPath($Root)) { throw "$Root is the user's Loom. Run a slot's copy." }

. (Join-Path $Root 'MSBuild.ps1')
$msbuild = Find-MSBuild

Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public static class OwnDesktop
{
	[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
	struct StartupInfo
	{
		public int size;
		public string reserved;
		public string desktop;
		public string title;
		public int x, y, width, height, columns, rows, fill, flags;
		public short show, reservedSize;
		public IntPtr reservedBytes, input, output, error;
	}

	[StructLayout(LayoutKind.Sequential)]
	struct ProcessInformation
	{
		public IntPtr process, thread;
		public int id, threadId;
	}

	const uint AllAccess = 0x10000000;

	[DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
	static extern IntPtr CreateDesktopW(string name, IntPtr device, IntPtr mode, int flags, uint access, IntPtr security);

	[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
	static extern bool CreateProcessW(string application, StringBuilder line, IntPtr processSecurity, IntPtr threadSecurity, bool inherit, int flags, IntPtr environment, string folder, ref StartupInfo startup, out ProcessInformation process);

	public static int Start(string name, string application, string line, string folder)
	{
		if (CreateDesktopW(name, IntPtr.Zero, IntPtr.Zero, 0, AllAccess, IntPtr.Zero) == IntPtr.Zero)
			throw new Win32Exception();

		StartupInfo startup = new StartupInfo();
		startup.size = Marshal.SizeOf(startup);
		startup.desktop = name;

		ProcessInformation process;

		if (!CreateProcessW(application, new StringBuilder(line), IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, folder, ref startup, out process))
			throw new Win32Exception();

		return process.id;
	}
}
'@

#endregion

#region Helpers

function Invoke-Build([string] $What, [string[]] $Arguments)
{
    $output = & $msbuild @Arguments -p:Configuration=Release -p:Platform=x64 -nologo -v:m -m -nr:false 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Building $What failed:`n$($output -join "`n")" }
}

function Build-Fixture([string] $Name, [bool] $OldApi)
{
    $folder = Join-Path $Scratch "Fixture\$Name"
    Invoke-Build "the $Name fixture" @($Fixture, "-p:OutDir=$folder\", "-p:IntDir=$folder\Intermediate\", "-p:OldApi=$($OldApi.ToString().ToLower())")
    Join-Path $folder $FixtureFile
}

function Set-Manifest([string] $Name, [string] $Source, [string] $LoomRoot)
{
    @{ Source = $Source; LoomRoot = $LoomRoot } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $Extensions "$Name$ManifestExtension") -Encoding UTF8
}

function Set-OlderThanEditor([string] $File)
{
    (Get-Item -LiteralPath (Join-Path $Extensions $File)).LastWriteTimeUtc = (Get-Item -LiteralPath $Editor).LastWriteTimeUtc.AddMinutes(-1)
}

function Invoke-Update
{
    # A native command's stderr would stop this script before its text can be read.
    $ErrorActionPreference = 'Continue'
    (& powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$UpdateScript' -Port $port -Project '$Project' -Extensions '$Extensions'" 2>&1) -join "`n"
}

function Invoke-EditorCommand([string] $Command)
{
    if ((Invoke-Tool 'editors') -notmatch 'Editor (\d+)') { throw 'No editor is listed.' }
    Invoke-Tool 'command' @{ command = $Command; editor = $Matches[1] } | Out-Null
}

function Assert-Current([string[]] $Files)
{
    $built = (Get-Item -LiteralPath $Editor).LastWriteTimeUtc
    $older = $Files | Where-Object { (Get-Item -LiteralPath (Join-Path $Extensions $_)).LastWriteTimeUtc -lt $built }
    if ($older) { throw "$($older -join ', ') stayed older than the editor." }
}

function Invoke-Tool([string] $Tool, [hashtable] $Arguments = @{})
{
    $body = @{ jsonrpc = '2.0'; id = 1; method = 'tools/call'; params = @{ name = $Tool; arguments = $Arguments } } | ConvertTo-Json -Depth 4
    $reply = Invoke-RestMethod -Uri "http://127.0.0.1:$port/mcp" -Method Post -ContentType 'application/json' -Headers @{ Accept = 'application/json' } `
        -Body ($Utf8.GetBytes($body)) -TimeoutSec $ToolSeconds
    if ($reply.error -or $reply.result.isError) { throw "$Tool failed: $($reply | ConvertTo-Json -Depth 6 -Compress)" }
    $Utf8.GetString($ReplyEncoding.GetBytes($reply.result.content[0].text))
}

function Get-EditorId
{
    $state = try { Invoke-Tool 'state' } catch { '' }
    if ($state -match 'Process: (\d+)') { [int] $Matches[1] }
}

function Wait-Editor([scriptblock] $Until, [string] $What, [int] $Seconds = $StartSeconds)
{
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $Seconds)
    {
        $found = Get-EditorId
        if ($found -and (& $Until $found)) { return $found }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    throw "No $What within $Seconds seconds."
}

function Wait-Compiled
{
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $CompileSeconds)
    {
        $state = Invoke-Tool 'state'
        if ($state -match 'Compile failed') { throw "The scripts failed to compile: $state" }
        if ($state -match 'Scripts: Loaded') { return }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    throw "No compile within $CompileSeconds seconds: $state"
}

function Invoke-Ui([string] $Script)
{
    Invoke-Tool 'ui' @{ script = $Script } | Out-Null
    Start-Sleep -Seconds $SettleSeconds
}

function Get-Items { (Invoke-Tool 'ui_items') -split "`r?`n" }

function Test-Item([string] $Label) { @(Get-Items | Where-Object { (($_ -split "`t")[1] -eq $Label) -or ((($_ -split "`t")[1] -split '##')[0] -eq $Label) }).Count -gt 0 }

function Test-PanelOpen([string] $Panel) { @(Get-Items | Where-Object { ($_ -split "`t")[0] -eq $Panel }).Count -gt 0 }

function Assert-Listed([string[]] $Panels, [string] $When)
{
    Invoke-Ui $OpenWindowMenu
    $missing = $Panels | Where-Object { -not (Test-Item $_) }
    Invoke-Ui $CloseMenu
    if ($missing) { throw "$When, the Window menu has no $($missing -join ', '). Console: $(Get-ConsoleText)" }
}

function Toggle-Panel([string] $Panel) { Invoke-Ui "$OpenWindowMenu`nclick `"$MenuWindow`" `"$Panel`"" }

# Opened last, so its tab is the one in front and it draws.
function Open-Panel([string] $Panel)
{
    if (Test-PanelOpen $Panel) { Toggle-Panel $Panel }
    Toggle-Panel $Panel
    if (-not (Test-PanelOpen $Panel)) { throw "$Panel did not open." }
}

function Assert-StillOpen([string[]] $Panels)
{
    $closed = $Panels | Where-Object { -not (Test-PanelOpen $_) }
    if ($closed) { throw "The update closed $($closed -join ', ')." }
}

function Get-ConsoleText { (Invoke-Tool 'console') -join "`n" }

function Assert-NothingLeftOut([string] $When)
{
    $console = Get-ConsoleText
    if ($console -match $LeftOut) { throw "$When, an extension was left out: $console" }
}

function Assert-FixtureWorks([string] $When)
{
    Open-Panel $FixturePanel
    if (-not (Test-Item $FixtureWaiting)) { throw "$When, the fixture's panel drew nothing." }
    Invoke-Ui "click `"$FixturePanel`" `"$FixtureWaiting`""
    if (-not (Test-Item $FixtureClicked)) { throw "$When, the fixture's button did nothing." }
}

function Stop-Started
{
    $started = @(Get-CimInstance Win32_Process -Filter "Name = 'Loom Editor.exe'" | Where-Object { $_.ExecutablePath -eq $Editor -and $_.CommandLine -like "*--mcp $port*" })
    foreach ($process in $started) { Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue }
    foreach ($process in $started) { Wait-Process -Id $process.ProcessId -Timeout $ToolSeconds -ErrorAction SilentlyContinue }
}

#endregion

#region Run

$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()

New-Item -ItemType Directory $Extensions, $TempFolder, $Project, $Refuses | Out-Null
Set-Content -LiteralPath (Join-Path $Refuses 'update-extension.ps1') -Value 'exit 1' -Encoding ascii
$projectFile = Join-Path $Project 'Scratch.loomproject'
Set-Content -LiteralPath $projectFile -Value 'name=Scratch' -Encoding ascii

try
{
    Invoke-Build 'the Release editor' @((Join-Path $Root 'Loom.sln'), '-t:Loom Editor')

    $current = Build-Fixture 'Current' $false
    $old = Build-Fixture 'Old' $true
    Copy-Item -LiteralPath $current -Destination $Extensions

    $env:LOCALAPPDATA = $AppData
    $env:TEMP = $TempFolder
    $env:TMP = $TempFolder

    $first = [OwnDesktop]::Start("Loom extension reload $port", $Editor, "`"$Editor`" --extensions `"$Extensions`" --mcp $port `"$projectFile`"", $Project)

    Wait-Editor { param($id) $id -eq $first } 'editor answering' | Out-Null
    Wait-Compiled

    Assert-NothingLeftOut 'Before the update'
    Assert-FixtureWorks 'Before the update'

    Copy-Item -LiteralPath $old -Destination $Extensions -Force
    Set-OlderThanEditor $FixtureFile

    $fixtureName = [IO.Path]::GetFileNameWithoutExtension($FixtureFile)
    Set-Manifest $fixtureName $Refuses $Root
    $refused = Invoke-Update
    if ($LASTEXITCODE -eq 0 -or $refused -notmatch 'failed to build') { throw "An extension that cannot rebuild did not stop the update: $refused" }
    if ($refused -match 'told to') { throw "An extension that cannot rebuild still told the editor: $refused" }
    if ((Get-EditorId) -ne $first) { throw 'An extension that cannot rebuild still restarted the editor.' }

    Invoke-EditorCommand 'update'
    $second = Wait-Editor { param($id) $id -ne $first } 'restart on the out of date extension'
    Wait-Compiled
    if ((Get-ConsoleText) -notmatch $OutOfDate) { throw "The console did not say the extension is out of date: $(Get-ConsoleText)" }

    Set-Manifest $fixtureName $FixtureSource $Root
    Set-Manifest 'Another Loom' $Refuses (Join-Path $Scratch 'Another Loom')

    $told = Invoke-Update
    if ($LASTEXITCODE -ne 0 -or $told -notmatch 'told to update') { throw "The editor was not told to update: $told" }
    $files = @($FixtureFile)
    Assert-Current $files

    $latest = Wait-Editor { param($id) $id -ne $second } 'restart'
    Wait-Compiled

    $line = (Get-CimInstance Win32_Process -Filter "ProcessId = $latest").CommandLine
    if ($line -notlike $KeepsExtensions) { throw "The restart dropped --extensions: $line" }

    Assert-NothingLeftOut 'After the update'
    Assert-Listed @($FixturePanel) 'After the update'
    Assert-FixtureWorks 'After the update'

    $files | ForEach-Object { Set-OlderThanEditor $_ }
    Invoke-EditorCommand 'refresh'
    Wait-Editor { param($id) $id -ne $latest } 'restart after the refresh' $CompileSeconds | Out-Null
    Wait-Compiled

    Assert-Current $files
    Assert-NothingLeftOut 'After the refresh'
    Assert-StillOpen @($FixturePanel)
    Assert-Listed @($FixturePanel) 'After the refresh'
    Assert-FixtureWorks 'After the refresh'

    Write-Output 'extension-reload passed.'
}
finally
{
    Stop-Started
    Remove-Item -LiteralPath $Scratch -Recurse -Force -ErrorAction SilentlyContinue
}

#endregion
