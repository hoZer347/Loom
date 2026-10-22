<#
.SYNOPSIS
    Times Loom's physics against Unity's on the same scene and sets the two
    side by side.

.DESCRIPTION
    The scene is the Profiling demo's Crowd: a walled pit, three ring
    MeshColliders, and fifteen hundred boxes and balls dropped from a grid,
    with a row of 160 raycasts every step. Loom runs it as the Benchmark suite
    in Loom Tests, built in Release; Unity runs it headless from
    "Benchmarks\Unity Crowd", stepping physics by hand, once with its job
    threads and once on one thread.

    Both step 1/60 of a second 1200 times and report, per step: the slowest,
    the average, the average over the first six seconds while everything
    lands, and over the last second once it has settled; the cost of one
    raycast; how many bodies are still awake; and how many got out of the
    pit. Unity sleeps whole islands of touching bodies together, so one that
    still twitches keeps its pile awake, while Loom sleeps each body on its
    own. Once settled, Loom has most of its bodies asleep and Unity most of
    its awake, so the settled row compares that difference as much as speed;
    the landing row, with every body awake in both, compares speed alone.

.EXAMPLE
    .\run-physics-benchmark.ps1
    .\run-physics-benchmark.ps1 -SkipUnity

.NOTES
    Needs the Unity version in "Benchmarks\Unity Crowd\ProjectSettings\
    ProjectVersion.txt" installed through Unity Hub, with a licence that runs
    in batch mode. Nothing opens a window.
#>
[CmdletBinding()]
param(
    [switch] $SkipUnity
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$project = Join-Path $root 'Benchmarks\Unity Crowd'
$work = Join-Path ([IO.Path]::GetTempPath()) "loom-physics-benchmark-$PID"
New-Item -ItemType Directory -Force $work | Out-Null

function Read-Results([string] $path)
{
    $results = [ordered]@{ }

    foreach ($line in Get-Content $path)
    {
        $name, $value = $line -split ' ', 2
        $results[$name] = $value
    }

    return $results
}

function Find-Unity
{
    $version = (Select-String -Path (Join-Path $project 'ProjectSettings\ProjectVersion.txt') -Pattern '^m_EditorVersion: (.+)$').Matches[0].Groups[1].Value
    $unity = Join-Path $env:ProgramFiles "Unity\Hub\Editor\$version\Editor\Unity.exe"

    if (-not (Test-Path $unity))
    {
        throw "Unity $version is not installed at $unity. Install it through Unity Hub, or pass -SkipUnity."
    }

    return $unity
}

function Invoke-Unity([string] $unity, [string] $output, [string[]] $extra)
{
    $log = Join-Path $work ([IO.Path]::GetFileNameWithoutExtension($output) + '.log')
    $arguments = @('-batchmode', '-nographics', '-quit', '-projectPath', "`"$project`"",
        '-executeMethod', 'CrowdBenchmark.Run', '-benchmarkOutput', "`"$output`"", '-logFile', "`"$log`"") + $extra

    # Unity sometimes crashes on its way out of batch mode, after the results
    # are written; only missing results count as a failure.
    $process = Start-Process -FilePath $unity -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $process.WaitForExit()

    if (-not (Test-Path $output))
    {
        throw "Unity wrote no results; see $log"
    }

    return Read-Results $output
}

$columns = [ordered]@{ }

Write-Host 'Timing Loom (Release)...'
$loomOutput = Join-Path $work 'loom.txt'
$env:LOOM_BENCHMARK_OUTPUT = $loomOutput

try
{
    & (Join-Path $root 'run-tests.ps1') -Configuration Release -TestArgs '-ts=Benchmark', '--no-skip' | Out-Null
}
finally
{
    Remove-Item Env:\LOOM_BENCHMARK_OUTPUT
}

if (-not (Test-Path $loomOutput))
{
    throw 'The Loom benchmark wrote no results.'
}

$columns['Loom'] = Read-Results $loomOutput

if (-not $SkipUnity)
{
    $unity = Find-Unity

    Write-Host 'Timing Unity...'
    $columns['Unity'] = Invoke-Unity $unity (Join-Path $work 'unity.txt') @()

    Write-Host 'Timing Unity on one thread...'
    $columns['Unity, one thread'] = Invoke-Unity $unity (Join-Path $work 'unity-one-thread.txt') @('-job-worker-count', '0')
}

$rows = [ordered]@{
    'peak_step_ms'    = 'Slowest step (ms)'
    'mean_step_ms'    = 'Average step (ms)'
    'landing_step_ms' = 'Landing, first 6 s (ms)'
    'settled_step_ms' = 'Settled, last 1 s (ms; see Still awake)'
    'ray_us'          = 'One raycast (us)'
    'awake'           = 'Still awake'
    'escaped'         = 'Out of the pit'
}

$table = foreach ($key in $rows.Keys)
{
    $row = [ordered]@{ 'Per physics step' = $rows[$key] }

    foreach ($engine in $columns.Keys)
    {
        $value = $columns[$engine][$key]
        $number = 0.0

        if ([double]::TryParse($value, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref] $number))
        {
            $value = '{0:0.###}' -f $number
        }

        $row[$engine] = $value
    }

    [pscustomobject]$row
}

Write-Host ''
Write-Host "$($columns['Loom']['bodies']) bodies, $($columns['Loom']['steps']) steps of 1/60 s"
$table | Format-Table -AutoSize | Out-String -Width 200 | Write-Host

Remove-Item -Recurse -Force $work
