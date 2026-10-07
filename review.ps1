<#
.SYNOPSIS
    Asks the user to approve or deny a finished feature.

.DESCRIPTION
    Opens a window that shows what was asked, what changed and what to expect,
    with a box for feedback and Approve / Deny buttons. It comes up without
    taking the focus and flashes in the taskbar instead. Blocks until the user
    answers, then prints {"decision": "approve" | "deny", "feedback": "..."}
    and exits 0 on approve, 1 otherwise. Closing the window counts as deny.
    Given -DemoProcessId, it closes that demo once the user answers.

.EXAMPLE
    .\review.ps1 -Feature inspector-layout -Prompt 'Name should not be on a newline' `
        -Changes 'Name sits beside its field' -Expect 'Select any object in Demos\Shadows' `
        -DemoProcessId $demo.Id
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Feature,
    [Parameter(Mandatory = $true)] [string] $Prompt,
    [Parameter(Mandatory = $true)] [string] $Changes,
    [Parameter(Mandatory = $true)] [string] $Expect,

    # The demo left open for the review, closed once the user answers.
    [int] $DemoProcessId,

    # For testing the window itself: no taskbar button, no flash, off every screen.
    [switch] $Offscreen
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Windows.Forms, System.Drawing

Add-Type -ReferencedAssemblies System.Windows.Forms -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Windows.Forms;

public class ReviewForm : Form
{
    [StructLayout(LayoutKind.Sequential)]
    struct FLASHWINFO
    {
        public uint cbSize;
        public IntPtr hwnd;
        public uint dwFlags;
        public uint uCount;
        public uint dwTimeout;
    }

    [DllImport("user32.dll")]
    static extern bool FlashWindowEx(ref FLASHWINFO info);

    const uint FLASHW_ALL = 3;
    const uint FLASHW_TIMERNOFG = 12;

    protected override bool ShowWithoutActivation { get { return true; } }

    // Flashes the taskbar button until the window comes to the foreground.
    public void Flash()
    {
        FLASHWINFO info = new FLASHWINFO();
        info.cbSize = (uint)Marshal.SizeOf(info);
        info.hwnd = Handle;
        info.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
        FlashWindowEx(ref info);
    }
}
'@

$FormWidth = 760
$FormHeight = 720
$Margin = 12
$FeedbackHeight = 140
$ButtonWidth = 110
$ButtonHeight = 34
$FontSize = 10
$OffscreenCoordinate = -32000
$DemoCloseTimeoutMs = 5000

$font = New-Object System.Drawing.Font('Segoe UI', $FontSize)

$form = New-Object ReviewForm
$form.Text = "Review: $Feature"
$form.ClientSize = New-Object System.Drawing.Size($FormWidth, $FormHeight)
$form.StartPosition = 'CenterScreen'

if ($Offscreen)
{
    $form.StartPosition = 'Manual'
    $form.Location = New-Object System.Drawing.Point($OffscreenCoordinate, $OffscreenCoordinate)
    $form.ShowInTaskbar = $false
}

$form.Font = $font

$summary = New-Object System.Windows.Forms.TextBox
$summary.Multiline = $true
$summary.ReadOnly = $true
$summary.ScrollBars = 'Vertical'
$summary.BackColor = [System.Drawing.SystemColors]::Window
$summary.Text = (@(
    'PROMPT', $Prompt, '',
    'CHANGES', $Changes, '',
    'WHAT TO EXPECT', $Expect
) -join "`r`n") -replace "(?<!`r)`n", "`r`n"

$label = New-Object System.Windows.Forms.Label
$label.Text = 'Feedback'
$label.AutoSize = $true

$feedback = New-Object System.Windows.Forms.TextBox
$feedback.Multiline = $true
$feedback.ScrollBars = 'Vertical'
$feedback.AcceptsReturn = $true

$approve = New-Object System.Windows.Forms.Button
$approve.Text = 'Approve'
$approve.Add_Click({ $script:decision = 'approve'; $form.Close() })

$deny = New-Object System.Windows.Forms.Button
$deny.Text = 'Deny'
$deny.Add_Click({ $form.Close() })

$layout =
{
    $width = $form.ClientSize.Width - 2 * $Margin
    $buttonsTop = $form.ClientSize.Height - $Margin - $ButtonHeight
    $feedbackTop = $buttonsTop - $Margin - $FeedbackHeight

    $deny.SetBounds($form.ClientSize.Width - $Margin - $ButtonWidth, $buttonsTop, $ButtonWidth, $ButtonHeight)
    $approve.SetBounds($deny.Left - $Margin - $ButtonWidth, $buttonsTop, $ButtonWidth, $ButtonHeight)
    $feedback.SetBounds($Margin, $feedbackTop, $width, $FeedbackHeight)
    $label.Location = New-Object System.Drawing.Point($Margin, ($feedbackTop - $label.PreferredHeight - $Margin / 2))
    $summary.SetBounds($Margin, $Margin, $width, $label.Top - 2 * $Margin)
}

$form.Controls.AddRange(@($summary, $label, $feedback, $approve, $deny))
$form.Add_Resize($layout)
$form.Add_FormClosing({ $script:written = $feedback.Text })
$form.Add_Shown({ $summary.SelectionLength = 0; if (-not $Offscreen) { $form.Flash() } })
& $layout

# Run rather than ShowDialog, which would take the focus.
$decision = 'deny'
$written = ''

# Holding the handle for the whole review stops Windows reusing the id if the
# user closes the demo first.
$demo = $null

if ($DemoProcessId)
{
    $demo = Get-Process -Id $DemoProcessId -ErrorAction SilentlyContinue

    if ($demo) { $null = $demo.Handle }
}

[System.Windows.Forms.Application]::Run($form)

if ($demo -and -not $demo.HasExited -and -not ($demo.CloseMainWindow() -and $demo.WaitForExit($DemoCloseTimeoutMs)))
{
    try { $demo.Kill() } catch [System.InvalidOperationException] { }
}

[pscustomobject]@{ decision = $decision; feedback = $written } | ConvertTo-Json -Compress
exit $(if ($decision -eq 'approve') { 0 } else { 1 })
