# Copy-ProjectFiles: mirrors a project into a build's folder without what only
# the editor reads, for build-game.ps1 and play-web.ps1. Dot-source it.

# The build folder, the scripts, which are in the program, dot-folders, the
# editor's own files, and Output itself when it is inside the project are left
# out. Returns whether it copied, and writes robocopy's lines when it did not.
function Copy-ProjectFiles([string] $Project, [string] $Output, [string] $Scripts)
{
    $folders = @((Join-Path $Project 'Build'), '.*')
    $files = @('*.loomproject', '*.loomscripts', '*.loomsounds', '*.loomfonts', '*.loombuild', '*.loomgame',
        '*.ps1', '*.py', '*.md', '*.ini', '*.log', '.*', '*.exe', '*.dll', '*.pdb')

    if ($Scripts)
    {
        $folders += $Scripts
    }

    if ($Output.StartsWith("$Project\", [System.StringComparison]::OrdinalIgnoreCase))
    {
        $folders += $Output
    }

    # Below 8, robocopy's exit code says what it copied rather than that it failed.
    $robocopyFailed = 8

    $said = robocopy $Project $Output /MIR /XD $folders /XF $files /NJH /NJS /NP /NFL /NDL 2>&1

    if ($LASTEXITCODE -ge $robocopyFailed)
    {
        $said | ForEach-Object { Write-Host "$_" }
        return $false
    }

    return $true
}
