# Find-MSBuild: the newest Visual Studio's MSBuild.exe, for the scripts that
# build the engine. Dot-source it.

function Find-MSBuild
{
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere)
    {
        $found = & $vswhere -latest -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        if ($found) { return $found }
    }

    $fallback = Get-ChildItem `
        -Path "$env:ProgramFiles\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio" `
        -Filter MSBuild.exe -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\Bin\MSBuild\.exe$' } |
        Select-Object -First 1

    if ($fallback) { return $fallback.FullName }

    throw 'Could not locate MSBuild.exe. Run this from a Developer PowerShell, or install the C++ build tools.'
}
