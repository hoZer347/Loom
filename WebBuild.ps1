# Dot-sourced by the scripts that build for the web: where emscripten is, the
# flags every web build shares, and the engine compiled once into a library
# they all link.

$webRoot = $PSScriptRoot

$webWasm = Join-Path $webRoot 'Loom WASM'
$webImGui = Join-Path $webRoot 'Loom ImGui'

$webIncludes = @(
    $webWasm
    (Join-Path $webRoot 'External Libraries\glm')
    (Join-Path $webRoot 'External Libraries\stb')
    (Join-Path $webRoot 'External Libraries\miniaudio')
    $webImGui
) | ForEach-Object { "-I$_" }

$webCompileFlags = @(
    '-std=c++23'
    '-O2'
    '-fexceptions'
    '-pthread'
)

$webLinkFlags = @(
    '-sUSE_GLFW=3'
    '-sFULL_ES3'
    '-sMIN_WEBGL_VERSION=2'
    '-sMAX_WEBGL_VERSION=2'
    '-sFETCH'                       # Shaders are fetched over HTTP...
    '-sASYNCIFY'                    # ...and waited on with emscripten_sleep, as miniaudio waits on its audio worklet.
    '-sAUDIO_WORKLET=1'             # miniaudio mixes in an AudioWorklet rather than the deprecated ScriptProcessorNode...
    '-sWASM_WORKERS=1'              # ...which runs on a Wasm Worker.
    '-sALLOW_MEMORY_GROWTH=1'
    '-sPTHREAD_POOL_SIZE=2*navigator.hardwareConcurrency'  # Pre-spawned, since a later one cannot start while main blocks: the job workers, the physics thread and an update lane for each thread ID the editor offers.
    '-Wno-pthreads-mem-growth'
)

# The oldest emscripten whose emdawnwebgpu port the WebGPU renderer builds with.
$minimumEmscripten = [version] '4.0.10'

# Emscripten 4 launches its tools through .bat files and later ones through .exe.
$emscriptenLaunchers = '.exe', '.bat'

function Get-EmscriptenVersion([string] $Emxx)
{
    [version] (Get-Content -LiteralPath (Join-Path (Split-Path -Parent $Emxx) 'emscripten-version.txt') -Raw).Trim().Trim('"')
}

# Another of emscripten's tools, launched the same way as em++.
function Get-EmscriptenTool([string] $Emxx, [string] $Name)
{
    Join-Path (Split-Path -Parent $Emxx) ($Name + [IO.Path]::GetExtension($Emxx))
}

function Find-Emxx
{
    $roots = @()

    if ($env:EMSDK) { $roots += (Join-Path $env:EMSDK 'upstream\emscripten') }

    $roots += 'C:\emsdk\upstream\emscripten'
    $roots += (Join-Path $webRoot 'External Libraries\emsdk\upstream\emscripten')
    $roots += Get-Command em++.exe, em++.bat -ErrorAction SilentlyContinue | ForEach-Object { Split-Path -Parent $_.Source }

    $tooOld = @()

    foreach ($root in $roots)
    {
        foreach ($launcher in $emscriptenLaunchers)
        {
            $candidate = Join-Path $root ('em++' + $launcher)

            if (-not (Test-Path -LiteralPath $candidate)) { continue }

            if ((Get-EmscriptenVersion $candidate) -ge $minimumEmscripten) { return $candidate }

            $tooOld += "$candidate is $(Get-EmscriptenVersion $candidate)"
            break
        }
    }

    throw @"
Could not find emscripten $minimumEmscripten or later.$(($tooOld | ForEach-Object { "`n    $_" }) -join '')
Install the emscripten SDK and either set EMSDK, put it at C:\emsdk, or install
the copy vendored at "External Libraries\emsdk":

    cd "External Libraries\emsdk"
    .\emsdk install latest
    .\emsdk activate latest
"@
}

function Test-Stale([string] $Target, $Inputs)
{
    -not (Test-Path -LiteralPath $Target) -or
        ($Inputs | Measure-Object -Property LastWriteTimeUtc -Maximum).Maximum -gt (Get-Item -LiteralPath $Target).LastWriteTimeUtc
}

# Each source to its own object, named from its path under $Base so files of
# the same name in different folders do not collide, with the list of the
# headers its compile read beside it.
function Get-CompileJobs($Sources, [string] $Base, [string] $Directory)
{
    foreach ($source in $Sources)
    {
        $object = Join-Path $Directory (($source.Substring($Base.Length).TrimStart('\') -replace '[\\ ]', '_') + '.o')

        [pscustomobject] @{
            Source = $source
            Object = $object
            Depends = [IO.Path]::ChangeExtension($object, '.d')
        }
    }
}

# The files a compile read, from the make rule em++'s -MMD wrote: the target, a
# colon, then paths separated by spaces, a space within a path escaped and long
# lines continued with a backslash.
function Read-Dependencies([string] $Path)
{
    $rule = [IO.File]::ReadAllText($Path) -replace '\\\r?\n', ' '
    $paths = $rule.Substring($rule.IndexOf(': ') + 2).Trim()

    [regex]::Split($paths, '(?<!\\)\s+') | Where-Object { $_ } | ForEach-Object { $_ -replace '\\ ', ' ' }
}

# The jobs to compile again: all of them when the flags differ from the ones
# $Stamp holds from the last build, otherwise those whose object is missing or
# older than its source or a header it read last time.
function Get-StaleJobs($Jobs, [string[]] $Flags, [string] $Stamp)
{
    if (-not (Test-Path -LiteralPath $Stamp) -or [IO.File]::ReadAllText($Stamp) -cne ($Flags -join "`n"))
    {
        return $Jobs
    }

    $written = @{}

    foreach ($job in $Jobs)
    {
        if (-not (Test-Path -LiteralPath $job.Object) -or -not (Test-Path -LiteralPath $job.Depends))
        {
            $job
            continue
        }

        $built = (Get-Item -LiteralPath $job.Object).LastWriteTimeUtc

        foreach ($dependency in Read-Dependencies $job.Depends)
        {
            if (-not $written.ContainsKey($dependency))
            {
                $written[$dependency] = if (Test-Path -LiteralPath $dependency) { (Get-Item -LiteralPath $dependency).LastWriteTimeUtc } else { [DateTime]::MaxValue }
            }

            if ($written[$dependency] -gt $built)
            {
                $job
                break
            }
        }
    }
}

# Runs the jobs a process per core at a time. Returns whether all of them
# succeeded; the compiler has already said why when one did not.
function Invoke-Compile([string] $Emxx, $Jobs, [string[]] $Flags)
{
    $running = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
    $failed = $false

    foreach ($job in $Jobs)
    {
        while ($running.Count -ge [Environment]::ProcessorCount)
        {
            $running[0].WaitForExit()
            $failed = $failed -or $running[0].ExitCode -ne 0
            $running.RemoveAt(0)
        }

        # Start-Process joins its arguments with spaces and quotes none of them.
        $arguments = @($Flags) + @('-MMD', '-MF', $job.Depends, '-c', $job.Source, '-o', $job.Object) |
            ForEach-Object { '"' + $_ + '"' }

        $process = Start-Process -FilePath $Emxx -ArgumentList $arguments -NoNewWindow -PassThru

        # The exit code is only kept for a process whose handle was opened while it ran.
        $null = $process.Handle
        $running.Add($process)
    }

    foreach ($process in $running)
    {
        $process.WaitForExit()
        $failed = $failed -or $process.ExitCode -ne 0
    }

    -not $failed
}

# The engine as a static library under x64\WebPlayer, rebuilt only when an
# engine source or header, or this file with its flags, is newer than it.
# Returns its path, or nothing when it failed to build.
function Build-WebEngine([string] $Emxx)
{
    $directory = Join-Path $webRoot 'x64\WebPlayer'
    $library = Join-Path $directory 'Loom Engine.a'

    # The same sources the desktop build globs, plus the ImGui the engine calls into.
    $sources = @(
        Get-ChildItem -Path (Join-Path $webWasm '*.cpp')
        Get-ChildItem -Path (Join-Path $webWasm 'Utilities\*.cpp') -Exclude '_*.cpp'
        'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp',
            'imgui_impl_glfw.cpp', 'imgui_impl_opengl3.cpp', 'ImGuiItemHooks.cpp' |
            ForEach-Object { Get-Item -LiteralPath (Join-Path $webImGui $_) }
    ) | ForEach-Object { $_.FullName }

    $inputs = @(
        Get-ChildItem -Path $webWasm, $webImGui -Recurse -File -Include '*.cpp', '*.h', '*.hpp'
        Get-Item -LiteralPath (Join-Path $webRoot 'WebBuild.ps1')
    )

    if (-not (Test-Stale $library $inputs))
    {
        return $library
    }

    Write-Host "Building the engine for the web ($($sources.Count) files)"

    New-Item -ItemType Directory -Force -Path $directory | Out-Null

    $jobs = @(Get-CompileJobs $sources $webRoot $directory)

    if (-not (Invoke-Compile $Emxx $jobs (@($webCompileFlags) + @($webIncludes))))
    {
        Write-Host 'The engine failed to build.'
        return
    }

    Remove-Item -LiteralPath $library -ErrorAction SilentlyContinue
    & (Get-EmscriptenTool $Emxx 'emar') rcs $library @($jobs | ForEach-Object { $_.Object }) | Out-Host

    if ($LASTEXITCODE -ne 0)
    {
        Write-Host "emar failed with exit code $LASTEXITCODE"
        return
    }

    $library
}
