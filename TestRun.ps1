# Invoke-TestSuite: runs a test executable in the console it was started from,
# and stops it if it runs past a time limit, so a suite that deadlocks fails
# rather than holds whatever ran it. Dot-source it.

function Invoke-TestSuite
{
    param(
        [string] $Path,
        [string[]] $Arguments,
        [int] $TimeoutMinutes
    )

    $millisecondsPerMinute = 60000

    # Not redirected, so it writes to this console or pipe as it goes.
    $info = New-Object System.Diagnostics.ProcessStartInfo $Path
    $info.UseShellExecute = $false
    $info.Arguments = ($Arguments | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join ' '

    $process = [System.Diagnostics.Process]::Start($info)

    if (-not $process.WaitForExit($TimeoutMinutes * $millisecondsPerMinute))
    {
        $process.Kill()
        $process.WaitForExit()
        Write-Error "$(Split-Path -Leaf $Path) ran past $TimeoutMinutes minutes and was stopped."
        return 1
    }

    return $process.ExitCode
}
