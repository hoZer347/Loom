# Dot-sourced by the scripts that build for the web: where emscripten is, the
# flags every web build shares, and the engine compiled once into a library
# they all link.

$webRoot = $PSScriptRoot

$webWasm = Join-Path $webRoot 'Loom WASM'
$webMath = Join-Path $webRoot 'Loom Math'
$webImGui = Join-Path $webRoot 'Loom ImGui'

$webIncludes = @(
    $webWasm
    $webMath
    (Join-Path $webRoot 'External Libraries\glm')
    (Join-Path $webRoot 'External Libraries\stb')
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
    '-sASYNCIFY'                    # ...and waited on with emscripten_sleep.
    '-sALLOW_MEMORY_GROWTH=1'
    '-sPTHREAD_POOL_SIZE=8'         # Pre-spawned: a later thread cannot start while main blocks in join.
    '-Wno-pthreads-mem-growth'
)

function Find-Emcc
{
    $candidates = @()

    if ($env:EMSDK) { $candidates += (Join-Path $env:EMSDK 'upstream\emscripten\emcc.bat') }

    $candidates += 'C:\emsdk\upstream\emscripten\emcc.bat'
    $candidates += (Join-Path $webRoot 'External Libraries\emsdk\upstream\emscripten\emcc.bat')

    foreach ($candidate in $candidates)
    {
        if (Test-Path $candidate) { return $candidate }
    }

    $onPath = Get-Command emcc.bat -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    throw @"
Could not find emcc. Install the emscripten SDK and either set EMSDK, put it at
C:\emsdk, or install the copy vendored at "External Libraries\emsdk":

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
# the same name in different folders do not collide.
function Get-CompileJobs($Sources, [string] $Base, [string] $Directory)
{
    foreach ($source in $Sources)
    {
        [pscustomobject] @{
            Source = $source
            Object = Join-Path $Directory (($source.Substring($Base.Length).TrimStart('\') -replace '[\\ ]', '_') + '.o')
        }
    }
}

# Runs the jobs a process per core at a time. Returns whether all of them
# succeeded; the compiler has already said why when one did not.
function Invoke-Compile([string] $Emcc, $Jobs, [string[]] $Flags)
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
        $arguments = @($Flags) + @('-c', $job.Source, '-o', $job.Object) |
            ForEach-Object { '"' + $_ + '"' }

        $process = Start-Process -FilePath $Emcc -ArgumentList $arguments -NoNewWindow -PassThru

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
function Build-WebEngine([string] $Emcc)
{
    $directory = Join-Path $webRoot 'x64\WebPlayer'
    $library = Join-Path $directory 'Loom Engine.a'

    # The same sources the desktop build globs, plus the ImGui the engine calls into.
    $sources = @(
        Get-ChildItem -Path (Join-Path $webWasm '*.cpp')
        Get-ChildItem -Path (Join-Path $webWasm 'Utilities\*.cpp') -Exclude '_*.cpp'
        Get-ChildItem -Path (Join-Path $webMath '*.cpp')
        'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp',
            'imgui_impl_glfw.cpp', 'imgui_impl_opengl3.cpp' |
            ForEach-Object { Get-Item -LiteralPath (Join-Path $webImGui $_) }
    ) | ForEach-Object { $_.FullName }

    $inputs = @(
        Get-ChildItem -Path $webWasm, $webMath, $webImGui -Recurse -File -Include '*.cpp', '*.h', '*.hpp'
        Get-Item -LiteralPath (Join-Path $webRoot 'WebBuild.ps1')
    )

    if (-not (Test-Stale $library $inputs))
    {
        return $library
    }

    Write-Host "Building the engine for the web ($($sources.Count) files)"

    New-Item -ItemType Directory -Force -Path $directory | Out-Null

    $jobs = @(Get-CompileJobs $sources $webRoot $directory)

    if (-not (Invoke-Compile $Emcc $jobs (@($webCompileFlags) + @($webIncludes))))
    {
        Write-Host 'The engine failed to build.'
        return
    }

    Remove-Item -LiteralPath $library -ErrorAction SilentlyContinue
    & (Join-Path (Split-Path -Parent $Emcc) 'emar.bat') rcs $library @($jobs | ForEach-Object { $_.Object }) | Out-Host

    if ($LASTEXITCODE -ne 0)
    {
        Write-Host "emar failed with exit code $LASTEXITCODE"
        return
    }

    $library
}
