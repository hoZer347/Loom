<#
.SYNOPSIS
    Runs a program on a virtual desktop of its own, so its windows never take
    the screen or the keyboard focus from whoever is at the machine.

.DESCRIPTION
    Creates a virtual desktop without switching to it, starts the program with
    `--desktop <id>` added to its arguments (Loom Editor moves its window there
    before showing it), waits for it to exit, then closes every window still on
    that desktop and removes the desktop.

    Creating and removing desktops goes through the shell's undocumented
    IVirtualDesktopManagerInternal, whose layout is specific to Windows 11
    24H2/25H2 (builds 26100+). The script refuses to run on anything older.

.EXAMPLE
    .\run-on-agent-desktop.ps1 -Exe 'x64\Debug\Loom Editor.exe' -- 'C:\MyProject' --play
    .\run-on-agent-desktop.ps1 -Cleanup

.NOTES
    -Cleanup removes desktops left behind by a run that was killed before it
    could tidy up, along with any windows on them.
#>
[CmdletBinding(DefaultParameterSetName = 'Run')]
param(
    [Parameter(ParameterSetName = 'Run', Mandatory = $true)]
    [string] $Exe,

    [Parameter(ParameterSetName = 'Run')]
    [int] $TimeoutSeconds = 600,

    [Parameter(ParameterSetName = 'Run', ValueFromRemainingArguments = $true)]
    [string[]] $ExeArgs = @(),

    [Parameter(ParameterSetName = 'Cleanup', Mandatory = $true)]
    [switch] $Cleanup
)

$ErrorActionPreference = 'Stop'

$minimumBuild = 26100
$namePrefix = 'Loom agent'
$closeGraceMilliseconds = 3000
$pollMilliseconds = 100

if ([Environment]::OSVersion.Version.Build -lt $minimumBuild)
{
    throw "Virtual desktop control needs Windows 11 build $minimumBuild or newer."
}

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace LoomAgentDesktop
{
    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("6D5140C1-7436-11CE-8034-00AA006009FA")]
    interface IServiceProvider10
    {
        [return: MarshalAs(UnmanagedType.IUnknown)]
        object QueryService(ref Guid service, ref Guid riid);
    }

    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("92CA9DCD-5622-4BBA-A805-5E9F541BD8C9")]
    interface IObjectArray
    {
        void GetCount(out int count);
        void GetAt(int index, ref Guid iid, [MarshalAs(UnmanagedType.IUnknown)] out object obj);
    }

    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("3F07F4BE-B107-441A-AF0F-39D82529072C")]
    interface IVirtualDesktop
    {
        bool IsViewVisible(IntPtr view);
        Guid GetId();
        [return: MarshalAs(UnmanagedType.HString)] string GetName();
    }

    // Only the slots up to SetDesktopName are declared; the vtable order is what matters.
    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("53F5CA0B-158F-4124-900C-057158060B27")]
    interface IVirtualDesktopManagerInternal
    {
        int GetCount();
        void MoveViewToDesktop(IntPtr view, IVirtualDesktop desktop);
        bool CanViewMoveDesktops(IntPtr view);
        IVirtualDesktop GetCurrentDesktop();
        void GetDesktops(out IObjectArray desktops);
        [PreserveSig] int GetAdjacentDesktop(IVirtualDesktop from, int direction, out IVirtualDesktop desktop);
        void SwitchDesktop(IVirtualDesktop desktop);
        void SwitchDesktopAndMoveForegroundView(IVirtualDesktop desktop);
        IVirtualDesktop CreateDesktop();
        void MoveDesktop(IVirtualDesktop desktop, int index);
        void RemoveDesktop(IVirtualDesktop desktop, IVirtualDesktop fallback);
        IVirtualDesktop FindDesktop(ref Guid id);
        void GetDesktopSwitchIncludeExcludeViews(IVirtualDesktop desktop, out IObjectArray include, out IObjectArray exclude);
        void SetDesktopName(IVirtualDesktop desktop, [MarshalAs(UnmanagedType.HString)] string name);
    }

    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("A5CD92FF-29BE-454C-8D04-D82879FB3F1B")]
    interface IVirtualDesktopManager
    {
        bool IsWindowOnCurrentVirtualDesktop(IntPtr window);
        Guid GetWindowDesktopId(IntPtr window);
        void MoveWindowToDesktop(IntPtr window, ref Guid desktop);
    }

    public static class Desktops
    {
        static readonly Guid ImmersiveShell = new Guid("C2F03A33-21F5-47FA-B4BB-156362A2F239");
        static readonly Guid ManagerInternalService = new Guid("C5E0CDCA-7B6E-41B2-9FC4-D93975CC467B");
        static readonly Guid ManagerClass = new Guid("AA509086-5CA9-4C25-8F95-589D3C07B48A");

        delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

        [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
        [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
        [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

        const uint WM_CLOSE = 0x0010;

        // GetAdjacentDesktop directions.
        const int Left = 3;
        const int Right = 4;

        static IVirtualDesktopManagerInternal Internal()
        {
            var shell = (IServiceProvider10)Activator.CreateInstance(Type.GetTypeFromCLSID(ImmersiveShell));
            Guid service = ManagerInternalService;
            Guid iid = typeof(IVirtualDesktopManagerInternal).GUID;
            return (IVirtualDesktopManagerInternal)shell.QueryService(ref service, ref iid);
        }

        public static Guid Create(string name)
        {
            var manager = Internal();
            IVirtualDesktop desktop = manager.CreateDesktop();
            manager.SetDesktopName(desktop, name);
            return desktop.GetId();
        }

        public static Dictionary<Guid, string> List()
        {
            var manager = Internal();
            IObjectArray desktops;
            manager.GetDesktops(out desktops);

            int count;
            desktops.GetCount(out count);

            var result = new Dictionary<Guid, string>();
            Guid iid = typeof(IVirtualDesktop).GUID;

            for (int i = 0; i < count; i++)
            {
                object item;
                desktops.GetAt(i, ref iid, out item);
                var desktop = (IVirtualDesktop)item;
                result[desktop.GetId()] = desktop.GetName();
            }

            return result;
        }

        // Visible top-level windows on the desktop.
        public static List<IntPtr> WindowsOn(Guid id)
        {
            var manager = Manager();
            var windows = new List<IntPtr>();

            EnumWindows((window, parameter) =>
            {
                if (IsWindowVisible(window) && DesktopOf(manager, window) == id)
                {
                    windows.Add(window);
                }

                return true;
            }, IntPtr.Zero);

            return windows;
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

        // Whether killing the process would only take windows on this desktop.
        public static bool OnlyOn(uint process, Guid id)
        {
            var manager = Manager();
            bool only = true;

            EnumWindows((window, parameter) =>
            {
                if (IsWindowVisible(window) && ProcessOf(window) == process && DesktopOf(manager, window) != id)
                {
                    only = false;
                }

                return only;
            }, IntPtr.Zero);

            return only;
        }

        // Windows still on a removed desktop are moved to the fallback, so the
        // caller closes them first. The fallback is a neighbour when the user
        // is looking at the desktop being removed.
        public static void Remove(Guid id)
        {
            var manager = Internal();
            IVirtualDesktop desktop = manager.FindDesktop(ref id);
            IVirtualDesktop fallback = manager.GetCurrentDesktop();

            if (fallback.GetId() == id && manager.GetAdjacentDesktop(desktop, Left, out fallback) != 0)
            {
                Marshal.ThrowExceptionForHR(manager.GetAdjacentDesktop(desktop, Right, out fallback));
            }

            manager.RemoveDesktop(desktop, fallback);
        }

        static IVirtualDesktopManager Manager()
        {
            return (IVirtualDesktopManager)Activator.CreateInstance(Type.GetTypeFromCLSID(ManagerClass));
        }

        static Guid DesktopOf(IVirtualDesktopManager manager, IntPtr window)
        {
            try
            {
                return manager.GetWindowDesktopId(window);
            }
            catch (COMException)
            {
                return Guid.Empty;
            }
        }
    }
}
'@

# Asks each window on the desktop to close, and only kills a process that
# ignores it when all of that process's windows are on this desktop.
function Close-Desktop([Guid] $id)
{
    foreach ($window in [LoomAgentDesktop.Desktops]::WindowsOn($id))
    {
        [LoomAgentDesktop.Desktops]::Close($window)
    }

    $deadline = [DateTime]::Now.AddMilliseconds($closeGraceMilliseconds)
    while ([LoomAgentDesktop.Desktops]::WindowsOn($id).Count -gt 0 -and [DateTime]::Now -lt $deadline)
    {
        Start-Sleep -Milliseconds $pollMilliseconds
    }

    $remaining = [LoomAgentDesktop.Desktops]::WindowsOn($id) |
        ForEach-Object { [LoomAgentDesktop.Desktops]::ProcessOf($_) } |
        Sort-Object -Unique

    foreach ($processId in $remaining)
    {
        if ([LoomAgentDesktop.Desktops]::OnlyOn($processId, $id))
        {
            Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue
        }
        else
        {
            Write-Warning "Process $processId kept a window open; it moves to the current desktop."
        }
    }

    [LoomAgentDesktop.Desktops]::Remove($id)
}

if ($Cleanup)
{
    $desktops = [LoomAgentDesktop.Desktops]::List()
    foreach ($id in @($desktops.Keys))
    {
        if ($desktops[$id] -notmatch "^$namePrefix (\d+)$") { continue }

        # Still owned by a run in progress, possibly another session's.
        $owner = Get-Process -Id $Matches[1] -ErrorAction SilentlyContinue
        if ($owner -and $owner.Name -in 'powershell', 'pwsh') { continue }

        Write-Host "Removing $($desktops[$id])"
        Close-Desktop $id
    }
    exit 0
}

$desktop = [LoomAgentDesktop.Desktops]::Create("$namePrefix $PID")
$process = $null

try
{
    $arguments = @('--desktop', $desktop.ToString()) + $ExeArgs |
        ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }

    $process = Start-Process -FilePath $Exe -ArgumentList $arguments -NoNewWindow -PassThru

    # Without the handle cached now, ExitCode reads as null once it has exited.
    $null = $process.Handle

    if (-not $process.WaitForExit($TimeoutSeconds * 1000))
    {
        Write-Warning "$Exe still running after $TimeoutSeconds s; stopping it."
        Stop-Process -Id $process.Id -Force
    }
    $process.WaitForExit()
}
finally
{
    if ($process -and -not $process.HasExited)
    {
        Stop-Process -Id $process.Id -Force
    }
    try
    {
        Close-Desktop $desktop
    }
    catch
    {
        Write-Warning "Could not remove the agent desktop: $_"
    }
}

exit $process.ExitCode
