<#
.SYNOPSIS
    Publishes the Loom UI folder to its own repository.

.DESCRIPTION
    Splits the history of "Loom UI" into a branch of its own with git subtree
    and pushes it to the main branch of the LoomUI repository.

.PARAMETER Remote
    The repository to push to.

.PARAMETER Branch
    The branch of that repository to push to.

.EXAMPLE
    .\sync-loom-ui.ps1
#>
param(
    [string] $Remote = 'https://github.com/hoZer347/LoomUI.git',
    [string] $Branch = 'main'
)

$ErrorActionPreference = 'Stop'

$prefix = 'Loom UI'

Push-Location $PSScriptRoot

try
{
    $split = git subtree split --prefix="$prefix"

    if ($LASTEXITCODE -ne 0)
    {
        throw "git subtree split failed with exit code $LASTEXITCODE"
    }

    git push $Remote "${split}:refs/heads/$Branch"

    if ($LASTEXITCODE -ne 0)
    {
        throw "git push failed with exit code $LASTEXITCODE"
    }
}
finally
{
    Pop-Location
}
