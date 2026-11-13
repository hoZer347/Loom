<#
.SYNOPSIS
    Checks that a project loads the extensions its .loomproject names.

.DESCRIPTION
    Opens this slot's Release editor on a Win32 desktop of its own, with
    LOCALAPPDATA and TEMP moved to a scratch folder. The extensions folder holds
    the fixture, and the scratch projects name a second build of it with
    extension= lines. A relative line and an absolute one must load it, its panel
    must be in the Window menu, its File item and its own menu must be drawn, and
    choosing its File item must reach it. A line for a file that is not there must
    say so on the console. A panel left open must come back open when the editor
    is opened on the project again, a project without the line must have none of
    the project extension's panels or menu items, and the extension in the
    extensions folder must stay through each switch.
    Throws naming what failed.
    A slot only: the user's Loom refuses, since its settings hold what they
    left open.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File 'C:\Users\3hoze\Desktop\Loom 1\Loom Tests\project-extensions.ps1'
#>
$ErrorActionPreference = 'Stop'

#region Setup

$Root = Split-Path $PSScriptRoot
$Editor = Join-Path $Root 'x64\Release\Loom Editor.exe'
$FixtureSource = Join-Path $PSScriptRoot 'Extension Fixture'
$Fixture = Join-Path $FixtureSource 'Reload Fixture.vcxproj'
$Scratch = Join-Path ([IO.Path]::GetTempPath()) "loom-project-extensions-$PID"
$AppData = Join-Path $Scratch 'AppData'
$TempFolder = Join-Path $Scratch 'Temp'
$Extensions = Join-Path $AppData 'Loom\Extensions'
$Projects = Join-Path $Scratch 'Projects'
$Elsewhere = Join-Path $Scratch 'Elsewhere'
$StartSeconds = 60
$CompileSeconds = 240
$PollMilliseconds = 250
$SettleSeconds = 2
$ToolSeconds = 15
$Utf8 = [Text.Encoding]::UTF8
$ReplyEncoding = [Text.Encoding]::GetEncoding('iso-8859-1')
$MainMenuBar = '##MainMenuBar'
$MenuWindow = '##Menu_00'
$OpenWindowMenu = "click `"$MainMenuBar`" `"Window`""
$CloseMenu = 'key escape'
$GlobalPanel = 'F Reload Fixture###Reload Fixture'
$ProjectPanel = 'F Project Fixture###Project Fixture'
$GlobalLatePanel = 'F Fixture Late Panel###Fixture Late Panel'
$ProjectLatePanel = 'F Project Late Panel###Project Late Panel'
$ProjectDll = 'Project Fixture.dll'
$RelativeLine = "Extension/$ProjectDll"
$MissingLine = 'Extension/Nope.dll'
$GlobalFileItem = 'Fixture File Item'
$ProjectFileItem = 'Project File Item'
$ProjectMenu = 'Project Menu'
$ProjectCustomItem = 'Project Custom Item'
$ProjectEditorMenuItem = 'Project Editor Menu Item'
$EditorMenus = 'Edit', 'GameObject', 'Window'
$ProjectFileUsed = 'Project File Item Used'
$ProjectCustomUsed = 'Project Custom Item Used'
$OpenDialog = 'Open Project##filebrowser'
$NotFound = 'Could not find the extension .*Nope\.dll'

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

function Toggle-Panel([string] $Panel) { Invoke-Ui "$OpenWindowMenu`nclick `"$MenuWindow`" `"$Panel`"" }

# Opened last, so its tab is the one in front and it draws.
function Open-Panel([string] $Panel)
{
    if (Test-PanelOpen $Panel) { Toggle-Panel $Panel }
    Toggle-Panel $Panel
    if (-not (Test-PanelOpen $Panel)) { throw "$Panel did not open." }
}

function Get-ConsoleText { (Invoke-Tool 'console') -join "`n" }

function Build-Fixture([string] $Name, [bool] $ProjectVariant)
{
    $folder = Join-Path $Scratch "Fixture\$Name"
    Invoke-Build "the $Name fixture" @($Fixture, "-p:OutDir=$folder\", "-p:IntDir=$folder\Intermediate\", "-p:ProjectVariant=$($ProjectVariant.ToString().ToLower())")
    Join-Path $folder 'Reload Fixture.dll'
}

function New-Project([string] $Name, [string[]] $Lines)
{
    $folder = Join-Path $Projects $Name
    New-Item -ItemType Directory -Force $folder | Out-Null
    $file = Join-Path $folder "$Name.loomproject"
    Set-Content -LiteralPath $file -Value (@("name=$Name") + $Lines) -Encoding ascii
    $file
}

function Start-Editor([string] $ProjectFile)
{
    [OwnDesktop]::Start("Loom project extensions $port", $Editor, "`"$Editor`" --extensions `"$Extensions`" --mcp $port `"$ProjectFile`"", (Split-Path $ProjectFile))
}

function Get-ProjectName
{
    if ((Invoke-Tool 'state') -match 'Project: ([^\r\n]*)') { Split-Path $Matches[1] -Leaf }
}

function Wait-Project([string] $Name)
{
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $CompileSeconds)
    {
        if ((Get-ProjectName) -eq $Name) { return }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    throw "The editor did not open the project $Name within $CompileSeconds seconds."
}

# The first label's items of the menu bar's entry, read while its menu is open.
function Open-Menu([string] $Menu) { Invoke-Ui "click `"$MainMenuBar`" `"$Menu`"" }

function Test-MenuHas([string] $Menu, [string] $Item)
{
    Open-Menu $Menu
    $found = Test-Item $Item
    Invoke-Ui $CloseMenu
    $found
}

function Test-BarHas([string] $Menu) { Test-Item $Menu }

function Test-Listed([string] $Panel) { Test-MenuHas 'Window' $Panel }

function Click-Like([string] $Pattern)
{
    $item = Get-Items | Where-Object { ($_ -split "`t")[1] -match $Pattern } | Select-Object -First 1
    if (-not $item) { throw "Nothing is labelled like $Pattern." }
    $f = $item -split "`t"
    Invoke-Ui ("click {0} {1}" -f ([double]$f[2] + [double]$f[4] / 2), ([double]$f[3] + [double]$f[5] / 2))
}

function Switch-Project([string] $Name)
{
    Invoke-Ui "click `"$MainMenuBar`" `"File`""
    Click-Like 'Open Project\.\.\.$'
    Invoke-Ui "doubleclick `"$OpenDialog`" `"[D] $Name`""
    Invoke-Ui "click `"$OpenDialog`" `"[F] $Name.loomproject`""
    Invoke-Ui "click `"$OpenDialog`" `" ok `""
    Wait-Project $Name
}

function Invoke-EditorCommand([string] $Command)
{
    if ((Invoke-Tool 'editors') -notmatch 'Editor (\d+)') { throw 'No editor is listed.' }
    Invoke-Tool 'command' @{ command = $Command; editor = $Matches[1] } | Out-Null
}

function Assert-Loaded([string] $When, [bool] $Project)
{
    $listed = Test-MenuHas 'Window' $ProjectPanel
    Assert-That ($listed -eq $Project) "$When, the Window menu $(if ($listed) { 'has' } else { 'lacks' }) the project extension's panel."
    Assert-That ((Test-MenuHas 'File' $ProjectFileItem) -eq $Project) "$When, the File menu's project item was not as expected."
    Assert-That ((Test-BarHas $ProjectMenu) -eq $Project) "$When, the project extension's menu was not as expected."
    foreach ($menu in $EditorMenus) { Assert-That ((Test-MenuHas $menu $ProjectEditorMenuItem) -eq $Project) "$When, the $menu menu's project item was not as expected." }

    if (-not $Project) { Assert-That (-not (Test-MenuHas 'Window' $ProjectLatePanel)) "$When, a panel the project extension added after it started is still in the Window menu." }

    Assert-That (Test-MenuHas 'Window' $GlobalPanel) "$When, the extensions folder's panel is gone."
    Assert-That (Test-MenuHas 'File' $GlobalFileItem) "$When, the extensions folder's File item is gone."
    Assert-That (Test-BarHas 'Fixture Menu') "$When, the extensions folder's menu is gone."
}

function Assert-That([bool] $Condition, [string] $Message)
{
    if (-not $Condition) { throw "$Message Console: $(Get-ConsoleText)" }
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

New-Item -ItemType Directory $Extensions, $TempFolder, $Projects, $Elsewhere | Out-Null

try
{
    Invoke-Build 'the Release editor' @((Join-Path $Root 'Loom.sln'), '-t:Loom Editor')

    Copy-Item -LiteralPath (Build-Fixture 'Global' $false) -Destination $Extensions
    $projectBuild = Build-Fixture 'Project' $true

    $relative = New-Project 'Relative' @("extension=$RelativeLine", "extension=$MissingLine")
    New-Item -ItemType Directory -Force (Join-Path (Split-Path $relative) 'Extension') | Out-Null
    Copy-Item -LiteralPath $projectBuild -Destination (Join-Path (Split-Path $relative) "Extension\$ProjectDll")

    $absoluteDll = Join-Path $Elsewhere $ProjectDll
    Copy-Item -LiteralPath $projectBuild -Destination $absoluteDll
    New-Project 'Absolute' @("extension=$absoluteDll") | Out-Null
    New-Project 'Plain' @() | Out-Null

    $env:LOCALAPPDATA = $AppData
    $env:TEMP = $TempFolder
    $env:TMP = $TempFolder

    $first = Start-Editor $relative
    Wait-Editor { param($id) $id -eq $first } 'editor answering' | Out-Null

    Assert-Loaded 'Relative project' $true
    Assert-That ((Get-ConsoleText) -match $NotFound) 'A line for a file that is not there did not say so.'

    Invoke-Ui "click `"$MainMenuBar`" `"File`"`nclick `"File`" `"$ProjectFileItem`""
    Open-Panel $ProjectPanel
    Assert-That (Test-Item $ProjectFileUsed) 'Choosing the project extension''s File item did not reach it.'

    Invoke-Ui "click `"$MainMenuBar`" `"$ProjectMenu`"`nclick `"$ProjectMenu`" `"$ProjectCustomItem`""
    Assert-That (Test-Item $ProjectCustomUsed) 'Choosing the project extension''s own menu item did not reach it.'

    Invoke-EditorCommand 'update'
    $second = Wait-Editor { param($id) $id -ne $first } 'restart on the project'
    Assert-Loaded 'Restarted project' $true
    Assert-That (Test-PanelOpen $ProjectPanel) 'The project extension''s panel did not come back open.'

    Switch-Project 'Absolute'
    Assert-Loaded 'Absolute project' $true
    Assert-That (Test-PanelOpen $ProjectPanel) 'A project extension''s panel did not keep its shown state across projects.'

    Invoke-Ui "click `"$MainMenuBar`" `"File`"`nclick `"File`" `"$GlobalFileItem`""
    Invoke-Ui "click `"$MainMenuBar`" `"File`"`nclick `"File`" `"$ProjectFileItem`""
    Assert-That (Test-MenuHas 'Window' $GlobalLatePanel) 'Choosing the extensions folder''s File item did not add its late panel.'
    Assert-That (Test-MenuHas 'Window' $ProjectLatePanel) 'Choosing the project extension''s File item did not add its late panel.'

    Switch-Project 'Plain'
    Assert-Loaded 'Plain project' $false
    Assert-That (Test-MenuHas 'Window' $GlobalLatePanel) 'Unloading the project extension took the extensions folder''s late panel with it.'

    Invoke-EditorCommand 'update'
    Wait-Editor { param($id) $id -ne $second } 'restart on the plain project' | Out-Null
    Assert-Loaded 'Restarted plain project' $false

    Switch-Project 'Relative'
    Assert-Loaded 'Relative project again' $true
    Assert-That (Test-PanelOpen $ProjectPanel) 'A panel unloaded across a restart lost the state the layout held.'

    Write-Output 'project-extensions passed.'
}
finally
{
    Stop-Started
    Remove-Item -LiteralPath $Scratch -Recurse -Force -ErrorAction SilentlyContinue
}

#endregion
