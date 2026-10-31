<#
.SYNOPSIS
    Runs a program on a Win32 desktop of its own, so its windows never take the
    screen or the keyboard focus from whoever is at the machine.

.DESCRIPTION
    Creates a desktop that is never the input desktop, starts the program on
    it, waits for it to exit, then closes every window still on that desktop.
    Whatever the program starts inherits the desktop. A window there can be
    shown, activated, maximized or made fullscreen and nothing reaches the
    user's screen, and the desktop goes with its last handle.

    A dialog on the desktop, such as a runtime abort() or assert box, has nobody
    to click it. The script prints its text to stderr, stops the program and
    exits with 1.

    Never use a Windows virtual desktop for this: activating a window on one
    makes Windows switch the user to it.

.EXAMPLE
    .\run-on-agent-desktop.ps1 -Exe 'x64\Debug\Loom Editor.exe' -- 'C:\MyProject' --play
#>
param(
    [Parameter(Mandatory = $true)]
    [string] $Exe,

    [int] $TimeoutSeconds = 600,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $ExeArgs = @()
)

#region Setup

$ErrorActionPreference = 'Stop'

$namePrefix = 'LoomAgent'
$closeGraceMilliseconds = 3000
$pollMilliseconds = 100

#endregion

#region Desktops

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace LoomAgentDesktop
{
    public static class Desktops
    {
        delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateDesktopW(string name, string device, IntPtr mode, uint flags, uint access, IntPtr attributes);

        [DllImport("user32.dll")]
        static extern bool CloseDesktop(IntPtr desktop);

        [DllImport("user32.dll")]
        static extern bool EnumDesktopWindows(IntPtr desktop, EnumWindowsCallback callback, IntPtr parameter);

        [DllImport("user32.dll")]
        static extern bool EnumChildWindows(IntPtr parent, EnumWindowsCallback callback, IntPtr parameter);

        [DllImport("user32.dll")]
        static extern bool IsWindowVisible(IntPtr window);

        [DllImport("user32.dll")]
        static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        static extern int GetWindowTextLength(IntPtr window);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        static extern int GetWindowText(IntPtr window, StringBuilder text, int capacity);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        static extern int GetClassName(IntPtr window, StringBuilder name, int capacity);

        [DllImport("user32.dll")]
        static extern bool PostMessage(IntPtr window, uint message, IntPtr wide, IntPtr low);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool CreateProcessW(string application, StringBuilder command, IntPtr process, IntPtr thread, bool inherit,
            uint flags, IntPtr environment, string directory, ref StartupInfo startup, out ProcessInformation information);

        [DllImport("kernel32.dll")]
        static extern bool CloseHandle(IntPtr handle);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct StartupInfo
        {
            public int cb;
            public string reserved, desktop, title;
            public int x, y, width, height, columns, rows, fill, flags;
            public short show, reservedSize;
            public IntPtr reservedBytes, input, output, error;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct ProcessInformation
        {
            public IntPtr process, thread;
            public int processId, threadId;
        }

        const uint GENERIC_ALL = 0x10000000;
        const uint WM_CLOSE = 0x0010;
        const string WindowStation = @"WinSta0\";

        // What a message box or a runtime error dialog is made from.
        const string DialogClass = "#32770";
        const int ClassNameCapacity = 256;

        public static IntPtr Create(string name)
        {
            IntPtr desktop = CreateDesktopW(name, null, IntPtr.Zero, 0, GENERIC_ALL, IntPtr.Zero);

            if (desktop == IntPtr.Zero)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }

            return desktop;
        }

        public static void Remove(IntPtr desktop)
        {
            CloseDesktop(desktop);
        }

        public static Process Start(string name, string commandLine)
        {
            var startup = new StartupInfo();
            startup.cb = Marshal.SizeOf(typeof(StartupInfo));
            startup.desktop = WindowStation + name;

            ProcessInformation information;

            if (!CreateProcessW(null, new StringBuilder(commandLine), IntPtr.Zero, IntPtr.Zero, true, 0, IntPtr.Zero, null, ref startup, out information))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }

            Process process = Process.GetProcessById(information.processId);
            CloseHandle(information.thread);
            CloseHandle(information.process);
            return process;
        }

        // Visible top-level windows on the desktop.
        public static List<IntPtr> WindowsOn(IntPtr desktop)
        {
            var windows = new List<IntPtr>();

            EnumDesktopWindows(desktop, (window, parameter) =>
            {
                if (IsWindowVisible(window))
                {
                    windows.Add(window);
                }

                return true;
            }, IntPtr.Zero);

            return windows;
        }

        public static List<IntPtr> DialogsOn(IntPtr desktop)
        {
            return WindowsOn(desktop).FindAll(window => ClassOf(window) == DialogClass);
        }

        // The dialog's title, then the text of each control in it.
        public static List<string> TextOf(IntPtr dialog)
        {
            var lines = new List<string> { TitleOf(dialog) };

            EnumChildWindows(dialog, (child, parameter) =>
            {
                string text = TitleOf(child);
                if (text.Length > 0)
                {
                    lines.Add(text);
                }

                return true;
            }, IntPtr.Zero);

            return lines;
        }

        static string ClassOf(IntPtr window)
        {
            var name = new StringBuilder(ClassNameCapacity);
            GetClassName(window, name, name.Capacity);
            return name.ToString();
        }

        static string TitleOf(IntPtr window)
        {
            var text = new StringBuilder(GetWindowTextLength(window) + 1);
            GetWindowText(window, text, text.Capacity);
            return text.ToString();
        }

        public static void Close(IntPtr window)
        {
            PostMessage(window, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
        }

        public static uint ProcessOf(IntPtr window)
        {
            uint process;
            GetWindowThreadProcessId(window, out process);
            return process;
        }
    }
}
'@

# Asks each window on the desktop to close, then kills what ignores that.
function Close-Desktop([IntPtr] $handle)
{
    foreach ($window in [LoomAgentDesktop.Desktops]::WindowsOn($handle))
    {
        [LoomAgentDesktop.Desktops]::Close($window)
    }

    $deadline = [DateTime]::Now.AddMilliseconds($closeGraceMilliseconds)
    while ([LoomAgentDesktop.Desktops]::WindowsOn($handle).Count -gt 0 -and [DateTime]::Now -lt $deadline)
    {
        Start-Sleep -Milliseconds $pollMilliseconds
    }

    [LoomAgentDesktop.Desktops]::WindowsOn($handle) |
        ForEach-Object { [LoomAgentDesktop.Desktops]::ProcessOf($_) } |
        Sort-Object -Unique |
        ForEach-Object { Stop-Process -Id $_ -Force -ErrorAction SilentlyContinue }

    [LoomAgentDesktop.Desktops]::Remove($handle)
}

#endregion

#region Run

$name = "$namePrefix$PID"
$handle = [LoomAgentDesktop.Desktops]::Create($name)
$process = $null
$dialogExitCode = 0

try
{
    $arguments = @($Exe) + $ExeArgs | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }

    $process = [LoomAgentDesktop.Desktops]::Start($name, ($arguments -join ' '))

    # Without the handle cached now, ExitCode reads as null once it has exited.
    $null = $process.Handle

    $deadline = [DateTime]::Now.AddSeconds($TimeoutSeconds)
    while (-not $process.WaitForExit($pollMilliseconds))
    {
        $dialogs = [LoomAgentDesktop.Desktops]::DialogsOn($handle)
        if ($dialogs.Count -gt 0)
        {
            foreach ($dialog in $dialogs)
            {
                $text = [LoomAgentDesktop.Desktops]::TextOf($dialog) -join "`n"
                [Console]::Error.WriteLine("$Exe put up a dialog; stopping it.`n$text")
            }
            $dialogExitCode = 1
            Stop-Process -Id $process.Id -Force
        }
        elseif ([DateTime]::Now -gt $deadline)
        {
            Write-Warning "$Exe still running after $TimeoutSeconds s; stopping it."
            Stop-Process -Id $process.Id -Force
        }
    }
}
finally
{
    if ($process -and -not $process.HasExited)
    {
        Stop-Process -Id $process.Id -Force
    }
    Close-Desktop $handle
}

if ($dialogExitCode) { exit $dialogExitCode }
exit $process.ExitCode

#endregion
