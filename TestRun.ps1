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
    $errors = $process.StandardError.BaseStream.CopyToAsync([Console]::OpenStandardError())
    $deadline = [DateTime]::Now.AddMinutes($TimeoutMinutes)

    # Its output joins this script's, so a caller can still pipe or discard it.
    for (;;)
    {
        $line = $process.StandardOutput.ReadLineAsync()
        $left = [Math]::Max(0, ($deadline - [DateTime]::Now).TotalMilliseconds)

        if (-not $line.Wait([int]$left))
        {
            $process.Kill()
            $process.WaitForExit()
            $global:LASTEXITCODE = 1
            Write-Error "$(Split-Path -Leaf $Path) ran past $TimeoutMinutes minutes and was stopped."
            return
        }

        if ($null -eq $line.Result) { break }

        $line.Result
    }

    $process.WaitForExit()
    $errors.Wait()
    $global:LASTEXITCODE = $process.ExitCode
}
