<#
.SYNOPSIS
    Hands out Loom 1 to Loom 8, the git worktrees agents build features in.

.DESCRIPTION
    Loom itself belongs to the user and is never a slot. Each slot is a
    worktree of this repository beside it. A claim is a create-only ref,
    refs/claims/loom-<n>, so two agents never hold the same slot; the slot's
    worktree lock carries the feature branch for people and for lookups. Any
    slot's copy of this script works on every slot.

.EXAMPLE
    .\slots.ps1 setup
    .\slots.ps1 status
    .\slots.ps1 claim inspector-layout
    .\slots.ps1 merge inspector-layout
    .\slots.ps1 release inspector-layout
    .\slots.ps1 release inspector-layout -Abandon
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
$ClaimPrefix = 'refs/claims/loom-'
$Trunk = 'master'
$NoObject = '0' * 40

# Ignored, prebuilt libraries a fresh checkout lacks. Slots link to the user's
# copies rather than holding several gigabytes each.
$SharedLibraries = @(
    'External Libraries\Vulkan',
    'External Libraries\boost\stage',
    'External Libraries\openssl\bin',
    'External Libraries\openssl\lib'
)

# Both helpers take the directory first and pass the rest to git untouched.
# A named parameter would swallow git's own flags, such as -d for -Directory.
function Invoke-Git
{
    & git -C @args
    if ($LASTEXITCODE -ne 0) { throw "git $($args[1..$args.Count]) failed in $($args[0])" }
}

# For git calls that are allowed to fail. Hosts that turn a native command's
# stderr into error records would otherwise stop the script on the first one.
function Test-Git
{
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'

    try
    {
        & git -C @args 2>$null | Out-Null
        return $LASTEXITCODE -eq 0
    }
    finally
    {
        $ErrorActionPreference = $previous
    }
}

$commonDir = (& git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir)
$userRoot = Split-Path $commonDir -Parent
$slotParent = Split-Path $userRoot -Parent

function Get-SlotPath
{
    param([int] $Number)

    [IO.Path]::GetFullPath((Join-Path $slotParent "$(Split-Path $userRoot -Leaf) $Number"))
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

# The one slot claimed for this feature, as @{ Number; Path; Branch }.
function Find-Claim
{
    if (-not $Feature) { throw "$Command needs the feature name it was claimed with." }

    $branch = "$BranchPrefix$Feature"
    $worktrees = Get-Worktrees
    $found = @(1..$SlotCount | Where-Object { $worktrees[(Get-SlotPath $_)].Locked -eq $branch })

    if ($found.Count -ne 1) { throw "Expected one slot claimed for $branch, found $($found.Count)." }

    return @{ Number = $found[0]; Path = (Get-SlotPath $found[0]); Branch = $branch }
}

function Assert-Clean
{
    param([string] $Slot)

    if (& git -C $Slot status --porcelain)
    {
        throw "$Slot has uncommitted changes. Commit them to the branch first."
    }
}

function Remove-Claim
{
    param([int] $Number)

    Test-Git $userRoot worktree unlock (Get-SlotPath $Number) | Out-Null
    Test-Git $userRoot update-ref -d "$ClaimPrefix$Number" | Out-Null
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
            $slot = Get-SlotPath $number
            $record = $worktrees[$slot]

            if (-not $record)
            {
                Write-Output "Loom $number  missing, run setup"
                continue
            }

            $state = if ($null -ne $record.Locked) { "claimed  $($record.Locked)" } else { 'free' }
            $dirty = @(& git -C $slot status --porcelain).Count
            $note = if ($dirty) { "  ($dirty uncommitted)" } else { '' }
            Write-Output "Loom $number  $state$note"
        }
    }

    'claim'
    {
        if (-not $Feature) { throw 'claim needs a feature name, e.g. .\slots.ps1 claim inspector-layout' }

        $branch = "$BranchPrefix$Feature"
        if (Test-Git $userRoot show-ref --verify --quiet "refs/heads/$branch") { throw "$branch already exists." }

        $worktrees = Get-Worktrees
        $trunkHead = (& git -C $userRoot rev-parse $Trunk)

        foreach ($number in 1..$SlotCount)
        {
            $slot = Get-SlotPath $number
            $record = $worktrees[$slot]

            if (-not $record -or $null -ne $record.Locked -or $record.Branch) { continue }
            if (& git -C $slot status --porcelain) { continue }

            # Create-only, so of several agents racing for this slot exactly one gets it.
            if (-not (Test-Git $userRoot update-ref "$ClaimPrefix$number" $trunkHead $NoObject)) { continue }

            try
            {
                Invoke-Git $userRoot worktree lock --reason $branch $slot
                Assert-Clean $slot
                Invoke-Git $slot switch --quiet --create $branch $Trunk
            }
            catch
            {
                Remove-Claim $number
                throw
            }

            Write-Output $slot
            return
        }

        throw 'Every slot is claimed. Check .\slots.ps1 status.'
    }

    'merge'
    {
        $claim = Find-Claim
        $slot = $claim.Path
        Assert-Clean $slot

        if ((& git -C $slot branch --show-current) -ne $claim.Branch)
        {
            throw "$slot is not on $($claim.Branch)."
        }

        foreach ($record in (Get-Worktrees).Values)
        {
            if ($record.Branch -eq $Trunk)
            {
                throw "$Trunk is checked out in a worktree, so it cannot be moved without touching that tree."
            }
        }

        $base = (& git -C $userRoot rev-parse $Trunk)
        if (-not (Test-Git $slot rebase --quiet $base))
        {
            throw "Rebasing onto $Trunk hit conflicts in $slot. Resolve them and git rebase --continue " +
                "(or git rebase --abort), then run merge again."
        }

        foreach ($suite in 'run-tests.ps1', 'run-web-tests.ps1')
        {
            & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $slot $suite)
            if ($LASTEXITCODE -ne 0) { throw "$suite failed. Nothing was merged." }
        }

        # Compare-and-swap: refused if another slot merged since the rebase.
        $head = (& git -C $slot rev-parse HEAD)
        if (-not (Test-Git $userRoot update-ref -m "merge $($claim.Branch)" "refs/heads/$Trunk" $head $base))
        {
            throw "$Trunk moved while the tests ran. Run merge again."
        }

        Write-Output "$($claim.Branch) is on $Trunk."
    }

    'release'
    {
        $claim = Find-Claim
        $slot = $claim.Path
        Assert-Clean $slot

        if (& git -C $slot branch --list $claim.Branch)
        {
            $merged = Test-Git $slot merge-base --is-ancestor $claim.Branch $Trunk

            if (-not $merged -and -not $Abandon)
            {
                throw "$($claim.Branch) is not on $Trunk. Merge it, or release -Abandon to drop it."
            }

            Invoke-Git $slot switch --quiet --detach $Trunk
            Invoke-Git $slot branch --quiet $(if ($merged) { '-d' } else { '-D' }) $claim.Branch
        }

        Remove-Claim $claim.Number
        Write-Output "$slot is free."
    }
}
