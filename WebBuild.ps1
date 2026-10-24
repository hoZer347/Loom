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
    '-sSTACK_SIZE=1MB'              # Tint recurses deeper than the 64KB default allows when it turns a shader into WGSL.
    '-sPTHREAD_POOL_SIZE=2*navigator.hardwareConcurrency'  # Pre-spawned, since a later one cannot start while main blocks: the job workers, the physics thread and an update lane for each thread ID the editor offers.
    '-Wno-pthreads-mem-growth'
    '--use-port=emdawnwebgpu'
)

# The oldest emscripten whose emdawnwebgpu port the WebGPU renderer builds with.
$minimumEmscripten = [version] '4.0.10'

# Dawn, for the Tint it builds, and the libraries of its own that Tint and
# glslang build with, each at the commit Dawn's DEPS pins.
$webDawn = Join-Path $webRoot 'External Libraries\dawn'

$webShaderToolSources = @(
    [pscustomobject] @{ Path = ''; Url = 'https://dawn.googlesource.com/dawn'; Commit = '6ae1aabf936c87331bb429c9bdfd2c2e86398f77' }
    [pscustomobject] @{ Path = 'third_party\glslang\src'; Url = 'https://chromium.googlesource.com/external/github.com/KhronosGroup/glslang'; Commit = '2465225163bb0127e3f76f3036acd324312899cd' }
    [pscustomobject] @{ Path = 'third_party\spirv-headers\src'; Url = 'https://chromium.googlesource.com/external/github.com/KhronosGroup/SPIRV-Headers'; Commit = 'fbf86836a9b1966586faf7be7bb8a6440e156d62' }
    [pscustomobject] @{ Path = 'third_party\spirv-tools\src'; Url = 'https://chromium.googlesource.com/external/github.com/KhronosGroup/SPIRV-Tools'; Commit = '7651d9234622041b3f1b09e5c684c7c6eb352d08' }
    [pscustomobject] @{ Path = 'third_party\abseil-cpp'; Url = 'https://chromium.googlesource.com/chromium/src/third_party/abseil-cpp'; Commit = 'af5aede14a0b4013be6890148d34e0fd7b1fac69' }
)

$webShaderToolsDirectory = Join-Path $webRoot 'x64\WebShaderTools'

# Every web build reads glslang's headers, the header glslang's build writes,
# Wgsl.h and SPIRV-Reflect, and draws through the WebGPU emscripten's port
# provides.
$webShaderIncludes = @(
    (Join-Path $webDawn 'third_party\glslang\src')
    (Join-Path $webShaderToolsDirectory 'include')
    (Join-Path $webRoot 'Web Shader Tools')
    (Join-Path $webRoot 'External Libraries\Vulkan\Source\SPIRV-Reflect')
) | ForEach-Object { "-I$_" }

$webIncludes += $webShaderIncludes

$webCompileFlags += '--use-port=emdawnwebgpu'

# A shallow fetch of the commit alone, which a submodule's history is too
# large to clone for. A directory holding anything else is replaced.
function Get-Checkout([string] $Directory, [string] $Url, [string] $Commit)
{
    if ((Test-Path -LiteralPath (Join-Path $Directory '.git')) -and (git -C $Directory rev-parse HEAD) -eq $Commit)
    {
        return $true
    }

    Write-Host "Fetching $Url at $Commit"

    Remove-Item -LiteralPath $Directory -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $Directory | Out-Null

    git -C $Directory init -q
    git -C $Directory fetch -q --depth 1 $Url $Commit
    git -C $Directory checkout -q FETCH_HEAD

    $LASTEXITCODE -eq 0
}

# The CMake and Ninja Visual Studio installs with its C++ CMake tools, or
# those on PATH.
function Find-CMakeTools
{
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

    if (Test-Path -LiteralPath $vswhere)
    {
        foreach ($installation in & $vswhere -all -prerelease -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath)
        {
            $tools = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake'
            $cmake = Join-Path $tools 'CMake\bin\cmake.exe'
            $ninja = Join-Path $tools 'Ninja\ninja.exe'

            if ((Test-Path -LiteralPath $cmake) -and (Test-Path -LiteralPath $ninja))
            {
                return [pscustomobject] @{ CMake = $cmake; Ninja = $ninja }
            }
        }
    }

    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    $ninja = Get-Command ninja.exe -ErrorAction SilentlyContinue

    if ($cmake -and $ninja)
    {
        return [pscustomobject] @{ CMake = $cmake.Source; Ninja = $ninja.Source }
    }

    throw 'Could not find CMake and Ninja. Install the "C++ CMake tools for Windows" component of Visual Studio, or put cmake and ninja on PATH.'
}

# glslang, Tint and SPIRV-Reflect as static libraries under x64\WebShaderTools, fetched and
# configured the first time and built again only for what changed. Returns
# their paths, or nothing when they failed to build.
function Build-WebShaderTools([string] $Emxx)
{
    foreach ($source in $webShaderToolSources)
    {
        # A trailing backslash would escape the quote PowerShell closes the path with.
        if (-not (Get-Checkout (Join-Path $webDawn $source.Path).TrimEnd('\') $source.Url $source.Commit))
        {
            Write-Host "Could not fetch $($source.Url)"
            return
        }
    }

    $tools = Find-CMakeTools

    if (-not (Test-Path -LiteralPath (Join-Path $webShaderToolsDirectory 'CMakeCache.txt')))
    {
        Write-Host 'Configuring the web shader tools'

        $options = @(
            '-DDAWN_BUILD_SAMPLES=OFF', '-DDAWN_BUILD_TESTS=OFF', '-DDAWN_BUILD_PROTOBUF=OFF', '-DDAWN_USE_GLFW=OFF',
            '-DDAWN_ENABLE_D3D11=OFF', '-DDAWN_ENABLE_D3D12=OFF', '-DDAWN_ENABLE_METAL=OFF', '-DDAWN_ENABLE_NULL=OFF',
            '-DDAWN_ENABLE_DESKTOP_GL=OFF', '-DDAWN_ENABLE_OPENGLES=OFF', '-DDAWN_ENABLE_VULKAN=OFF',
            '-DTINT_BUILD_SPV_READER=ON', '-DTINT_BUILD_WGSL_WRITER=ON',
            '-DTINT_BUILD_WGSL_READER=OFF', '-DTINT_BUILD_GLSL_WRITER=OFF', '-DTINT_BUILD_GLSL_VALIDATOR=OFF',
            '-DTINT_BUILD_HLSL_WRITER=OFF', '-DTINT_BUILD_MSL_WRITER=OFF', '-DTINT_BUILD_SPV_WRITER=OFF',
            '-DTINT_BUILD_NULL_WRITER=OFF', '-DTINT_BUILD_TESTS=OFF', '-DTINT_BUILD_CMD_TOOLS=OFF',
            '-DTINT_BUILD_IR_BINARY=OFF', '-DTINT_ENABLE_IR_DUMPING=OFF', '-DTINT_ENABLE_IR_VALIDATION_ASSERTS=OFF'
        )

        & (Get-EmscriptenTool $Emxx 'emcmake') $tools.CMake -S (Join-Path $webRoot 'Web Shader Tools') -B $webShaderToolsDirectory -G Ninja `
            '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)" "-DDAWN=$($webDawn.Replace('\', '/'))" `
            '-DCMAKE_CXX_FLAGS=-pthread -fexceptions' '-DCMAKE_C_FLAGS=-pthread' @options | Out-Host

        if ($LASTEXITCODE -ne 0)
        {
            Remove-Item -LiteralPath (Join-Path $webShaderToolsDirectory 'CMakeCache.txt') -ErrorAction SilentlyContinue
            Write-Host "CMake failed with exit code $LASTEXITCODE"
            return
        }
    }

    & $tools.Ninja -C $webShaderToolsDirectory Everything | Out-Host

    if ($LASTEXITCODE -ne 0)
    {
        Write-Host "The web shader tools failed to build, with exit code $LASTEXITCODE"
        return
    }

    Get-ChildItem -Path $webShaderToolsDirectory -Recurse -File -Filter '*.a' | ForEach-Object { $_.FullName }
}

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
# engine source or header, or this file with its flags, is newer than it,
# and the shader tools it links. Returns the engine's path as Engine and the
# tools' as Tools, or nothing when either failed to build.
function Build-WebEngine([string] $Emxx)
{
    $tools = @(Build-WebShaderTools $Emxx)

    if (-not $tools)
    {
        return
    }

    $directory = Join-Path $webRoot 'x64\WebPlayer'
    $library = Join-Path $directory 'Loom Engine.a'
    $built = [pscustomobject] @{ Engine = $library; Tools = $tools }

    # The same sources the desktop build globs, plus the ImGui the engine calls into.
    $sources = @(
        Get-ChildItem -Path (Join-Path $webWasm '*.cpp')
        Get-ChildItem -Path (Join-Path $webWasm 'Utilities\*.cpp') -Exclude '_*.cpp'
        'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp',
            'imgui_impl_glfw.cpp', 'imgui_impl_opengl3.cpp', 'imgui_impl_wgpu.cpp', 'ImGuiItemHooks.cpp' |
            ForEach-Object { Get-Item -LiteralPath (Join-Path $webImGui $_) }
    ) | ForEach-Object { $_.FullName }

    $inputs = @(
        Get-ChildItem -Path $webWasm, $webImGui -Recurse -File -Include '*.cpp', '*.h', '*.hpp'
        Get-Item -LiteralPath (Join-Path $webRoot 'WebBuild.ps1')
    )

    if (-not (Test-Stale $library $inputs))
    {
        return $built
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

    $built
}
