<#
.SYNOPSIS
    Checks which file update-editor.ps1 judges an extension's staleness on.

.DESCRIPTION
    Runs only update-editor.ps1's Update-Extensions, against scratch folders and
    a stand-in update-extension.ps1 that leaves a mark when it runs. A manifest
    with no Library is judged on <name>.dll in the extensions folder. One with a
    Library is judged on that file, whatever the extensions folder holds.
    Throws naming what failed.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File 'C:\Users\3hoze\Desktop\Loom 1\Loom Tests\update-editor-library.ps1'
#>
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot
$Script = Join-Path $Root 'update-editor.ps1'
$Scratch = Join-Path ([IO.Path]::GetTempPath()) "loom-update-library-test-$PID"
$ManifestExtension = '.extension.json'
$Mark = 'rebuilt.txt'
$MinutesApart = 5

$ast = [Management.Automation.Language.Parser]::ParseFile($Script, [ref]$null, [ref]$null)
$function = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Update-Extensions' }, $true)
if (-not $function) { throw 'update-editor.ps1 has no Update-Extensions.' }
$LoomRoot = $PSScriptRoot
Invoke-Expression $function.Extent.Text.Replace('$PSScriptRoot', '$LoomRoot')

function New-Case([string] $Name, [bool] $InFolder, [bool] $InLibrary, [string] $Library)
{
    $source = Join-Path $Scratch "$Name Source"
    New-Item -ItemType Directory -Force -Path $source | Out-Null
    Set-Content -LiteralPath (Join-Path $source 'update-extension.ps1') -Value "Set-Content -LiteralPath '$(Join-Path $source $Mark)' -Value 'yes'"

    $manifest = @{ Source = $source; LoomRoot = $LoomRoot }
    if ($Library) { $manifest.Library = $Library }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Extensions "$Name$ManifestExtension")

    $old = $Editor.LastWriteTimeUtc.AddMinutes(-$MinutesApart)
    $new = $Editor.LastWriteTimeUtc.AddMinutes($MinutesApart)

    foreach ($file in @(@($InFolder, (Join-Path $Extensions "$Name.dll")), @($InLibrary, $Library)))
    {
        if (-not $file[1]) { continue }
        Set-Content -LiteralPath $file[1] -Value 'x'
        (Get-Item -LiteralPath $file[1]).LastWriteTimeUtc = if ($file[0]) { $new } else { $old }
    }

    Join-Path $source $Mark
}

try
{
    $Extensions = Join-Path $Scratch 'Extensions'
    New-Item -ItemType Directory -Force -Path $Extensions | Out-Null
    $built = Join-Path $Scratch 'Built'
    New-Item -ItemType Directory -Force -Path $built | Out-Null

    $editorFile = Join-Path $Scratch 'Loom Editor.exe'
    Set-Content -LiteralPath $editorFile -Value 'x'
    $Editor = Get-Item -LiteralPath $editorFile

    $plainStale = New-Case 'Plain Stale' $false $false ''
    $plainFresh = New-Case 'Plain Fresh' $true $false ''
    $libraryStale = New-Case 'Library Stale' $true $false (Join-Path $built 'Library Stale.dll')
    $libraryFresh = New-Case 'Library Fresh' $false $true (Join-Path $built 'Library Fresh.dll')
    $libraryMissing = New-Case 'Library Missing' $true $false (Join-Path $built 'Nowhere.dll')
    Remove-Item -LiteralPath (Join-Path $built 'Nowhere.dll') -ErrorAction SilentlyContinue

    Update-Extensions | Out-Null

    $expected = [ordered]@{
        'no Library and no dll in the folder'     = @($plainStale, $true)
        'no Library, a current dll in the folder' = @($plainFresh, $false)
        'a stale Library, a current folder dll'   = @($libraryStale, $true)
        'a current Library, no folder dll'        = @($libraryFresh, $false)
        'a missing Library'                       = @($libraryMissing, $true)
    }

    foreach ($case in $expected.Keys)
    {
        $rebuilt = Test-Path -LiteralPath $expected[$case][0]
        if ($rebuilt -ne $expected[$case][1]) { throw "$case`: rebuilt was $rebuilt, expected $($expected[$case][1])." }
    }

    Write-Output 'update-editor-library passed.'
}
finally
{
    Remove-Item -LiteralPath $Scratch -Recurse -Force -ErrorAction SilentlyContinue
}
