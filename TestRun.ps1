# Invoke-TestSuite: runs a test executable, passing its output on as it comes,
# and stops it if it runs past a time limit, so a suite that deadlocks fails
# rather than holds whatever ran it. Sets $LASTEXITCODE as a native command
# would. Dot-source it.

function Invoke-TestSuite
{
    param(
        [string] $Path,
        [string[]] $Arguments,
        [int] $TimeoutMinutes
    )

    # Quoted as CommandLineToArgvW reads them back: backslashes before a quote
    # or the closing quote are doubled.
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
        Write-Error "$(Split-Path -Leaf $Path) ran past $TimeoutMinutes minutes and was stopped."
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
