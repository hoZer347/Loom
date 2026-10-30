<#
.SYNOPSIS
    Brings the Release editor of the copy it sits in up to master, and tells
    the editors left open to take it.

.DESCRIPTION
    The editor is rebuilt when master moved there after the build. A running
    editor is moved aside rather than blocking the link, the way its own full
    refresh does. One run at a time; a second waits, then checks again.

    Each editor open from this copy is then told over MCP what to do: update,
    which stops its run, saves what is unsaved and restarts it on the new build
    once no game or page is building, or update-scripts, which stops its run
    and compiles over unsaved work. An editor on -Project is told whatever -ScriptsOnly says,
    and any other only when the build is newer than it. Demos and hidden runs
    are left alone.

    Exits non-zero if the build fails or an open editor cannot be reached.

.PARAMETER Project
    The project the merge landed on, when it was not the engine's.

.PARAMETER ScriptsOnly
    With -Project, the merge changed only that project's scripts.

.PARAMETER BuildOnly
    Rebuilds and tells nobody.

.PARAMETER Port
    The MCP port the editors share.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File 'C:\Users\3hoze\Desktop\Loom\update-editor.ps1' -Project 'C:\Users\3hoze\Desktop\RTS' -ScriptsOnly
#>
[CmdletBinding()]
param(
    [string] $Project,
    [switch] $ScriptsOnly,
    [switch] $BuildOnly,
    [int] $Port = 47800
)

$ErrorActionPreference = 'Stop'

$Configuration = 'Release'
$Output = Join-Path $PSScriptRoot "x64\$Configuration"
$Editor = Join-Path $Output 'Loom Editor.exe'
$Locked = @($Editor, (Join-Path $Output 'Loom Editor.pdb'))
$AsideExtension = '.aside'
$AutomatedFlags = '--play', '--screenshot', '--ui-script'
$McpTimeoutSeconds = 15

function Update-Build
{
    $mutex = [Threading.Mutex]::new($false, 'Global\LoomUpdateEditor')
    # A run killed while holding it leaves it abandoned, and this one owns it anyway.
    try { [void]$mutex.WaitOne() } catch [Threading.AbandonedMutexException] { }

    try
    {
        # Left by earlier runs; the ones a running editor still holds stay until it closes.
        Get-ChildItem $Output -Filter "*$AsideExtension" -ErrorAction SilentlyContinue |
            ForEach-Object { try { Remove-Item -LiteralPath $_.FullName } catch { } }

        # When master moved here; a branch merged later carries older commit dates.
        $moved = & git -C $PSScriptRoot reflog -1 --format=%gd --date=unix master
        if ($LASTEXITCODE -ne 0 -or $moved -notmatch '\{(\d+)\}') { throw "Could not read when master last moved in $PSScriptRoot." }

        $updated = [DateTimeOffset]::FromUnixTimeSeconds([long]$Matches[1]).UtcDateTime
        $built = if (Test-Path $Editor) { (Get-Item $Editor).LastWriteTimeUtc } else { [DateTime]::MinValue }

        if ($updated -le $built)
        {
            Write-Output "$Editor is up to date."
            return
        }

        $msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease `
            -find 'MSBuild\**\Bin\amd64\MSBuild.exe' | Select-Object -First 1
        if (-not $msbuild) { throw 'No MSBuild found.' }

        # A running exe can be renamed but not written. The copy put back keeps
        # its timestamps, so the link sees it as the old build and replaces it.
        $asides = @{}

        foreach ($file in $Locked | Where-Object { Test-Path $_ })
        {
            $asides[$file] = "$file.$([DateTime]::UtcNow.Ticks)$AsideExtension"
            Move-Item -LiteralPath $file -Destination $asides[$file]
            Copy-Item -LiteralPath $asides[$file] -Destination $file
        }

        Write-Output "Building $Editor"
        & $msbuild (Join-Path $PSScriptRoot 'Loom.sln') '-t:Loom Editor' "-p:Configuration=$Configuration" -p:Platform=x64 -nologo -v:m '-clp:ErrorsOnly;WarningsOnly;Summary' -m -nr:false
        $result = $LASTEXITCODE

        # A failed link can leave nothing behind; the last good build goes back.
        foreach ($file in $asides.Keys | Where-Object { -not (Test-Path $_) })
        {
            Copy-Item -LiteralPath $asides[$file] -Destination $file
        }

        if ($result -ne 0) { throw "$Editor failed to build." }
    }
    finally
    {
        $mutex.ReleaseMutex()
    }
}

# The tool's text, or a throw naming what it answered instead.
function Invoke-EditorTool([string] $Tool, [hashtable] $Arguments = @{})
{
    $body = @{ jsonrpc = '2.0'; id = 1; method = 'tools/call'; params = @{ name = $Tool; arguments = $Arguments } } | ConvertTo-Json -Depth 4
    $reply = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/mcp" -Method Post -ContentType 'application/json' `
        -Headers @{ Accept = 'application/json' } -Body $body -TimeoutSec $McpTimeoutSeconds

    if ($reply.error -or $reply.result.isError) { throw "The editor refused $Tool`: $($reply | ConvertTo-Json -Depth 6 -Compress)" }

    $reply.result.content[0].text
}

function Get-OpenEditors
{
    Get-CimInstance Win32_Process -Filter "Name = 'Loom Editor.exe'" | Where-Object {
        $line = $_.CommandLine
        $_.ExecutablePath -eq $Editor -and
            -not ($AutomatedFlags | Where-Object { $line -like "*$_*" })
    }
}

function Send-Updates
{
    $open = @(Get-OpenEditors)

    if (-not $open)
    {
        Write-Output 'No editor is open. The next one opened builds what it needs.'
        return
    }

    $built = (Get-Item $Editor).LastWriteTimeUtc
    $listing = Invoke-EditorTool 'editors'

    foreach ($process in $open)
    {
        $block = [regex]::Match($listing, "Editor (\d+)\r?\nProcess: $($process.ProcessId)\r?\nProject: ([^\r\n]*)")
        if (-not $block.Success) { throw "Editor $($process.ProcessId) is older than the update commands, so it needs one restart by hand." }

        $id = $block.Groups[1].Value
        $onProject = $Project -and ([IO.Path]::GetFullPath($block.Groups[2].Value) -eq [IO.Path]::GetFullPath($Project))
        $stale = $process.CreationDate.ToUniversalTime() -lt $built

        $command = if ($stale -or ($onProject -and -not $ScriptsOnly)) { 'update' } elseif ($onProject) { 'update-scripts' }
        if (-not $command) { continue }

        Invoke-EditorTool 'command' @{ command = $command; editor = $id } | Out-Null
        Write-Output "Editor $($process.ProcessId) told to $command."
    }
}

Update-Build

if (-not $BuildOnly) { Send-Updates }
