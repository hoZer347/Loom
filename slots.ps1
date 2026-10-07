<#
.SYNOPSIS
    Hands out Loom 1 to Loom 8, the git worktrees agents build features in.

.DESCRIPTION
    Loom itself belongs to the user and is never a slot. Each slot is a
    worktree of this repository beside it, and a claimed slot is locked with
    `git worktree lock`, so two agents never hold the same one. Any slot's copy
    of this script works on every slot; merge and release act on the slot the
    script is run from.

.EXAMPLE
    .\slots.ps1 setup
    .\slots.ps1 status
    .\slots.ps1 claim inspector-layout
    .\slots.ps1 merge
    .\slots.ps1 release
    .\slots.ps1 release -Abandon
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('setup', 'status', 'claim', 'merge', 'release')]
    [string] $Command,

    [Parameter(Position = 1)]
    [string] $Feature,

    [switch] $Abandon
)

$ErrorActionPreference = 'Stop'

$SlotCount = 8
$BranchPrefix = 'feature/'
$Trunk = 'master'

# Ignored, prebuilt libraries a fresh checkout lacks. Slots link to the user's
# copies rather than holding several gigabytes each.
$SharedLibraries = @(
    'External Libraries\Vulkan',
    'External Libraries\boost\stage',
    'External Libraries\openssl\bin',
    'External Libraries\openssl\lib'
)

function Invoke-Git
{
    param([string] $Directory)

    & git -C $Directory @args
    if ($LASTEXITCODE -ne 0) { throw "git $args failed in $Directory" }
}

$commonDir = (& git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir)
$userRoot = Split-Path $commonDir -Parent
$slotParent = Split-Path $userRoot -Parent

function Get-SlotPath([int] $Number)
{
    Join-Path $slotParent "$(Split-Path $userRoot -Leaf) $Number"
}

# git worktree list --porcelain, one record per worktree, keyed by full path.
function Get-Worktrees
{
    $records = @{}
    $current = $null

    foreach ($line in (& git -C $userRoot worktree list --porcelain))
    {
        if ($line -match '^worktree (.+)$')
        {
            $current = @{ Branch = $null; Locked = $null }
            $records[[IO.Path]::GetFullPath($Matches[1])] = $current
        }
        elseif ($line -match '^branch refs/heads/(.+)$') { $current.Branch = $Matches[1] }
        elseif ($line -match '^locked ?(.*)$') { $current.Locked = $Matches[1] }
    }

    return $records
}

function Get-ThisSlot
{
    $root = [IO.Path]::GetFullPath($PSScriptRoot)

    foreach ($number in 1..$SlotCount)
    {
        if ([IO.Path]::GetFullPath((Get-SlotPath $number)) -eq $root) { return $root }
    }

    throw "$root is not a slot. Run merge and release with the slot's own copy of this script."
}

function Assert-Clean([string] $Slot)
{
    if (& git -C $Slot status --porcelain) { throw "$Slot has uncommitted changes. Commit them to the branch first." }
}

switch ($Command)
{
    'setup'
    {
        foreach ($number in 1..$SlotCount)
        {
            $slot = Get-SlotPath $number

            if (-not (Test-Path $slot))
            {
                Write-Host "Creating $slot"
                Invoke-Git $userRoot worktree add --quiet --detach $slot $Trunk
            }

            foreach ($library in $SharedLibraries)
            {
                $source = Join-Path $userRoot $library
                $link = Join-Path $slot $library

                if ((Test-Path $source) -and -not (Test-Path $link))
                {
                    New-Item -ItemType Junction -Path $link -Target $source | Out-Null
                }
            }
        }
    }

    'status'
    {
        $worktrees = Get-Worktrees

        foreach ($number in 1..$SlotCount)
        {
            $slot = [IO.Path]::GetFullPath((Get-SlotPath $number))
            $record = $worktrees[$slot]

            if (-not $record)
            {
                Write-Output "Loom $number  missing, run setup"
                continue
            }

            $state = if ($null -ne $record.Locked) { "claimed  $($record.Branch)" } else { 'free' }
            $dirty = @(& git -C $slot status --porcelain).Count
            Write-Output ("Loom {0}  {1}{2}" -f $number, $state, $(if ($dirty) { "  ($dirty uncommitted)" } else { '' }))
        }
    }

    'claim'
    {
        if (-not $Feature) { throw 'claim needs a feature name, e.g. .\slots.ps1 claim inspector-layout' }

        $branch = "$BranchPrefix$Feature"
        & git -C $userRoot show-ref --verify --quiet "refs/heads/$branch"
        if ($LASTEXITCODE -eq 0) { throw "$branch already exists." }

        $worktrees = Get-Worktrees

        foreach ($number in 1..$SlotCount)
        {
            $slot = [IO.Path]::GetFullPath((Get-SlotPath $number))
            $record = $worktrees[$slot]

            if (-not $record -or $null -ne $record.Locked -or $record.Branch) { continue }

            # The lock is the claim. Losing a race to another agent fails here.
            & git -C $userRoot worktree lock --reason $branch $slot
            if ($LASTEXITCODE -ne 0) { continue }

            if (& git -C $slot status --porcelain)
            {
                Invoke-Git $userRoot worktree unlock $slot
                continue
            }

            Invoke-Git $slot switch --quiet --create $branch $Trunk
            Write-Output $slot
            return
        }

        throw 'Every slot is claimed. Check .\slots.ps1 status.'
    }

    'merge'
    {
        $slot = Get-ThisSlot
        $branch = (& git -C $slot branch --show-current)
        if (-not $branch -or -not $branch.StartsWith($BranchPrefix)) { throw "$slot is not on a $BranchPrefix branch." }
        Assert-Clean $slot

        foreach ($record in (Get-Worktrees).Values)
        {
            if ($record.Branch -eq $Trunk) { throw "$Trunk is checked out in a worktree, so it cannot be moved without touching that tree." }
        }

        $base = (& git -C $userRoot rev-parse $Trunk)
        Invoke-Git $slot rebase --quiet $base

        foreach ($suite in 'run-tests.ps1', 'run-web-tests.ps1')
        {
            & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $slot $suite)
            if ($LASTEXITCODE -ne 0) { throw "$suite failed. Nothing was merged." }
        }

        # Compare-and-swap: refused if another slot merged since the rebase.
        & git -C $userRoot update-ref -m "merge $branch" "refs/heads/$Trunk" (& git -C $slot rev-parse HEAD) $base
        if ($LASTEXITCODE -ne 0) { throw "$Trunk moved while the tests ran. Run merge again." }

        Write-Output "$branch is on $Trunk."
    }

    'release'
    {
        $slot = Get-ThisSlot
        $branch = (& git -C $slot branch --show-current)
        Assert-Clean $slot

        if ($branch)
        {
            & git -C $slot merge-base --is-ancestor HEAD $Trunk
            $merged = $LASTEXITCODE -eq 0
            if (-not $merged -and -not $Abandon) { throw "$branch is not on $Trunk. Merge it, or release -Abandon to drop it." }

            Invoke-Git $slot switch --quiet --detach $Trunk
            Invoke-Git $slot branch --quiet $(if ($merged) { '-d' } else { '-D' }) $branch
        }

        Invoke-Git $userRoot worktree unlock $slot
        Write-Output "$slot is free."
    }
}
