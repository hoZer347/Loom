<#
.SYNOPSIS
    Checks update-editor.ps1 against an editor left open with unsaved work.

.DESCRIPTION
    Opens this slot's Release editor on a hidden desktop, on a scratch project,
    and runs update-editor.ps1 at it: an update saves a scene left unsaved,
    stops a run and restarts at once, a clean scene takes what a merge wrote to
    its file and to a prefab it holds, an open menu holds the update and the
    edit made under it is saved, a save that fails keeps the update
    waiting, a scripts update stops a run and compiles over unsaved work
    without a restart, a restart waits out a game being built, and an editor
    with nothing to take is told nothing. Throws naming what failed.
    A slot only: the user's Loom refuses, since its settings hold what they
    left open.

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
$CompileSeconds = 240
$PollMilliseconds = 250
$SettleSeconds = 3
$OpenBuildAndBuild = @'
click "##MainMenuBar" "File"
click "##Menu_00" "Build Settings..."
click "Build Settings" "Build"
'@

$common = & git -C $Root rev-parse --path-format=absolute --git-common-dir
if ([IO.Path]::GetFullPath((Split-Path $common -Parent)) -eq [IO.Path]::GetFullPath($Root)) { throw "$Root is the user's Loom. Run a slot's copy." }

if (-not $Desktop)
{
    & (Join-Path $Root 'run-on-agent-desktop.ps1') -Exe 'powershell.exe' -TimeoutSeconds 600 -- -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Desktop '{desktop}'
    exit $LASTEXITCODE
}

$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()

$project = Join-Path ([IO.Path]::GetTempPath()) "loom-update-test-$port"
New-Item -ItemType Directory $project | Out-Null
$projectFile = Join-Path $project 'Scratch.loomproject'
Set-Content -LiteralPath $projectFile -Value 'name=Scratch' -Encoding ascii
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

function Wait-Pid([scriptblock] $Until, [string] $What, [int] $Seconds = $DeadlineSeconds)
{
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $Seconds)
    {
        $found = Get-Pid
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
        if ($state -match 'Scripts: Loaded' -and $state -notmatch 'changed since') { return }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    throw "No compile within $CompileSeconds seconds: $state"
}

function Wait-Running
{
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $DeadlineSeconds)
    {
        if ((Invoke-Tool 'state') -match 'Play: running') { return }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    throw "No run within $DeadlineSeconds seconds."
}

function Invoke-Restart([string] $What)
{
    $before = $latest
    Invoke-Update | Out-Null
    $script:latest = Wait-Pid { param($id) $id -ne $before } $What
    Wait-Compiled
}

function Invoke-Update([string[]] $Flags = @())
{
    $line = "& '$Script' -Port $port -Project '$project' $($Flags -join ' ')"
    (& powershell -NoProfile -ExecutionPolicy Bypass -Command $line) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "update-editor.ps1 failed." }
}

$first = Start-Process -FilePath $Editor -ArgumentList "--desktop $Desktop --mcp $port `"$projectFile`"" -WorkingDirectory $project -PassThru
$latest = $first.Id

try
{
    Wait-Pid { param($id) $id -eq $first.Id } 'editor answering' | Out-Null
    Wait-Compiled

    $idle = & powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$Script' -Port $port"
    if ($idle -match 'told') { throw "An editor with nothing to take was told: $idle" }

    Invoke-Tool 'open_scene' @{ path = $scene } | Out-Null
    Invoke-Tool 'add_object' @{ name = 'Unsaved' } | Out-Null

    $told = Invoke-Update
    if ($told -notmatch 'told to update') { throw "The editor was not told to update: $told" }

    $latest = Wait-Pid { param($id) $id -ne $first.Id } 'restart over unsaved work'

    if (-not $first.WaitForExit($DeadlineSeconds * 1000)) { throw 'The first editor stayed up after the restart.' }
    if ((Invoke-Tool 'state') -match 'Update: waiting') { throw 'The restarted editor still has an update waiting.' }

    if ((Get-Content -LiteralPath $scene -Raw) -notmatch 'Unsaved') { throw 'The saved scene did not keep the object.' }
    if ((Invoke-Tool 'hierarchy') -notmatch 'Unsaved') { throw 'The restarted editor did not reopen the scene.' }

    Wait-Compiled

    $saved = Get-Content -LiteralPath $scene -Raw
    Set-Content -LiteralPath $scene -Value ($saved -replace 'Unsaved', 'Merged') -NoNewline -Encoding utf8
    Start-Sleep -Seconds $SettleSeconds

    Invoke-Restart 'restart on a merged scene'

    if ((Get-Content -LiteralPath $scene -Raw) -notmatch 'Merged') { throw 'The restart wrote a clean scene over what a merge left in its file.' }
    if ((Invoke-Tool 'hierarchy') -notmatch 'Merged') { throw 'The restarted editor did not take what a merge left in the scene.' }

    Invoke-Tool 'add_object' @{ name = 'Holder' } | Out-Null
    Invoke-Tool 'command' @{ command = 'play' } | Out-Null
    Wait-Running
    Invoke-Restart 'restart over a run'

    if ((Get-Content -LiteralPath $scene -Raw) -notmatch 'Holder') { throw 'The restart over a run did not save the object.' }
    if ((Invoke-Tool 'state') -notmatch 'Play: stopped') { throw 'The restarted editor came back playing.' }

    $prefab = Join-Path $project 'Thing.loomprefab'
    Set-Content -LiteralPath $prefab -Encoding ascii -Value "GameObject Thing($([guid]::NewGuid()))`n`tGameObject Inner($([guid]::NewGuid()))"
    $held = (Get-Content -LiteralPath $scene -Raw) -replace '(?m)^(\t*)(GameObject Holder\(.*\r?\n(?:\t+\d+ = .*\r?\n)*)', "`$1`$2`$1`tComponent Prefab($([guid]::NewGuid()))`n`$1`t`t0 = `"Thing.loomprefab`"`n"
    Set-Content -LiteralPath $scene -Value $held -NoNewline -Encoding utf8
    Invoke-Restart 'restart on a prefab instance'
    if ((Invoke-Tool 'hierarchy') -notmatch 'Inner') { throw 'The restarted editor did not load the prefab.' }
    Invoke-Tool 'command' @{ command = 'save' } | Out-Null

    $text = (Get-Content -LiteralPath $prefab -Raw) -replace 'Inner', 'InnerMerged'
    Set-Content -LiteralPath $prefab -Value $text -NoNewline -Encoding ascii
    $text = (Get-Content -LiteralPath $scene -Raw) -replace 'GameObject Merged', 'GameObject MergedBit'
    Set-Content -LiteralPath $scene -Value $text -NoNewline -Encoding utf8
    Invoke-Restart 'restart on a merged prefab'

    if ((Get-Content -LiteralPath $scene -Raw) -notmatch 'MergedBit') { throw 'A prefab the merge changed wrote its clean scene over the merge.' }
    if ((Invoke-Tool 'hierarchy') -notmatch 'InnerMerged') { throw 'The restarted editor did not take the merged prefab.' }

    Invoke-Tool 'ui' @{ script = 'click "##MainMenuBar" "File"' } | Out-Null
    Invoke-Tool 'add_object' @{ name = 'HeldEdit' } | Out-Null
    $beforeHeld = $latest
    Invoke-Update | Out-Null
    Start-Sleep -Seconds $SettleSeconds
    if ((Get-Pid) -ne $beforeHeld) { throw 'The editor restarted through an open menu.' }
    try { Invoke-Tool 'ui' @{ script = 'key escape' } | Out-Null } catch [Net.WebException] { }
    $latest = Wait-Pid { param($id) $id -ne $beforeHeld } 'restart once the menu shut'
    Wait-Compiled
    if ((Get-Content -LiteralPath $scene -Raw) -notmatch 'HeldEdit') { throw 'The restart lost an edit made under an open menu.' }

    Invoke-Tool 'add_object' @{ name = 'CannotSave' } | Out-Null
    Set-ItemProperty -LiteralPath $scene -Name IsReadOnly -Value $true
    $beforeRefused = $latest
    Invoke-Update | Out-Null
    Start-Sleep -Seconds $SettleSeconds
    Set-ItemProperty -LiteralPath $scene -Name IsReadOnly -Value $false
    if ((Get-Pid) -ne $beforeRefused) { throw 'The editor restarted over a save that failed.' }
    if ((Invoke-Tool 'state') -notmatch 'Update: waiting') { throw 'A failed save lost the update.' }

    Invoke-Tool 'command' @{ command = 'save' } | Out-Null
    $latest = Wait-Pid { param($id) $id -ne $beforeRefused } 'restart once the failed save went through'
    Wait-Compiled
    if ((Get-Content -LiteralPath $scene -Raw) -notmatch 'CannotSave') { throw 'The object held back by the failed save was not written.' }

    Invoke-Tool 'add_object' @{ name = 'UnsavedAgain' } | Out-Null

    $scriptsFolder = Split-Path (Get-ChildItem -LiteralPath $project -Filter '*.vcxproj' -Recurse | Select-Object -First 1).FullName
    Set-Content -LiteralPath (Join-Path $scriptsFolder 'Changed.hpp') -Value '#pragma once' -Encoding ascii
    Start-Sleep -Seconds $SettleSeconds
    if ((Invoke-Tool 'state') -notmatch 'changed since') { throw 'The scripts did not go stale.' }

    Invoke-Tool 'command' @{ command = 'play' } | Out-Null
    Wait-Running

    $scripts = Invoke-Update '-ScriptsOnly'
    if ($scripts -notmatch 'told to update-scripts') { throw "The editor was not told to update its scripts: $scripts" }

    Start-Sleep -Seconds $SettleSeconds
    if ((Get-Pid) -ne $latest) { throw 'A scripts update restarted the editor.' }
    Wait-Compiled
    if ((Invoke-Tool 'state') -notmatch 'Play: stopped') { throw 'A scripts update left the run going.' }
    if ((Invoke-Tool 'hierarchy') -notmatch 'UnsavedAgain') { throw 'The compile dropped the unsaved object.' }
    if ((Get-Content -LiteralPath $scene -Raw) -match 'UnsavedAgain') { throw 'The compile saved the unsaved object.' }

    Invoke-Tool 'command' @{ command = 'save' } | Out-Null
    Invoke-Tool 'ui' @{ script = $OpenBuildAndBuild } | Out-Null

    Invoke-Update | Out-Null
    Start-Sleep -Seconds $SettleSeconds
    if ((Get-Pid) -ne $latest) { throw 'The editor restarted over a game being built.' }
    if ((Invoke-Tool 'state') -notmatch 'Update: waiting') { throw 'State does not say an update waits for the build.' }

    $beforeBuild = $latest
    $latest = Wait-Pid { param($id) $id -ne $beforeBuild } 'restart once built' $CompileSeconds

    Write-Output 'update-editor passed.'
}
finally
{
    Get-CimInstance Win32_Process -Filter "Name = 'Loom Editor.exe'" |
        Where-Object { $_.CommandLine -like "*--desktop $Desktop*" } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 1
    Remove-Item -LiteralPath $project -Recurse -Force -ErrorAction SilentlyContinue
}
