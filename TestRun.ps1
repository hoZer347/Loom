# Invoke-TestSuite: runs a test executable, passing its output on as it comes,
# and stops it if it runs past a time limit, so a suite that deadlocks fails
# rather than holds whatever ran it. Sets $LASTEXITCODE as a native command
# would. Given a progress log, the suite writes each case to it as it runs, and
# a run stopped at its limit names the case it stopped in. Dot-source it.

function Invoke-TestSuite
{
    param(
        [string] $Path,
        [string[]] $Arguments,
        [int] $TimeoutMinutes,
        [string] $ProgressLog
    )

    # Quoted as CommandLineToArgvW reads them back: backslashes before a quote
    # or the closing quote are doubled.
    if ($ProgressLog)
    {
        "Progress: $ProgressLog"
        $Arguments = @($Arguments) + "--progress-log=$ProgressLog"
    }

    $quoted = $Arguments | ForEach-Object { '"' + (($_ -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"' }

    $info = New-Object System.Diagnostics.ProcessStartInfo $Path
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.Arguments = $quoted -join ' '

    $process = [System.Diagnostics.Process]::Start($info)
    $deadline = [DateTime]::Now.AddMinutes($TimeoutMinutes)
    $left = { [int][Math]::Max(0, ($deadline - [DateTime]::Now).TotalMilliseconds) }

    # Stopped however this ends, even by a caller that stops reading early.
    try
    {
        $errors = $process.StandardError.BaseStream.CopyToAsync([Console]::OpenStandardError())

        # Its output joins this script's, so a caller can still pipe or discard it.
        for (;;)
        {
            $line = $process.StandardOutput.ReadLineAsync()

            if (-not $line.Wait((& $left)) -or $null -eq $line.Result) { break }

            $line.Result
        }

        if ($process.WaitForExit((& $left)))
        {
            [void]$errors.Wait((& $left))
            $global:LASTEXITCODE = $process.ExitCode
            return
        }

        $global:LASTEXITCODE = 1
        $stopped = "$(Split-Path -Leaf $Path) ran past $TimeoutMinutes minutes and was stopped."

        if ($ProgressLog -and (Test-Path -LiteralPath $ProgressLog))
        {
            $stopped += " Its progress log ends: $(Get-Content -LiteralPath $ProgressLog -Tail 1)"
        }

        Write-Error $stopped
    }
    finally
    {
        if (-not $process.HasExited)
        {
            $process.Kill()
            $process.WaitForExit()
        }
    }
}
