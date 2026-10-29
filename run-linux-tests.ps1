<#
.SYNOPSIS
    Builds this tree on the Linux guest and runs the Loom test suite there.

.EXAMPLE
    .\run-linux-tests.ps1
    .\run-linux-tests.ps1 -TestArgs '-ts=Compute'
    .\run-linux-tests.ps1 -Targets LoomEditor -NoTests

.NOTES
    The guest keeps one clone of Loom at ~/loom. A run first moves the clone
    up to where this tree meets master, if that is newer, with a bundle of
    the commits it lacks. Then it sends the files this tree changes against
    that commit, puts back any the last run changed that this one does not,
    and builds with Ninja. Runs from several trees take turns on a lock.
    Exits with the runner's code.
#>
[CmdletBinding()]
param(
    [string] $Guest = 'liam@mint.mshome.net',

    [string] $Key = (Join-Path $HOME '.ssh\mint_ed25519'),

    [string[]] $Targets = @('LoomTests', 'LoomEditor', 'LoomPlayer'),

    [switch] $NoTests,

    # Many times what the suite takes on the guest; past it the run has deadlocked.
    [int] $TimeoutMinutes = 20,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $TestArgs = @()
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

#region Guest script

# Arguments: base commit, files sent, targets, deleted files, test arguments,
# whether to test, timeout in minutes, the clone's commit. Lists arrive base64
# encoded, '-' when empty. A loom.bundle beside the script moves the clone to base.
$guestScript = @'
set -e
base=$1; count=$2; targets=$3; deleted=$4; testargs=$5; test=$6; minutes=$7; head=$8
here=$(dirname "$0")
trap 'rm -rf "$here"' EXIT
stage=$here/stage
mkdir -p "$stage"
tar -xf - -C "$stage"
decode() { [ "$1" = - ] || echo "$1" | base64 -d; }

exec 9>"$HOME/loom.lock"
flock 9
cd ~/loom
if [ "$(git rev-parse HEAD)" != "$head" ]; then echo "Another run moved the guest's clone; run this again"; exit 1; fi
# Only HEAD and the index move, so the files the last run sent are put back below.
if [ -f "$here/loom.bundle" ]; then git fetch -q "$here/loom.bundle" master; git reset -q "$base"; fi

(cd "$stage" && find . -type f -printf '%P\n') > "$here/sent"
received=$(wc -l < "$here/sent")
if [ "$received" -ne "$count" ]; then echo "Received $received of $count files"; exit 1; fi
decode "$deleted" > "$here/deleted"
{ git diff --name-only "$base" 2>/dev/null; git ls-files -o --exclude-standard -- . ':!build'; } | sort -u > "$here/changed"
sort -u "$here/sent" "$here/deleted" | comm -23 "$here/changed" - | while IFS= read -r f; do
    if git cat-file -e "$base:$f" 2>/dev/null; then git checkout -q "$base" -- "$f"; else rm -f "$f"; fi
done
while IFS= read -r f; do
    if ! cmp -s "$stage/$f" "$f"; then mkdir -p "$(dirname "$f")"; cp "$stage/$f" "$f"; fi
done < "$here/sent"
while IFS= read -r f; do rm -f "$f"; done < "$here/deleted"

[ -f build/build.ninja ] || cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14 -DCMAKE_C_COMPILER=gcc-14 > /dev/null
ninja --quiet -C build $targets
[ "$test" = 1 ] || exit 0

set -a; . /etc/environment; set +a
cd x64/Release
mapfile -t args < <(decode "$testargs")
set +e
timeout "${minutes}m" xvfb-run -a -s "-screen 0 1920x1080x24" ./"Loom Tests" --no-colors "${args[@]}"
code=$?
[ $code -ne 124 ] || echo "Loom Tests ran past $minutes minutes"
exit $code
'@

#endregion

#region Helpers

function ConvertTo-Base64([string[]] $lines)
{
    if ($lines.Count -eq 0) { return '-' }
    return [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($lines -join "`n") + "`n"))
}

function Invoke-Guest([string] $command)
{
    $output = & ssh -i $Key -o BatchMode=yes $Guest $command
    if ($LASTEXITCODE -ne 0) { throw "ssh to $Guest failed with exit code $LASTEXITCODE" }
    return $output
}

function Invoke-Git([string[]] $arguments)
{
    $output = & git -C $root -c core.quotepath=off @arguments
    if ($LASTEXITCODE -ne 0) { throw "git $($arguments -join ' ') failed with exit code $LASTEXITCODE" }
    return @($output | Where-Object { $_ })
}

#endregion

$head = (Invoke-Guest 'git -C ~/loom rev-parse HEAD').Trim()
& git -C $root cat-file -e "$head^{commit}" 2>$null
if ($LASTEXITCODE -ne 0) { throw "This tree does not have the guest's commit $head" }

$base = $head
$meeting = @(Invoke-Git @('merge-base', 'HEAD', 'master'))[0]
& git -C $root merge-base --is-ancestor $head $meeting
if ($LASTEXITCODE -eq 0 -and $meeting -ne $head) { $base = $meeting }

$dir = (Invoke-Guest 'mktemp -d').Trim()

$present = @(Invoke-Git @('diff', '--name-only', '--diff-filter=d', $base)) + @(Invoke-Git @('ls-files', '-o', '--exclude-standard'))
$deleted = Invoke-Git @('diff', '--name-only', '--diff-filter=D', $base)

$list = New-TemporaryFile
$bundle = Join-Path ([IO.Path]::GetTempPath()) "loom-$PID.bundle"
try
{
    [IO.File]::WriteAllLines($list.FullName, [string[]] $present)

    if ($base -ne $head)
    {
        Invoke-Git @('bundle', 'create', '-q', $bundle, "^$head", 'master') | Out-Null
        & scp -q -i $Key -o BatchMode=yes $bundle "${Guest}:$dir/loom.bundle"
        if ($LASTEXITCODE -ne 0) { throw "scp to $Guest failed with exit code $LASTEXITCODE" }
        Write-Host "Moving the guest's clone from $($head.Substring(0, 9)) to $($base.Substring(0, 9))"
    }

    $script = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($guestScript.Replace("`r`n", "`n")))
    $test = if ($NoTests) { 0 } else { 1 }
    $remote = "echo $script | base64 -d > $dir/run.sh && bash $dir/run.sh $base $($present.Count) '$($Targets -join ' ')' $(ConvertTo-Base64 $deleted) $(ConvertTo-Base64 $TestArgs) $test $TimeoutMinutes $head"

    Write-Host "Building $($Targets -join ', ') on $Guest from $($present.Count) changed files"

    # Through cmd, since a PowerShell pipe would re-encode the tar as text, and with
    # Windows' own tar, since another on the path reads a drive letter as a host.
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    cmd /c "$tar -cf - -C `"$root`" -T `"$($list.FullName)`" | ssh -i `"$Key`" -o BatchMode=yes $Guest `"$remote`""
    exit $LASTEXITCODE
}
finally
{
    Remove-Item $list.FullName
    Remove-Item $bundle -ErrorAction SilentlyContinue
    # The guest script removes it itself, unless it never ran.
    & ssh -i $Key -o BatchMode=yes $Guest "rm -rf $dir" 2>$null
}
