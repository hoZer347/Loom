<#
.SYNOPSIS
    Checks update-editor.ps1 against an editor left open with unsaved work.

.DESCRIPTION
    Opens this slot's Release editor on a hidden desktop, on a scratch project,
    and runs update-editor.ps1 at it: an update waits while a scene is unsaved
    and restarts the editor once it is saved, a scripts update goes in over
    unsaved work without a restart, and an editor with nothing to take is told
    nothing. Throws naming what failed. A slot only: the user's
    Loom refuses, since its settings hold what they left open.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File 'C:\Users\3hoze\Desktop\Loom 1\Loom Tests\update-editor.ps1'
#>
param(
    # The hidden desktop run-on-agent-desktop.ps1 made; without one it runs this under that.
    [string] $Desktop
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot
$Script = Join-Path $Root 'update-editor.ps1'
$Editor = Join-Path $Root 'x64\Release\Loom Editor.exe'
$DeadlineSeconds = 60
$PollMilliseconds = 250
$SettleSeconds = 3

$common = & git -C $Root rev-parse --path-format=absolute --git-common-dir
if ([IO.Path]::GetFullPath((Split-Path $common -Parent)) -eq [IO.Path]::GetFullPath($Root)) { throw "$Root is the user's Loom. Run a slot's copy." }

if (-not $Desktop)
{
    & (Join-Path $Root 'run-on-agent-desktop.ps1') -Exe 'powershell.exe' -TimeoutSeconds 300 -- -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Desktop '{desktop}'
    exit $LASTEXITCODE
}

$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()

$project = Join-Path ([IO.Path]::GetTempPath()) "loom-update-test-$port"
New-Item -ItemType Directory $project | Out-Null
Set-Content -LiteralPath (Join-Path $project 'Scratch.loomproject') -Value 'name=Scratch' -Encoding ascii
$scene = Join-Path $project 'Scratch.loomscene'
New-Item -ItemType File $scene | Out-Null

function Invoke-Tool([string] $Tool, [hashtable] $Arguments = @{})
{
    $body = @{ jsonrpc = '2.0'; id = 1; method = 'tools/call'; params = @{ name = $Tool; arguments = $Arguments } } | ConvertTo-Json -Depth 4
    $reply = Invoke-RestMethod -Uri "http://127.0.0.1:$port/mcp" -Method Post -ContentType 'application/json' -Headers @{ Accept = 'application/json' } -Body $body -TimeoutSec 15
    if ($reply.error -or $reply.result.isError) { throw "$Tool failed: $($reply | ConvertTo-Json -Depth 6 -Compress)" }
    $reply.result.content[0].text
}

function Get-Pid
{
    $state = try { Invoke-Tool 'state' } catch { '' }
    if ($state -match 'Process: (\d+)') { [int] $Matches[1] }
}

function Wait-Pid([scriptblock] $Until, [string] $What)
{
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $DeadlineSeconds)
    {
        $found = Get-Pid
        if ($found -and (& $Until $found)) { return $found }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    throw "No $What within $DeadlineSeconds seconds."
}

function Invoke-Update([string[]] $Flags = @())
{
    $line = "& '$Script' -Port $port -Project '$project' $($Flags -join ' ')"
    (& powershell -NoProfile -ExecutionPolicy Bypass -Command $line) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "update-editor.ps1 failed." }
}

$first = Start-Process -FilePath $Editor -ArgumentList "--desktop $Desktop --mcp $port `"$project`"" -WorkingDirectory $project -PassThru
$latest = $first.Id

try
{
    Wait-Pid { param($id) $id -eq $first.Id } 'editor answering' | Out-Null

    $idle = & powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$Script' -Port $port"
    if ($idle -match 'told') { throw "An editor with nothing to take was told: $idle" }

    Invoke-Tool 'open_scene' @{ path = $scene } | Out-Null
    Invoke-Tool 'add_object' @{ name = 'Unsaved' } | Out-Null

    $told = Invoke-Update
    if ($told -notmatch 'told to update, waiting') { throw "The update did not wait for unsaved work: $told" }

    Start-Sleep -Seconds $SettleSeconds
    if ((Get-Pid) -ne $first.Id) { throw 'The editor restarted over unsaved work.' }
    if ((Invoke-Tool 'state') -notmatch 'Update: waiting') { throw 'State does not say an update waits.' }

    Invoke-Tool 'command' @{ command = 'save' } | Out-Null

    $latest = Wait-Pid { param($id) $id -ne $first.Id } 'restart once saved'

    if (-not $first.WaitForExit($DeadlineSeconds * 1000)) { throw 'The first editor stayed up after the restart.' }
    if ((Invoke-Tool 'state') -match 'Update: waiting') { throw 'The restarted editor still has an update waiting.' }

    if ((Get-Content -LiteralPath $scene -Raw) -notmatch 'Unsaved') { throw 'The saved scene did not keep the object.' }
    if ((Invoke-Tool 'hierarchy') -notmatch 'Unsaved') { throw 'The restarted editor did not reopen the scene.' }

    Invoke-Tool 'add_object' @{ name = 'UnsavedAgain' } | Out-Null

    $scripts = Invoke-Update '-ScriptsOnly'
    if ($scripts -notmatch 'told to update-scripts') { throw "The editor was not told to update its scripts: $scripts" }

    Start-Sleep -Seconds $SettleSeconds
    if ((Get-Pid) -ne $latest) { throw 'A scripts update restarted the editor.' }
    if ((Invoke-Tool 'state') -match 'Update: waiting') { throw 'A scripts update waited on unsaved work.' }

    Write-Output 'update-editor passed.'
}
finally
{
    Get-CimInstance Win32_Process -Filter "Name LIKE 'Loom Editor.exe%'" |
        Where-Object { $_.CommandLine -like "*--desktop $Desktop*" } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 1
    Remove-Item -LiteralPath $project -Recurse -Force -ErrorAction SilentlyContinue
}
